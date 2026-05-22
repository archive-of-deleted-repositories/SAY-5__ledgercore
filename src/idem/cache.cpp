#include "idem/cache.h"

namespace ledgercore::idem {

std::optional<CachedOutcome> Cache::lookup(const IdempotencyKeyHash& key) {
    std::lock_guard lock(mu_);
    auto it = index_.find(key);
    if (it == index_.end()) return std::nullopt;
    // Move to front of LRU.
    lru_.splice(lru_.begin(), lru_, it->second);
    return it->second->second;
}

void Cache::insert(const IdempotencyKeyHash& key, CachedOutcome outcome) {
    std::lock_guard lock(mu_);
    if (capacity_ == 0) {
        return;
    }
    auto it = index_.find(key);
    if (it != index_.end()) {
        it->second->second = outcome;
        lru_.splice(lru_.begin(), lru_, it->second);
        return;
    }
    if (lru_.size() >= capacity_) {
        auto& victim = lru_.back();
        index_.erase(victim.first);
        lru_.pop_back();
    }
    lru_.emplace_front(key, outcome);
    index_[key] = lru_.begin();
}

std::size_t Cache::size() const {
    std::lock_guard lock(mu_);
    return lru_.size();
}

}  // namespace ledgercore::idem
