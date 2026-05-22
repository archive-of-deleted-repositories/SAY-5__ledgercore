// ledgercored: serves the gRPC LedgerService. Wires up the in-memory ledger,
// the idempotency cache, optional WAL writer, and the gRPC server. All
// configuration is via environment variables so the container image stays
// stateless.
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "core/ledger.h"
#include "idem/cache.h"
#include "rpc/server.h"
#include "wal/replay.h"
#include "wal/writer.h"

namespace lc = ledgercore;

namespace {
std::string envOr(const char* k, const std::string& def) {
    const char* v = std::getenv(k);
    return v ? std::string(v) : def;
}
}

int main() {
    const auto listen = envOr("LEDGERCORE_LISTEN", "0.0.0.0:50051");
    const auto wal_path = envOr("LEDGERCORE_WAL_PATH", "/tmp/ledgercore.wal");
    const std::size_t idem_cap =
        std::strtoull(envOr("LEDGERCORE_IDEM_CAPACITY", "1048576").c_str(), nullptr, 10);

    auto wal = std::make_shared<lc::wal::WalWriter>(wal_path);
    auto ledger = std::make_shared<lc::Ledger>(wal);
    // Boot replay if a WAL file already exists from a prior run.
    auto stats = lc::wal::replay(wal_path, *ledger);
    std::cerr << "replay: applied " << stats.records_applied
              << " of " << stats.records_seen
              << " (tail_truncated=" << stats.tail_truncated << ")\n";
    auto cache = std::make_shared<lc::idem::Cache>(idem_cap);

    lc::rpc::Server server(listen, ledger, cache);
    std::cerr << "ledgercored listening on " << listen << "\n";
    server.run();
    return 0;
}
