// Dispatcher: routes a transaction to one or more workers.
//
// Each worker owns a partition of account IDs (hash partitioning over the
// account id, modulo num_workers). A transaction whose debits and credits all
// fall in the same partition is single-partition: just enqueue on that
// worker's ring.
//
// A transaction whose lines span multiple partitions is multi-partition. The
// dispatcher coordinates a two-phase commit across the involved workers:
//
//   prepare:
//     each worker pre-checks its slice of lines (debits have funds; credits
//     do not overflow) and parks the txn in a "prepared" map, holding the
//     accounts but not yet mutating the balances
//   commit:
//     if all workers reported ok, the dispatcher tells them to commit and
//     each applies its slice atomically; final invariant assertion runs at
//     the dispatcher once the commit acks return
//   abort:
//     any prepare failure aborts the whole txn; workers release their
//     reservations and the dispatcher returns the failure reason
//
// Single-partition txns skip the 2PC overhead entirely. The benchmark drives
// both paths so the cross-partition cost shows up honestly.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/ledger.h"
#include "core/transaction.h"
#include "core/types.h"
#include "idem/cache.h"

namespace ledgercore::intake {

// Forward declaration of Worker. The dispatcher only needs a small interface
// surface on Worker, so we forward-declare and use through a function-pointer
// table for testability.
class Worker;

struct DispatchResult {
    Outcome outcome{Outcome::Rejected};
    RejectReason reason{RejectReason::None};
    TxnId txn_id{0};
    bool idempotency_hit{false};
};

class Dispatcher {
public:
    // Construct with N worker partitions and an idempotency cache. The
    // dispatcher does not own workers; the caller wires the worker pool.
    Dispatcher(std::size_t num_partitions, std::shared_ptr<idem::Cache> cache,
               std::shared_ptr<Ledger> ledger);

    // Submit a transaction. Synchronous; returns once the txn has been
    // committed, rejected, or recognized as a replay. Single-partition
    // submissions bypass the 2PC path.
    DispatchResult submit(const Transaction& txn);

    // Compute the partition for an account id. Public for tests.
    std::size_t partitionOf(AccountId acct) const noexcept {
        return std::hash<AccountId>{}(acct) % num_partitions_;
    }

    // The set of partitions touched by a transaction.
    std::unordered_set<std::size_t> partitionsTouched(const Transaction& txn) const;

private:
    DispatchResult submitSinglePartition(const Transaction& txn,
                                          const IdempotencyKeyHash& key_hash);
    DispatchResult submitMultiPartition(const Transaction& txn,
                                         const IdempotencyKeyHash& key_hash,
                                         const std::unordered_set<std::size_t>& parts);

    std::size_t num_partitions_;
    std::shared_ptr<idem::Cache> cache_;
    std::shared_ptr<Ledger> ledger_;
    std::mutex mp_mu_;  // serializes the 2PC path
};

}  // namespace ledgercore::intake
