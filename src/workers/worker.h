// Worker thread. Owns one SPSC ring for inbound transactions, drives the
// dispatcher for each pop. The "pool" is just N of these wired up at startup
// time; the dispatcher routes by hash-partition over account IDs so per-
// account ordering is preserved.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>

#include "core/ledger.h"
#include "core/transaction.h"
#include "intake/dispatcher.h"
#include "intake/spsc.h"

namespace ledgercore::workers {

// Default ring size: 65536 slots. Producers spin briefly on full rings; in
// the bench harness the producer count and rate are chosen so the rings do
// not saturate. In production the gRPC handler should treat a full ring as
// backpressure and return UNAVAILABLE.
inline constexpr std::size_t kDefaultRingSize = 1 << 16;

struct WorkItem {
    Transaction txn;
    // The promise pattern is heavy; instead we use a small callback the
    // caller supplies. The dispatcher invokes this once with the result.
    std::function<void(intake::DispatchResult)> done;
};

class Worker {
public:
    Worker(std::size_t id, std::shared_ptr<intake::Dispatcher> dispatcher);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    // Producer-side push. Returns false if the ring is full.
    bool try_submit(WorkItem item) noexcept { return ring_.try_push(std::move(item)); }

    // Stop the worker loop and join. Idempotent.
    void stop();

    std::size_t id() const noexcept { return id_; }
    std::uint64_t processed() const noexcept { return processed_.load(std::memory_order_relaxed); }

private:
    void run();

    std::size_t id_;
    std::shared_ptr<intake::Dispatcher> dispatcher_;
    intake::SpscRing<WorkItem, kDefaultRingSize> ring_;
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> processed_{0};
    std::thread thread_;
};

}  // namespace ledgercore::workers
