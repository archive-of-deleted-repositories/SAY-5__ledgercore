#include "core/transaction.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace ledgercore {

namespace {

// Safe signed addition. Returns nullopt on overflow.
std::optional<Amount> checkedAdd(Amount a, Amount b) noexcept {
    Amount result;
    if (__builtin_add_overflow(a, b, &result)) {
        return std::nullopt;
    }
    return result;
}

std::optional<Amount> sumLines(const std::vector<Line>& lines) noexcept {
    Amount total = 0;
    for (const auto& line : lines) {
        auto next = checkedAdd(total, line.amount);
        if (!next.has_value()) {
            return std::nullopt;
        }
        total = *next;
    }
    return total;
}

}  // namespace

std::optional<Amount> sumDebits(const Transaction& txn) noexcept {
    return sumLines(txn.debits);
}

std::optional<Amount> sumCredits(const Transaction& txn) noexcept {
    return sumLines(txn.credits);
}

ValidationResult validate(const Transaction& txn) noexcept {
    if (txn.debits.empty() || txn.credits.empty()) {
        return {RejectReason::EmptyLines};
    }
    for (const auto& line : txn.debits) {
        if (line.amount <= 0) {
            return {RejectReason::NonPositiveAmount};
        }
    }
    for (const auto& line : txn.credits) {
        if (line.amount <= 0) {
            return {RejectReason::NonPositiveAmount};
        }
    }
    // An account must not appear on both sides of the same txn. Otherwise a
    // client could "balance" by debiting and crediting the same account which
    // is semantically a no-op and almost always a client bug.
    std::unordered_set<AccountId> debitAccounts;
    debitAccounts.reserve(txn.debits.size());
    for (const auto& line : txn.debits) {
        debitAccounts.insert(line.account);
    }
    for (const auto& line : txn.credits) {
        if (debitAccounts.contains(line.account)) {
            return {RejectReason::DuplicateAccount};
        }
    }
    auto d = sumDebits(txn);
    auto c = sumCredits(txn);
    if (!d.has_value() || !c.has_value()) {
        return {RejectReason::Overflow};
    }
    if (*d != *c) {
        return {RejectReason::Unbalanced};
    }
    return {RejectReason::None};
}

// FNV-1a 64.
static std::uint64_t fnv1a(const void* data, std::size_t n, std::uint64_t seed) noexcept {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t h = seed;
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= kPrime;
    }
    return h;
}

IdempotencyKeyHash hashKey(const IdempotencyKey& key) noexcept {
    // Two-lane FNV with different seeds gives a 128 bit digest. Adequate for
    // the cache; collisions still fall back to string compare on the entry.
    std::uint64_t lo = fnv1a(key.value.data(), key.value.size(), 14695981039346656037ULL);
    std::uint64_t hi = fnv1a(key.value.data(), key.value.size(), 0xcbf29ce484222325ULL ^ key.value.size());
    IdempotencyKeyHash out{};
    std::memcpy(out.data(), &lo, 8);
    std::memcpy(out.data() + 8, &hi, 8);
    return out;
}

}  // namespace ledgercore
