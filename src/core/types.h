// Core scalar types. Money is always integer cents (signed 64-bit). No floats,
// ever; double-entry accounting requires bit-exact equality of debits and
// credits.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ledgercore {

// Account identifier. 64-bit so the ledger can host O(billions) of accounts.
using AccountId = std::uint64_t;

// Transaction identifier assigned by the ledger after intake acceptance.
using TxnId = std::uint64_t;

// Money is signed integer cents. The maximum representable value is
// approximately +/- 92 quadrillion cents which is far beyond any plausible
// single-account balance.
using Amount = std::int64_t;

// Client supplied idempotency key. Free-form short bytes; the cache hashes it.
struct IdempotencyKey {
    std::string value;

    bool operator==(const IdempotencyKey& other) const = default;
};

// 16 byte hash used inside WAL records and for cache keying.
using IdempotencyKeyHash = std::array<std::uint8_t, 16>;

// Outcome categories returned by the ledger.
enum class Outcome : std::uint8_t {
    Committed = 0,
    Rejected = 1,    // intake rejected the txn (bad shape, unbalanced, etc.)
    Replayed = 2,    // idempotency hit; the original outcome is reported
};

// Reason a transaction was rejected. The shape error codes are stable and form
// part of the gRPC contract.
enum class RejectReason : std::uint8_t {
    None = 0,
    Unbalanced = 1,         // sum(debits) != sum(credits)
    EmptyLines = 2,         // no debit or no credit line
    NonPositiveAmount = 3,  // a line had amount <= 0
    DuplicateAccount = 4,   // same account appears as both debit and credit
    UnknownAccount = 5,     // ledger does not know the account
    InsufficientFunds = 6,  // account balance would go negative for a debit
    Overflow = 7,           // amount sum would overflow Amount
};

inline std::string_view rejectReasonName(RejectReason r) noexcept {
    switch (r) {
        case RejectReason::None:
            return "none";
        case RejectReason::Unbalanced:
            return "unbalanced";
        case RejectReason::EmptyLines:
            return "empty_lines";
        case RejectReason::NonPositiveAmount:
            return "non_positive_amount";
        case RejectReason::DuplicateAccount:
            return "duplicate_account";
        case RejectReason::UnknownAccount:
            return "unknown_account";
        case RejectReason::InsufficientFunds:
            return "insufficient_funds";
        case RejectReason::Overflow:
            return "overflow";
    }
    return "unknown";
}

}  // namespace ledgercore
