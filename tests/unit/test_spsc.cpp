#include <gtest/gtest.h>

#include <cstdint>

#include "intake/spsc.h"

using ledgercore::intake::SpscRing;

TEST(SpscRing, EmptyPopFails) {
    SpscRing<int, 4> ring;
    int out = 0;
    EXPECT_FALSE(ring.try_pop(out));
    EXPECT_EQ(ring.size_approx(), 0u);
}

TEST(SpscRing, PushPopRoundTrip) {
    SpscRing<int, 4> ring;
    EXPECT_TRUE(ring.try_push(7));
    EXPECT_EQ(ring.size_approx(), 1u);
    int out = -1;
    EXPECT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 7);
    EXPECT_EQ(ring.size_approx(), 0u);
}

TEST(SpscRing, FillsToCapacityAndRejects) {
    SpscRing<int, 4> ring;
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(ring.try_push(i)) << "push " << i << " should succeed";
    }
    EXPECT_FALSE(ring.try_push(99));
    EXPECT_EQ(ring.size_approx(), 4u);
}

TEST(SpscRing, FifoOrderingPreserved) {
    SpscRing<int, 8> ring;
    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(ring.try_push(i));
    }
    for (int i = 0; i < 6; ++i) {
        int out = -1;
        ASSERT_TRUE(ring.try_pop(out));
        EXPECT_EQ(out, i);
    }
    int unused = 0;
    EXPECT_FALSE(ring.try_pop(unused));
}

TEST(SpscRing, WrapsAroundCorrectly) {
    SpscRing<int, 4> ring;
    // Push 3, pop 3, then push 4 more so head wraps past the capacity boundary.
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(ring.try_push(i));
    int out;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(ring.try_pop(out));
        EXPECT_EQ(out, i);
    }
    for (int i = 100; i < 104; ++i) ASSERT_TRUE(ring.try_push(i));
    for (int i = 100; i < 104; ++i) {
        ASSERT_TRUE(ring.try_pop(out));
        EXPECT_EQ(out, i);
    }
}

TEST(SpscRing, MovesNonCopyableType) {
    struct OnlyMovable {
        std::unique_ptr<int> ptr;
        OnlyMovable() noexcept = default;
        explicit OnlyMovable(int v) : ptr(std::make_unique<int>(v)) {}
        OnlyMovable(OnlyMovable&&) noexcept = default;
        OnlyMovable& operator=(OnlyMovable&&) noexcept = default;
    };
    SpscRing<OnlyMovable, 4> ring;
    ASSERT_TRUE(ring.try_push(OnlyMovable{42}));
    OnlyMovable out;
    ASSERT_TRUE(ring.try_pop(out));
    ASSERT_TRUE(out.ptr);
    EXPECT_EQ(*out.ptr, 42);
}

TEST(SpscRing, CapacityIsCompileTime) {
    static_assert(SpscRing<int, 8>::capacity() == 8u);
    static_assert(SpscRing<int, 1024>::capacity() == 1024u);
}
