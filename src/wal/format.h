// Write-ahead log record format.
//
//   offset  size  field
//   0       4     length (uint32 LE)             -- bytes of payload + trailer
//   4       8     txn_id (uint64 LE)             -- monotonic ledger id
//   12      16    idempotency_key_hash (16 B)    -- two-lane FNV hash
//   28      L     payload (TLV lines)
//   28+L    4     crc32c (uint32 LE)             -- over [0 .. 28+L)
//
// The payload is a tiny self-describing TLV scheme. Lines:
//
//   1B kind (0 = debit, 1 = credit, 2 = memo)
//   if kind in {0, 1}: 8B account (LE), 8B amount (LE)
//   if kind == 2:      2B length (LE), bytes
//
// A trailing record kind of 0xFF marks end-of-txn within the payload. This
// keeps the parser one-pass and avoids any length prefix at the line level.
#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

#include "core/transaction.h"
#include "core/types.h"

namespace ledgercore::wal {

// Header sizes are part of the on-disk contract; do not change without a
// version bump.
inline constexpr std::size_t kHeaderBytes = 4 + 8 + 16;  // length + txn_id + key hash
inline constexpr std::size_t kTrailerBytes = 4;          // crc32c
inline constexpr std::uint8_t kKindDebit = 0;
inline constexpr std::uint8_t kKindCredit = 1;
inline constexpr std::uint8_t kKindMemo = 2;
inline constexpr std::uint8_t kKindEnd = 0xFF;

struct Record {
    TxnId txn_id{0};
    IdempotencyKeyHash key_hash{};
    Transaction txn;
};

// Encode a record to bytes (header + payload + crc trailer).
std::vector<std::uint8_t> encode(const Record& r);

// Decode a record. Returns nullopt on any framing or CRC error. Caller is
// expected to pass exactly one record's bytes (length-prefix and all).
std::optional<Record> decode(const std::uint8_t* data, std::size_t n);

// CRC32C (Castagnoli) over the supplied byte range. Pure software impl; the
// WAL hot path is fsync-bound so the CRC cost is in the noise.
std::uint32_t crc32c(const std::uint8_t* data, std::size_t n) noexcept;

}  // namespace ledgercore::wal
