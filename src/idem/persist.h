// Postgres mirror of the idempotency cache. Only compiled when
// LEDGERCORE_ENABLE_POSTGRES is on. The cache layer treats Postgres as a
// best-effort durable mirror: hot path writes go through the in-memory LRU
// and the persist layer is updated in the background.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "core/types.h"
#include "idem/cache.h"

namespace ledgercore::idem {

class Persist {
public:
    explicit Persist(const std::string& pg_url);
    ~Persist();

    // Idempotent schema setup. Creates the idempotency table and unique
    // index if absent.
    void ensureSchema();

    // Mirror an outcome into Postgres. Best-effort; failures are logged but
    // do not block the hot path.
    void put(const IdempotencyKeyHash& key, const CachedOutcome& outcome);

    // Reload the LRU on boot. Pulls the most recent N entries ordered by
    // last-seen time and stuffs them into the supplied cache.
    void warm(Cache& cache, std::size_t max_entries);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ledgercore::idem
