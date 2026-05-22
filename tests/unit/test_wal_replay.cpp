#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "core/ledger.h"
#include "wal/replay.h"
#include "wal/writer.h"

using namespace ledgercore;

namespace {
std::filesystem::path tempWal(const char* tag) {
    auto p = std::filesystem::temp_directory_path() /
              (std::string("ledgercore_replay_") + tag + "_" +
               std::to_string(::getpid()) + ".wal");
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return p;
}
}  // namespace

TEST(WalReplay, EmptyFileNoOp) {
    auto p = tempWal("missing");
    Ledger l;
    auto s = wal::replay(p, l);
    EXPECT_EQ(s.records_seen, 0u);
    EXPECT_FALSE(s.tail_truncated);
}

TEST(WalReplay, ReplayRestoresState) {
    auto p = tempWal("happy");
    {
        wal::WalWriter w(p);
        Ledger writer_ledger(std::make_shared<wal::WalWriter>(p, true));
        // Open accounts on a fresh ledger that shares the WAL.
        writer_ledger.openAccount(1, 100);
        writer_ledger.openAccount(2, 0);
        Transaction t;
        t.idempotency.value = "k";
        t.debits = {{1, 30}};
        t.credits = {{2, 30}};
        auto r = writer_ledger.applyChecked(t);
        ASSERT_EQ(r.outcome, Outcome::Committed);
    }
    // Now boot a new ledger and replay.
    Ledger fresh;
    fresh.openAccount(1, 100);
    fresh.openAccount(2, 0);
    auto s = wal::replay(p, fresh);
    EXPECT_GE(s.records_seen, 1u);
    EXPECT_EQ(s.records_applied, s.records_seen);
    EXPECT_EQ(fresh.balanceOf(1), 70);
    EXPECT_EQ(fresh.balanceOf(2), 30);
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

TEST(WalReplay, TornTailDetected) {
    auto p = tempWal("torn");
    {
        wal::WalWriter w(p);
        Transaction t;
        t.idempotency.value = "k";
        t.debits = {{1, 10}};
        t.credits = {{2, 10}};
        ASSERT_TRUE(w.append(t, 1, hashKey(t.idempotency)));
    }
    // Append a few junk bytes to simulate a torn write.
    {
        std::ofstream f(p, std::ios::binary | std::ios::app);
        const char junk[] = {0x01, 0x02, 0x03};
        f.write(junk, sizeof(junk));
    }
    Ledger l;
    l.openAccount(1, 100);
    l.openAccount(2, 0);
    auto s = wal::replay(p, l);
    EXPECT_GE(s.records_seen, 1u);
    EXPECT_TRUE(s.tail_truncated);
    std::error_code ec;
    std::filesystem::remove(p, ec);
}
