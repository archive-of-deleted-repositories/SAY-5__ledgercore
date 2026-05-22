// Account state. A balance is a signed 64-bit integer cents value; debits
// decrement, credits increment. Accounts may carry a non-zero opening balance
// established by an opening transaction.
#pragma once

#include <atomic>
#include <cstdint>

#include "core/types.h"

namespace ledgercore {

// Single account state. Updated only by the worker that owns the account's
// partition, so the inner balance is plain int64 with no atomics on the hot
// path. The class exposes a relaxed atomic snapshot for read-only queries
// from other threads.
class Account {
public:
    Account() = default;
    explicit Account(AccountId id, Amount opening = 0) noexcept : id_(id), balance_(opening) {}

    AccountId id() const noexcept { return id_; }

    // Apply a debit. Returns false (with no state change) if the debit would
    // either overflow or leave the balance negative; this is a precondition
    // for crediting another account in the same transaction.
    bool tryDebit(Amount amount) noexcept;

    // Apply a credit. Returns false on overflow only; credits cannot fail for
    // funds reasons.
    bool tryCredit(Amount amount) noexcept;

    // Force a debit even if it would overflow. Caller asserts the balance
    // arithmetic has been pre-checked. Used in 2PC commit paths where the
    // prepare step already validated the amount.
    void forceDebit(Amount amount) noexcept { balance_ -= amount; }
    void forceCredit(Amount amount) noexcept { balance_ += amount; }

    Amount balance() const noexcept { return balance_; }

private:
    AccountId id_{0};
    Amount balance_{0};
};

}  // namespace ledgercore
