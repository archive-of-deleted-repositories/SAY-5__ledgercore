#include "workers/worker.h"

namespace ledgercore::workers {

Worker::Worker(std::size_t id, std::shared_ptr<intake::Dispatcher> dispatcher)
    : id_(id), dispatcher_(std::move(dispatcher)) {
    thread_ = std::thread(&Worker::run, this);
}

Worker::~Worker() { stop(); }

void Worker::stop() {
    if (stop_.exchange(true)) return;
    if (thread_.joinable()) thread_.join();
}

void Worker::run() {
    WorkItem item;
    while (!stop_.load(std::memory_order_acquire)) {
        if (ring_.try_pop(item)) {
            auto r = dispatcher_->submit(item.txn);
            if (item.done) item.done(r);
            processed_.fetch_add(1, std::memory_order_relaxed);
        } else {
            // Empty ring: yield briefly to the OS. A spin-wait here would
            // burn cores on idle. Real backpressure tuning is out of scope
            // for the benchmark which keeps the rings hot.
            std::this_thread::yield();
        }
    }
    // Drain on shutdown so in-flight items do not get dropped.
    while (ring_.try_pop(item)) {
        auto r = dispatcher_->submit(item.txn);
        if (item.done) item.done(r);
        processed_.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace ledgercore::workers
