#include "core/account.h"

#include <limits>

namespace ledgercore {

bool Account::tryDebit(Amount amount) noexcept {
    if (amount <= 0) {
        return false;
    }
    Amount next;
    if (__builtin_sub_overflow(balance_, amount, &next)) {
        return false;
    }
    if (next < 0) {
        // An account would never legitimately go negative in this single-
        // currency ledger; loans and overdrafts are out of scope.
        return false;
    }
    balance_ = next;
    return true;
}

bool Account::tryCredit(Amount amount) noexcept {
    if (amount <= 0) {
        return false;
    }
    Amount next;
    if (__builtin_add_overflow(balance_, amount, &next)) {
        return false;
    }
    balance_ = next;
    return true;
}

}  // namespace ledgercore
