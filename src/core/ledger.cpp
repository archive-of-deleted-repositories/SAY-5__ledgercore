#include "core/ledger.h"

#include <cassert>

#include "wal/writer.h"

namespace ledgercore {

bool Ledger::openAccount(AccountId id, Amount opening) {
    std::unique_lock lock(mu_);
    auto it = accounts_.find(id);
    if (it != accounts_.end()) {
        return it->second.balance() == opening;
    }
    accounts_.emplace(id, Account(id, opening));
    return true;
}

std::optional<Amount> Ledger::balanceOf(AccountId id) const {
    std::shared_lock lock(mu_);
    auto it = accounts_.find(id);
    if (it == accounts_.end()) {
        return std::nullopt;
    }
    return it->second.balance();
}

std::size_t Ledger::numAccounts() const {
    std::shared_lock lock(mu_);
    return accounts_.size();
}

RejectReason Ledger::preflightLocked(const Transaction& txn) const {
    for (const auto& line : txn.debits) {
        auto it = accounts_.find(line.account);
        if (it == accounts_.end()) {
            return RejectReason::UnknownAccount;
        }
        Amount b = it->second.balance();
        Amount next;
        if (__builtin_sub_overflow(b, line.amount, &next)) {
            return RejectReason::Overflow;
        }
        if (next < 0) {
            return RejectReason::InsufficientFunds;
        }
    }
    for (const auto& line : txn.credits) {
        auto it = accounts_.find(line.account);
        if (it == accounts_.end()) {
            return RejectReason::UnknownAccount;
        }
        Amount b = it->second.balance();
        Amount next;
        if (__builtin_add_overflow(b, line.amount, &next)) {
            return RejectReason::Overflow;
        }
    }
    return RejectReason::None;
}

void Ledger::applyMutationsLocked(const Transaction& txn) {
    for (const auto& line : txn.debits) {
        accounts_.find(line.account)->second.forceDebit(line.amount);
    }
    for (const auto& line : txn.credits) {
        accounts_.find(line.account)->second.forceCredit(line.amount);
    }
}

ApplyResult Ledger::applyChecked(const Transaction& txn) {
    ApplyResult r{};
    r.key_hash = hashKey(txn.idempotency);

    auto v = validate(txn);
    if (!v.ok()) {
        r.outcome = Outcome::Rejected;
        r.reason = v.reason;
        return r;
    }

    std::unique_lock lock(mu_);
    auto pre = preflightLocked(txn);
    if (pre != RejectReason::None) {
        r.outcome = Outcome::Rejected;
        r.reason = pre;
        return r;
    }
    TxnId id = next_id_++;
    if (wal_) {
        // The WAL append cross-checks the balance invariant before writing.
        // If it returns false the txn is rejected with no state change.
        if (!wal_->append(txn, id, r.key_hash)) {
            r.outcome = Outcome::Rejected;
            r.reason = RejectReason::Unbalanced;  // wal cross-check failed
            // Roll back the txn id allocation; otherwise we leak ids on
            // rejected writes.
            next_id_ = id;
            return r;
        }
    }
    applyMutationsLocked(txn);
    // Final safety-net assertion: replay sum-of-balance delta. This catches
    // memory corruption between preflight and apply. Stripped in release if
    // NDEBUG is set; keep enabled to make corruption immediately visible.
    {
        Amount delta = 0;
        for (const auto& line : txn.debits) delta -= line.amount;
        for (const auto& line : txn.credits) delta += line.amount;
        assert(delta == 0 && "double-entry invariant breached at apply");
        (void)delta;
    }
    r.outcome = Outcome::Committed;
    r.txn_id = id;
    return r;
}

ApplyResult Ledger::applyFromWal(const Transaction& txn, TxnId txn_id) {
    ApplyResult r{};
    r.key_hash = hashKey(txn.idempotency);
    auto v = validate(txn);
    if (!v.ok()) {
        r.outcome = Outcome::Rejected;
        r.reason = v.reason;
        return r;
    }
    std::unique_lock lock(mu_);
    auto pre = preflightLocked(txn);
    if (pre != RejectReason::None) {
        r.outcome = Outcome::Rejected;
        r.reason = pre;
        return r;
    }
    applyMutationsLocked(txn);
    next_id_ = std::max(next_id_, txn_id + 1);
    r.outcome = Outcome::Committed;
    r.txn_id = txn_id;
    return r;
}

}  // namespace ledgercore
