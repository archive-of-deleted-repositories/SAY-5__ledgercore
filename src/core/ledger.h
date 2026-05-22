// In-memory ledger. The only place state is mutated. All callers go through
// applyChecked() which re-validates and (if WAL is configured) durably logs
// the transaction before any account is mutated.
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

#include "core/account.h"
#include "core/transaction.h"

namespace ledgercore {

namespace wal {
class WalWriter;  // forward; full type lives in wal/writer.h
}  // namespace wal
using WalWriter = wal::WalWriter;

struct ApplyResult {
    Outcome outcome{Outcome::Rejected};
    RejectReason reason{RejectReason::None};
    TxnId txn_id{0};
    // Hash of the idempotency key; useful for callers that want to dedupe
    // before re-submission.
    IdempotencyKeyHash key_hash{};
};

// The ledger owns the account map and an optional WAL writer. It is thread
// safe at the public API; internally it uses a single coarse mutex around
// apply() for clarity in this single-partition path. The dispatcher class
// uses per-partition Ledger instances to scale across cores.
class Ledger {
public:
    Ledger() = default;
    explicit Ledger(std::shared_ptr<WalWriter> wal) noexcept : wal_(std::move(wal)) {}

    // Register an account with an opening balance. Idempotent on the (id,
    // opening) pair; rejects a second open with a different opening.
    bool openAccount(AccountId id, Amount opening = 0);

    // Look up balance. Returns nullopt if the account is not open.
    std::optional<Amount> balanceOf(AccountId id) const;

    // Apply a transaction. The flow is:
    //   1. validate() -- reject typed errors
    //   2. Pre-check every debit against current balances (atomic w.r.t the
    //      ledger mutex)
    //   3. WAL append + fsync if a writer is attached
    //   4. Mutate accounts; the apply step cannot fail because step 2 already
    //      proved each debit has the funds
    ApplyResult applyChecked(const Transaction& txn);

    // Replay path: apply a transaction read back from the WAL. Skips the WAL
    // re-write and assumes the txn already balanced when written. Still runs
    // the validation as a safety net since the WAL CRC only protects against
    // bit rot, not against logic bugs in older binaries.
    ApplyResult applyFromWal(const Transaction& txn, TxnId txn_id);

    // For tests.
    std::size_t numAccounts() const;

private:
    mutable std::shared_mutex mu_;
    std::unordered_map<AccountId, Account> accounts_;
    std::shared_ptr<WalWriter> wal_;
    TxnId next_id_{1};

    // Apply with caller-supplied txn id. Caller holds mu_ exclusively.
    void applyMutationsLocked(const Transaction& txn);

    // Verify that, given the current balances, every debit can clear. Caller
    // holds mu_.
    RejectReason preflightLocked(const Transaction& txn) const;
};

}  // namespace ledgercore
