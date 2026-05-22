#include <gtest/gtest.h>

#include <random>

#include "core/transaction.h"

using namespace ledgercore;

namespace {

Transaction makeBalanced(std::initializer_list<Line> dr, std::initializer_list<Line> cr,
                         std::string key = "k") {
    Transaction t;
    t.idempotency.value = std::move(key);
    t.debits.assign(dr);
    t.credits.assign(cr);
    return t;
}

}  // namespace

TEST(TransactionValidate, AcceptsBalanced) {
    auto t = makeBalanced({{1, 100}}, {{2, 100}});
    auto v = validate(t);
    EXPECT_TRUE(v.ok());
}

TEST(TransactionValidate, RejectsEmptyDebits) {
    auto t = makeBalanced({}, {{2, 100}});
    EXPECT_EQ(validate(t).reason, RejectReason::EmptyLines);
}

TEST(TransactionValidate, RejectsEmptyCredits) {
    auto t = makeBalanced({{1, 100}}, {});
    EXPECT_EQ(validate(t).reason, RejectReason::EmptyLines);
}

TEST(TransactionValidate, RejectsZeroAmount) {
    auto t = makeBalanced({{1, 0}}, {{2, 0}});
    EXPECT_EQ(validate(t).reason, RejectReason::NonPositiveAmount);
}

TEST(TransactionValidate, RejectsNegativeAmount) {
    Transaction t;
    t.debits = {{1, -1}};
    t.credits = {{2, -1}};
    EXPECT_EQ(validate(t).reason, RejectReason::NonPositiveAmount);
}

TEST(TransactionValidate, RejectsUnbalanced) {
    auto t = makeBalanced({{1, 100}}, {{2, 99}});
    EXPECT_EQ(validate(t).reason, RejectReason::Unbalanced);
}

TEST(TransactionValidate, RejectsDuplicateAccount) {
    auto t = makeBalanced({{1, 100}}, {{1, 100}});
    EXPECT_EQ(validate(t).reason, RejectReason::DuplicateAccount);
}

TEST(TransactionValidate, RejectsDebitOverflow) {
    Transaction t;
    t.debits = {{1, std::numeric_limits<Amount>::max()}, {2, 1}};
    t.credits = {{3, 100}};
    EXPECT_EQ(validate(t).reason, RejectReason::Overflow);
}

TEST(TransactionValidate, MultiLineBalances) {
    auto t = makeBalanced({{1, 30}, {2, 70}}, {{3, 40}, {4, 60}});
    EXPECT_TRUE(validate(t).ok());
}

// Property test: any deterministically-generated balanced txn validates.
TEST(TransactionValidate, PropertyBalancedAlwaysAccepts) {
    std::mt19937_64 rng(123);
    std::uniform_int_distribution<int> n(1, 6);
    std::uniform_int_distribution<Amount> amt(1, 1'000'000);
    for (int trial = 0; trial < 500; ++trial) {
        const int nd = n(rng);
        const int nc = n(rng);
        Transaction t;
        t.idempotency.value = "k" + std::to_string(trial);
        Amount sum = 0;
        // Build N-1 debits with random amounts.
        for (int i = 0; i < nd - 1; ++i) {
            const Amount a = amt(rng);
            t.debits.push_back({static_cast<AccountId>(100 + i), a});
            sum += a;
        }
        // Last debit closes the sum to some target = sum + final
        const Amount final_debit = amt(rng);
        t.debits.push_back({static_cast<AccountId>(199), final_debit});
        const Amount target = sum + final_debit;
        // Now spread `target` across nc credits.
        Amount remaining = target;
        for (int i = 0; i < nc - 1; ++i) {
            // Leave at least 1 for the last credit
            const Amount slice = (remaining > 1) ? (1 + (amt(rng) % (remaining - 1))) : 1;
            t.credits.push_back({static_cast<AccountId>(200 + i), slice});
            remaining -= slice;
            if (remaining <= 0) {
                remaining = 1;
                break;
            }
        }
        t.credits.push_back({static_cast<AccountId>(299), remaining});
        // Recompute sums and assert.
        auto d = sumDebits(t).value();
        auto c = sumCredits(t).value();
        ASSERT_EQ(d, c) << "trial=" << trial;
        ASSERT_TRUE(validate(t).ok()) << "trial=" << trial;
    }
}

TEST(IdempotencyHash, DeterministicAndDistinguishes) {
    auto h1 = hashKey({"alpha"});
    auto h2 = hashKey({"alpha"});
    auto h3 = hashKey({"beta"});
    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, h3);
}
