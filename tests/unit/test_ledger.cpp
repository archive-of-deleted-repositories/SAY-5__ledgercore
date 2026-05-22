#include <gtest/gtest.h>

#include "core/ledger.h"
#include "core/transaction.h"

using namespace ledgercore;

namespace {

Transaction simple(AccountId from, AccountId to, Amount amount, std::string key = "k") {
    Transaction t;
    t.idempotency.value = std::move(key);
    t.debits = {{from, amount}};
    t.credits = {{to, amount}};
    return t;
}

}  // namespace

TEST(Ledger, OpenAndBalance) {
    Ledger l;
    EXPECT_TRUE(l.openAccount(1, 1000));
    EXPECT_TRUE(l.openAccount(2, 0));
    EXPECT_EQ(l.balanceOf(1), 1000);
    EXPECT_EQ(l.balanceOf(2), 0);
    EXPECT_FALSE(l.balanceOf(3).has_value());
}

TEST(Ledger, OpenSecondCallRequiresSameOpening) {
    Ledger l;
    EXPECT_TRUE(l.openAccount(1, 1000));
    EXPECT_TRUE(l.openAccount(1, 1000));
    EXPECT_FALSE(l.openAccount(1, 500));
}

TEST(Ledger, ApplyChecked_HappyPath) {
    Ledger l;
    l.openAccount(1, 100);
    l.openAccount(2, 0);
    auto r = l.applyChecked(simple(1, 2, 30));
    EXPECT_EQ(r.outcome, Outcome::Committed);
    EXPECT_EQ(l.balanceOf(1), 70);
    EXPECT_EQ(l.balanceOf(2), 30);
    EXPECT_GT(r.txn_id, 0u);
}

TEST(Ledger, RejectsInsufficientFunds) {
    Ledger l;
    l.openAccount(1, 10);
    l.openAccount(2, 0);
    auto r = l.applyChecked(simple(1, 2, 100));
    EXPECT_EQ(r.outcome, Outcome::Rejected);
    EXPECT_EQ(r.reason, RejectReason::InsufficientFunds);
    EXPECT_EQ(l.balanceOf(1), 10);
    EXPECT_EQ(l.balanceOf(2), 0);
}

TEST(Ledger, RejectsUnknownAccount) {
    Ledger l;
    l.openAccount(1, 100);
    auto r = l.applyChecked(simple(1, 99, 10));
    EXPECT_EQ(r.outcome, Outcome::Rejected);
    EXPECT_EQ(r.reason, RejectReason::UnknownAccount);
}

TEST(Ledger, RejectsUnbalancedAtIntake) {
    Ledger l;
    l.openAccount(1, 100);
    l.openAccount(2, 0);
    Transaction t;
    t.idempotency.value = "x";
    t.debits = {{1, 30}};
    t.credits = {{2, 29}};
    auto r = l.applyChecked(t);
    EXPECT_EQ(r.outcome, Outcome::Rejected);
    EXPECT_EQ(r.reason, RejectReason::Unbalanced);
}

TEST(Ledger, ManyTransactionsConserveTotal) {
    Ledger l;
    l.openAccount(1, 1000);
    l.openAccount(2, 0);
    l.openAccount(3, 0);
    Amount initial = *l.balanceOf(1) + *l.balanceOf(2) + *l.balanceOf(3);
    for (int i = 0; i < 50; ++i) {
        auto r = l.applyChecked(simple(1, 2, 10, "a" + std::to_string(i)));
        EXPECT_EQ(r.outcome, Outcome::Committed);
        r = l.applyChecked(simple(2, 3, 5, "b" + std::to_string(i)));
        EXPECT_EQ(r.outcome, Outcome::Committed);
    }
    Amount after = *l.balanceOf(1) + *l.balanceOf(2) + *l.balanceOf(3);
    EXPECT_EQ(initial, after);
}
