#include <gtest/gtest.h>

#include "core/transaction.h"
#include "idem/cache.h"

using namespace ledgercore;
using namespace ledgercore::idem;

TEST(IdemCache, MissThenHit) {
    Cache c(4);
    auto h = hashKey({"k"});
    EXPECT_FALSE(c.lookup(h).has_value());
    CachedOutcome co{Outcome::Committed, RejectReason::None, 42};
    c.insert(h, co);
    auto got = c.lookup(h);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->outcome, Outcome::Committed);
    EXPECT_EQ(got->txn_id, 42u);
}

TEST(IdemCache, OverwriteUpdatesValue) {
    Cache c(4);
    auto h = hashKey({"k"});
    c.insert(h, {Outcome::Committed, RejectReason::None, 1});
    c.insert(h, {Outcome::Rejected, RejectReason::Unbalanced, 2});
    auto got = c.lookup(h);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->outcome, Outcome::Rejected);
    EXPECT_EQ(got->reason, RejectReason::Unbalanced);
}

TEST(IdemCache, EvictsLeastRecentlyUsed) {
    Cache c(3);
    auto k = [](const char* s) { return hashKey({s}); };
    c.insert(k("a"), {Outcome::Committed, {}, 1});
    c.insert(k("b"), {Outcome::Committed, {}, 2});
    c.insert(k("c"), {Outcome::Committed, {}, 3});
    // Touch 'a' so it stays hot.
    EXPECT_TRUE(c.lookup(k("a")).has_value());
    c.insert(k("d"), {Outcome::Committed, {}, 4});
    // 'b' is now the oldest; should be evicted.
    EXPECT_FALSE(c.lookup(k("b")).has_value());
    EXPECT_TRUE(c.lookup(k("a")).has_value());
    EXPECT_TRUE(c.lookup(k("c")).has_value());
    EXPECT_TRUE(c.lookup(k("d")).has_value());
    EXPECT_EQ(c.size(), 3u);
}

TEST(IdemCache, ZeroCapacityIsNoOp) {
    Cache c(0);
    c.insert(hashKey({"x"}), {Outcome::Committed, {}, 1});
    EXPECT_EQ(c.size(), 0u);
}
