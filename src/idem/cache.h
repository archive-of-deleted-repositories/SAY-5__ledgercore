// Bounded LRU of seen idempotency keys.
//
// Lookups return the prior ApplyResult so a replay returns the original
// outcome without re-applying. Insertion past the bound evicts the
// least-recently-used entry. Restart durability is provided by the optional
// Postgres mirror layered on top; the in-memory cache is rebuilt on boot from
// the WAL replay path, falling back to the DB for entries older than the WAL
// retention window.
//
// Thread safety: a single mutex serializes lookups and inserts. The cache is
// a coarse-grained shared resource and the workload is read-heavy at the
// "key not seen yet" point in the hot path; future work could shard by hash
// bucket.
#pragma once

#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "core/ledger.h"
#include "core/transaction.h"
#include "core/types.h"

namespace ledgercore::idem {

struct CachedOutcome {
    Outcome outcome{Outcome::Rejected};
    RejectReason reason{RejectReason::None};
    TxnId txn_id{0};
};

struct HashEq {
    using is_transparent = void;
    std::size_t operator()(const IdempotencyKeyHash& h) const noexcept {
        std::size_t v = 0;
        for (std::size_t i = 0; i < h.size(); ++i) v ^= static_cast<std::size_t>(h[i]) << ((i & 7) * 8);
        return v;
    }
};

class Cache {
public:
    // Construct with a maximum entry count. Lookups past the bound evict LRU.
    explicit Cache(std::size_t capacity) : capacity_(capacity) {}

    // Look up by hash. Refreshes LRU position on hit.
    std::optional<CachedOutcome> lookup(const IdempotencyKeyHash& key);

    // Insert or overwrite. Evicts the LRU entry if at capacity.
    void insert(const IdempotencyKeyHash& key, CachedOutcome outcome);

    std::size_t size() const;
    std::size_t capacity() const noexcept { return capacity_; }

private:
    using ListIt = std::list<std::pair<IdempotencyKeyHash, CachedOutcome>>::iterator;

    mutable std::mutex mu_;
    std::size_t capacity_;
    std::list<std::pair<IdempotencyKeyHash, CachedOutcome>> lru_;
    std::unordered_map<IdempotencyKeyHash, ListIt, HashEq> index_;
};

}  // namespace ledgercore::idem
