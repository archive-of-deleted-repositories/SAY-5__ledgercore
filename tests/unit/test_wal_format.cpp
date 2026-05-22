#include <gtest/gtest.h>

#include "wal/format.h"

using namespace ledgercore;
using namespace ledgercore::wal;

namespace {

Record makeRecord() {
    Record r;
    r.txn_id = 42;
    r.key_hash = hashKey({"hello"});
    r.txn.idempotency.value = "hello";
    r.txn.debits = {{1, 50}, {2, 50}};
    r.txn.credits = {{3, 100}};
    r.txn.memo = "lunch";
    return r;
}

}  // namespace

TEST(WalFormat, RoundTrip) {
    auto r = makeRecord();
    auto bytes = encode(r);
    auto back = decode(bytes.data(), bytes.size());
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->txn_id, r.txn_id);
    EXPECT_EQ(back->key_hash, r.key_hash);
    EXPECT_EQ(back->txn.idempotency.value, "hello");
    ASSERT_EQ(back->txn.debits.size(), 2u);
    EXPECT_EQ(back->txn.debits[0].account, 1u);
    EXPECT_EQ(back->txn.debits[0].amount, 50);
    EXPECT_EQ(back->txn.debits[1].account, 2u);
    EXPECT_EQ(back->txn.debits[1].amount, 50);
    ASSERT_EQ(back->txn.credits.size(), 1u);
    EXPECT_EQ(back->txn.credits[0].account, 3u);
    EXPECT_EQ(back->txn.credits[0].amount, 100);
    EXPECT_EQ(back->txn.memo, "lunch");
}

TEST(WalFormat, CrcCatchesCorruption) {
    auto r = makeRecord();
    auto bytes = encode(r);
    // Flip a single bit somewhere in the payload (not in the CRC itself).
    ASSERT_GT(bytes.size(), kHeaderBytes + 4);
    bytes[kHeaderBytes + 1] ^= 0x01;
    EXPECT_FALSE(decode(bytes.data(), bytes.size()).has_value());
}

TEST(WalFormat, CrcCatchesTrailerCorruption) {
    auto r = makeRecord();
    auto bytes = encode(r);
    bytes.back() ^= 0x80;
    EXPECT_FALSE(decode(bytes.data(), bytes.size()).has_value());
}

TEST(WalFormat, RejectsTruncated) {
    auto r = makeRecord();
    auto bytes = encode(r);
    EXPECT_FALSE(decode(bytes.data(), bytes.size() - 1).has_value());
    EXPECT_FALSE(decode(bytes.data(), 3).has_value());
}

TEST(WalFormat, RejectsBogusKindByte) {
    auto r = makeRecord();
    auto bytes = encode(r);
    // The first payload byte is the kind byte of the first debit (0). Flip
    // it to an unknown kind.
    bytes[kHeaderBytes] = 0x7F;
    EXPECT_FALSE(decode(bytes.data(), bytes.size()).has_value());
}

TEST(WalFormatCrc, CastagnoliKnownVector) {
    // Standard CRC32C known vectors.
    const std::uint8_t empty[] = {0};
    EXPECT_EQ(crc32c(empty, 0), 0u);
    const char* msg = "123456789";
    EXPECT_EQ(crc32c(reinterpret_cast<const std::uint8_t*>(msg), 9), 0xE3069283u);
}
