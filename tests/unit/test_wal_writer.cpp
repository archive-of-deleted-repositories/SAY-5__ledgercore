#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>

#include "core/transaction.h"
#include "wal/format.h"
#include "wal/writer.h"

using namespace ledgercore;

namespace {
std::filesystem::path tempWalPath(const char* tag) {
    auto p = std::filesystem::temp_directory_path() /
              (std::string("ledgercore_test_") + tag + "_" +
               std::to_string(::getpid()) + ".wal");
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return p;
}
}  // namespace

TEST(WalWriter, AppendsAndCountsBytes) {
    auto path = tempWalPath("append");
    {
        wal::WalWriter w(path);
        Transaction t;
        t.idempotency.value = "k1";
        t.debits = {{1, 10}};
        t.credits = {{2, 10}};
        ASSERT_TRUE(w.append(t, 1, hashKey(t.idempotency)));
        ASSERT_TRUE(w.append(t, 2, hashKey({"k2"})));
        EXPECT_GT(w.bytesWritten(), 0u);
        EXPECT_EQ(w.fsyncs(), 2u);
    }
    EXPECT_TRUE(std::filesystem::exists(path));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(WalWriter, RejectsUnbalanced) {
    auto path = tempWalPath("reject");
    wal::WalWriter w(path);
    Transaction t;
    t.idempotency.value = "bad";
    t.debits = {{1, 10}};
    t.credits = {{2, 9}};
    EXPECT_FALSE(w.append(t, 1, hashKey(t.idempotency)));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(WalWriter, FsyncToggleableForBench) {
    auto path = tempWalPath("nofsync");
    wal::WalWriter w(path, /*fsync_on_append=*/false);
    Transaction t;
    t.idempotency.value = "k";
    t.debits = {{1, 10}};
    t.credits = {{2, 10}};
    ASSERT_TRUE(w.append(t, 1, hashKey(t.idempotency)));
    EXPECT_EQ(w.fsyncs(), 0u);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
