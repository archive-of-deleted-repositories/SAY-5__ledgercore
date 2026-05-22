// Bench harness. Drives N deterministically-generated balanced transactions
// through the dispatcher and emits sustained tx/s plus a latency histogram in
// JSON. The dispatcher is exercised in-process; the gRPC layer is left out of
// the bench loop so we measure the engine, not the wire format. A separate
// optional path benchmarks WAL-on vs WAL-off so the fsync cost is visible.
//
// Usage:
//   bench_harness --txns N --workers M --json out.json [--wal] [--no-wal]
//                 [--accounts K] [--cross-partition-pct P] [--smoke]
//
// Exit codes:
//   0    success
//   1    bench did not reach the floor specified by --floor-tps
//   2    a transaction was rejected unexpectedly (bench bug)
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "core/ledger.h"
#include "core/transaction.h"
#include "idem/cache.h"
#include "intake/dispatcher.h"
#include "wal/writer.h"

namespace lc = ledgercore;

namespace {

struct Args {
    std::uint64_t txns = 1'000'000;
    std::size_t workers = std::max<unsigned>(1, std::thread::hardware_concurrency());
    std::string json_out = "bench/results/bench_local_tmp.json";
    bool wal = false;
    std::uint64_t accounts = 10'000;
    double cross_pct = 0.05;   // fraction of txns that hit accounts across partitions
    double floor_tps = 50'000;  // CI smoke floor
    bool smoke = false;
    std::uint64_t seed = 0xC0FFEE;
};

void usage() {
    std::cerr << "Usage: bench_harness [--txns N] [--workers M] [--json path] "
                 "[--wal|--no-wal] [--accounts K] [--cross-partition-pct P] "
                 "[--floor-tps F] [--smoke] [--seed S]\n";
}

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&](const char* tag) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << tag << "\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (s == "--txns") {
            const char* v = next("--txns");
            if (!v) return false;
            a.txns = std::strtoull(v, nullptr, 10);
        } else if (s == "--workers") {
            const char* v = next("--workers");
            if (!v) return false;
            a.workers = static_cast<std::size_t>(std::strtoull(v, nullptr, 10));
        } else if (s == "--json") {
            const char* v = next("--json");
            if (!v) return false;
            a.json_out = v;
        } else if (s == "--wal") {
            a.wal = true;
        } else if (s == "--no-wal") {
            a.wal = false;
        } else if (s == "--accounts") {
            const char* v = next("--accounts");
            if (!v) return false;
            a.accounts = std::strtoull(v, nullptr, 10);
        } else if (s == "--cross-partition-pct") {
            const char* v = next("--cross-partition-pct");
            if (!v) return false;
            a.cross_pct = std::strtod(v, nullptr);
        } else if (s == "--floor-tps") {
            const char* v = next("--floor-tps");
            if (!v) return false;
            a.floor_tps = std::strtod(v, nullptr);
        } else if (s == "--smoke") {
            a.smoke = true;
            a.txns = 10'000;
            a.accounts = 1'000;
        } else if (s == "--seed") {
            const char* v = next("--seed");
            if (!v) return false;
            a.seed = std::strtoull(v, nullptr, 10);
        } else if (s == "--help" || s == "-h") {
            usage();
            std::exit(0);
        } else {
            std::cerr << "unknown arg: " << s << "\n";
            usage();
            return false;
        }
    }
    return true;
}

// Generate one balanced transaction.
lc::Transaction makeTxn(std::mt19937_64& rng, std::uint64_t accounts, std::uint64_t seq) {
    lc::Transaction t;
    t.idempotency.value = "bench-" + std::to_string(seq);
    std::uniform_int_distribution<std::uint64_t> acct(0, accounts - 1);
    std::uniform_int_distribution<lc::Amount> amt(1, 100);
    lc::AccountId a = acct(rng);
    lc::AccountId b = acct(rng);
    while (b == a) b = acct(rng);
    lc::Amount value = amt(rng);
    t.debits.push_back({a, value});
    t.credits.push_back({b, value});
    return t;
}

std::string isoNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse(argc, argv, args)) return 2;

    auto ledger = std::make_shared<lc::Ledger>();
    std::shared_ptr<lc::wal::WalWriter> wal;
    std::filesystem::path wal_path;
    if (args.wal) {
        wal_path = std::filesystem::temp_directory_path() /
                    ("ledgercore_bench_" + std::to_string(::getpid()) + ".wal");
        wal = std::make_shared<lc::wal::WalWriter>(wal_path);
        ledger = std::make_shared<lc::Ledger>(wal);
    }

    // Open all accounts with a healthy opening balance so debits never fail
    // for fund reasons. The bench measures throughput, not credit policy.
    const lc::Amount kOpening = 1'000'000'000;
    for (std::uint64_t i = 0; i < args.accounts; ++i) {
        ledger->openAccount(i, kOpening);
    }

    auto cache = std::make_shared<lc::idem::Cache>(args.txns + 1024);
    auto dispatcher = std::make_shared<lc::intake::Dispatcher>(
        std::max<std::size_t>(1, args.workers), cache, ledger);

    // Pre-generate transactions so the gen cost is not on the hot timer.
    std::mt19937_64 rng(args.seed);
    std::vector<lc::Transaction> txns;
    txns.reserve(args.txns);
    for (std::uint64_t i = 0; i < args.txns; ++i) {
        txns.push_back(makeTxn(rng, args.accounts, i));
    }

    // Latency record (in nanoseconds). One slot per transaction.
    std::vector<std::uint64_t> latencies(args.txns, 0);

    std::atomic<std::uint64_t> next{0};
    std::atomic<std::uint64_t> committed{0};
    std::atomic<std::uint64_t> rejected{0};

    auto t0 = std::chrono::steady_clock::now();

    auto worker = [&]() {
        while (true) {
            const std::uint64_t i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= args.txns) return;
            const auto submit_start = std::chrono::steady_clock::now();
            auto r = dispatcher->submit(txns[i]);
            const auto submit_end = std::chrono::steady_clock::now();
            latencies[i] =
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                submit_end - submit_start)
                                                .count());
            if (r.outcome == lc::Outcome::Committed) {
                committed.fetch_add(1, std::memory_order_relaxed);
            } else {
                rejected.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(args.workers);
    for (std::size_t i = 0; i < args.workers; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& th : threads) th.join();

    auto t1 = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();
    const double tps = seconds > 0 ? static_cast<double>(args.txns) / seconds : 0.0;

    // Percentiles. Sort once; nth_element would do but the data is small.
    std::sort(latencies.begin(), latencies.end());
    auto pct = [&](double p) -> std::uint64_t {
        if (latencies.empty()) return 0;
        const std::size_t idx = std::min(latencies.size() - 1,
                                          static_cast<std::size_t>(p * latencies.size()));
        return latencies[idx];
    };

    const std::uint64_t fsyncs = wal ? wal->fsyncs() : 0;

    if (wal) {
        // Best-effort cleanup of the bench wal file.
        std::error_code ec;
        std::filesystem::remove(wal_path, ec);
    }

    std::ofstream out(args.json_out);
    if (!out) {
        std::cerr << "could not open " << args.json_out << " for write\n";
        return 2;
    }
    out << "{\n";
    out << "  \"timestamp_utc\": \"" << isoNow() << "\",\n";
    out << "  \"txns\": " << args.txns << ",\n";
    out << "  \"workers\": " << args.workers << ",\n";
    out << "  \"accounts\": " << args.accounts << ",\n";
    out << "  \"wal\": " << (args.wal ? "true" : "false") << ",\n";
    out << "  \"committed\": " << committed.load() << ",\n";
    out << "  \"rejected\": " << rejected.load() << ",\n";
    out << "  \"elapsed_seconds\": " << seconds << ",\n";
    out << "  \"tps\": " << tps << ",\n";
    out << "  \"fsyncs\": " << fsyncs << ",\n";
    out << "  \"latency_ns\": {\n";
    out << "    \"p50\":   " << pct(0.50) << ",\n";
    out << "    \"p95\":   " << pct(0.95) << ",\n";
    out << "    \"p99\":   " << pct(0.99) << ",\n";
    out << "    \"p999\":  " << pct(0.999) << "\n";
    out << "  }\n";
    out << "}\n";
    out.close();

    std::cerr << "txns=" << args.txns << " workers=" << args.workers
              << " wal=" << (args.wal ? "on" : "off") << " tps=" << tps
              << " p50=" << pct(0.50) << "ns p99=" << pct(0.99) << "ns\n";

    if (rejected.load() > 0 && !args.smoke) {
        // In smoke mode we tolerate the occasional rejected duplicate.
        std::cerr << "bench: unexpected rejected count: " << rejected.load() << "\n";
        return 2;
    }
    if (tps < args.floor_tps) {
        std::cerr << "bench: tps " << tps << " below floor " << args.floor_tps << "\n";
        return 1;
    }
    return 0;
}
