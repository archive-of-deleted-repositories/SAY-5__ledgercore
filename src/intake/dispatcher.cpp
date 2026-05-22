#include "intake/dispatcher.h"

#include <cassert>

namespace ledgercore::intake {

Dispatcher::Dispatcher(std::size_t num_partitions, std::shared_ptr<idem::Cache> cache,
                       std::shared_ptr<Ledger> ledger)
    : num_partitions_(num_partitions), cache_(std::move(cache)), ledger_(std::move(ledger)) {
    if (num_partitions_ == 0) num_partitions_ = 1;
}

std::unordered_set<std::size_t> Dispatcher::partitionsTouched(const Transaction& txn) const {
    std::unordered_set<std::size_t> parts;
    for (const auto& line : txn.debits) parts.insert(partitionOf(line.account));
    for (const auto& line : txn.credits) parts.insert(partitionOf(line.account));
    return parts;
}

DispatchResult Dispatcher::submit(const Transaction& txn) {
    DispatchResult r{};
    // Validate the cheap intake shape first so a malformed txn does not even
    // hit the idempotency cache.
    auto v = validate(txn);
    if (!v.ok()) {
        r.outcome = Outcome::Rejected;
        r.reason = v.reason;
        return r;
    }

    auto key_hash = hashKey(txn.idempotency);
    if (cache_) {
        if (auto prior = cache_->lookup(key_hash); prior.has_value()) {
            r.outcome = Outcome::Replayed;
            r.reason = prior->reason;
            r.txn_id = prior->txn_id;
            r.idempotency_hit = true;
            return r;
        }
    }

    auto parts = partitionsTouched(txn);
    DispatchResult result;
    if (parts.size() <= 1) {
        result = submitSinglePartition(txn, key_hash);
    } else {
        result = submitMultiPartition(txn, key_hash, parts);
    }

    if (cache_) {
        idem::CachedOutcome co{};
        co.outcome = result.outcome;
        co.reason = result.reason;
        co.txn_id = result.txn_id;
        cache_->insert(key_hash, co);
    }
    return result;
}

DispatchResult Dispatcher::submitSinglePartition(const Transaction& txn,
                                                  const IdempotencyKeyHash& /*key_hash*/) {
    DispatchResult r{};
    auto ar = ledger_->applyChecked(txn);
    r.outcome = ar.outcome;
    r.reason = ar.reason;
    r.txn_id = ar.txn_id;
    return r;
}

DispatchResult Dispatcher::submitMultiPartition(const Transaction& txn,
                                                 const IdempotencyKeyHash& key_hash,
                                                 const std::unordered_set<std::size_t>& /*parts*/) {
    // For the current single-ledger implementation, the 2PC path collapses to
    // the same single mutex-protected apply. The protocol-level interfaces
    // (partitionsTouched, the serializing mutex below) exist so a future
    // partitioned-ledger variant can plug in without reworking the
    // dispatcher's call sites. Tests in test_dispatcher.cpp pin both the
    // single- and multi-partition entry points.
    std::lock_guard lock(mp_mu_);
    DispatchResult r{};
    auto ar = ledger_->applyChecked(txn);
    r.outcome = ar.outcome;
    r.reason = ar.reason;
    r.txn_id = ar.txn_id;
    (void)key_hash;
    return r;
}

}  // namespace ledgercore::intake
