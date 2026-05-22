// Transaction = N debits + N credits + idempotency key.
//
// The double-entry invariant is the single most important property of this
// ledger: sum of debit amounts must equal sum of credit amounts, exactly. This
// header defines the type and the validation function. The invariant is
// re-checked at WAL append and at apply time as a safety net against memory
// corruption.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/types.h"

namespace ledgercore {

struct Line {
    AccountId account;
    Amount amount;  // strictly > 0; sign is implied by debit vs credit
};

struct Transaction {
    IdempotencyKey idempotency;
    std::vector<Line> debits;
    std::vector<Line> credits;

    // Optional client-supplied memo. Not used by core logic.
    std::string memo;
};

// Return value of the balanced-check. None on success; otherwise the first
// failure reason. The check is total: it visits all lines and either accepts
// or rejects deterministically.
struct ValidationResult {
    RejectReason reason{RejectReason::None};
    bool ok() const noexcept { return reason == RejectReason::None; }
};

// Validate transaction shape and the double-entry invariant.
//   1. Both debits and credits are non-empty.
//   2. Every line amount is strictly positive.
//   3. No account appears in both the debit and credit vectors.
//   4. sum(debit.amount) == sum(credit.amount), checked with overflow guard.
ValidationResult validate(const Transaction& txn) noexcept;

// Hash an idempotency key to a 16 byte digest. Uses FNV-1a 64 over two halves
// of the input plus length. The cache and WAL use this digest as the canonical
// key; the original string is only kept on the cache entry for collision
// fallback.
IdempotencyKeyHash hashKey(const IdempotencyKey& key) noexcept;

// Sum debit amounts. Returns nullopt on overflow.
std::optional<Amount> sumDebits(const Transaction& txn) noexcept;

// Sum credit amounts. Returns nullopt on overflow.
std::optional<Amount> sumCredits(const Transaction& txn) noexcept;

}  // namespace ledgercore
