#include <gtest/gtest.h>
#include <limits>

#include "core/account.h"

using namespace ledgercore;

TEST(Account, OpeningBalance) {
    Account a(42, 1000);
    EXPECT_EQ(a.id(), 42u);
    EXPECT_EQ(a.balance(), 1000);
}

TEST(Account, DebitSucceedsWhenSufficient) {
    Account a(1, 100);
    EXPECT_TRUE(a.tryDebit(40));
    EXPECT_EQ(a.balance(), 60);
}

TEST(Account, DebitFailsWhenInsufficient) {
    Account a(1, 50);
    EXPECT_FALSE(a.tryDebit(60));
    EXPECT_EQ(a.balance(), 50);
}

TEST(Account, DebitRejectsNonPositive) {
    Account a(1, 100);
    EXPECT_FALSE(a.tryDebit(0));
    EXPECT_FALSE(a.tryDebit(-5));
    EXPECT_EQ(a.balance(), 100);
}

TEST(Account, CreditSucceeds) {
    Account a(1, 100);
    EXPECT_TRUE(a.tryCredit(25));
    EXPECT_EQ(a.balance(), 125);
}

TEST(Account, CreditOverflowFails) {
    Account a(1, std::numeric_limits<Amount>::max() - 5);
    EXPECT_FALSE(a.tryCredit(100));
    EXPECT_EQ(a.balance(), std::numeric_limits<Amount>::max() - 5);
}
