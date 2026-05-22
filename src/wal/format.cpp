#include "wal/format.h"

#include <array>
#include <cstring>

namespace ledgercore::wal {

namespace {

// Castagnoli polynomial 0x1EDC6F41, reversed: 0x82F63B78.
constexpr std::uint32_t kPoly = 0x82F63B78u;

std::array<std::uint32_t, 256> buildTable() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            c = (c & 1u) ? ((c >> 1) ^ kPoly) : (c >> 1);
        }
        t[i] = c;
    }
    return t;
}

const std::array<std::uint32_t, 256>& crcTable() {
    static const auto t = buildTable();
    return t;
}

void putU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}
void putU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}
std::uint16_t readU16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t readU32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t readU64(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    return v;
}

}  // namespace

std::uint32_t crc32c(const std::uint8_t* data, std::size_t n) noexcept {
    const auto& t = crcTable();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        c = t[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

std::vector<std::uint8_t> encode(const Record& r) {
    std::vector<std::uint8_t> out;
    // Reserve a generous amount; we resize the length field at the end.
    out.reserve(64 + 16 * (r.txn.debits.size() + r.txn.credits.size()));

    // Placeholder for length; filled in below.
    putU32(out, 0);
    putU64(out, r.txn_id);
    for (auto b : r.key_hash) out.push_back(b);

    const std::size_t payload_start = out.size();
    for (const auto& line : r.txn.debits) {
        out.push_back(kKindDebit);
        putU64(out, line.account);
        putU64(out, static_cast<std::uint64_t>(line.amount));
    }
    for (const auto& line : r.txn.credits) {
        out.push_back(kKindCredit);
        putU64(out, line.account);
        putU64(out, static_cast<std::uint64_t>(line.amount));
    }
    if (!r.txn.memo.empty()) {
        out.push_back(kKindMemo);
        const auto len = static_cast<std::uint16_t>(
            std::min<std::size_t>(r.txn.memo.size(), 0xFFFFu));
        putU16(out, len);
        out.insert(out.end(), r.txn.memo.begin(), r.txn.memo.begin() + len);
    }
    // Also the idempotency key bytes go in the payload so replay can repopulate
    // the LRU. Format: kKindMemo with a 2-byte tag prefix would be cleaner, but
    // we instead use a dedicated kind 3.
    out.push_back(0x03);
    const auto klen =
        static_cast<std::uint16_t>(std::min<std::size_t>(r.txn.idempotency.value.size(), 0xFFFFu));
    putU16(out, klen);
    out.insert(out.end(), r.txn.idempotency.value.begin(),
               r.txn.idempotency.value.begin() + klen);

    out.push_back(kKindEnd);

    const std::size_t payload_end = out.size();
    const std::uint32_t payload_len = static_cast<std::uint32_t>(payload_end - payload_start);
    // Fill in the length field (header offset 0).
    out[0] = static_cast<std::uint8_t>(payload_len & 0xFF);
    out[1] = static_cast<std::uint8_t>((payload_len >> 8) & 0xFF);
    out[2] = static_cast<std::uint8_t>((payload_len >> 16) & 0xFF);
    out[3] = static_cast<std::uint8_t>((payload_len >> 24) & 0xFF);

    const std::uint32_t c = crc32c(out.data(), out.size());
    putU32(out, c);
    return out;
}

std::optional<Record> decode(const std::uint8_t* data, std::size_t n) {
    if (n < kHeaderBytes + 1 + kTrailerBytes) return std::nullopt;
    const std::uint32_t payload_len = readU32(data);
    const std::size_t total = kHeaderBytes + payload_len + kTrailerBytes;
    if (total != n) return std::nullopt;

    const std::uint32_t expected = readU32(data + kHeaderBytes + payload_len);
    if (crc32c(data, kHeaderBytes + payload_len) != expected) return std::nullopt;

    Record r;
    r.txn_id = readU64(data + 4);
    std::memcpy(r.key_hash.data(), data + 12, 16);

    const std::uint8_t* p = data + kHeaderBytes;
    const std::uint8_t* end = p + payload_len;
    bool saw_end = false;
    while (p < end) {
        const std::uint8_t kind = *p++;
        if (kind == kKindEnd) {
            saw_end = true;
            break;
        }
        if (kind == kKindDebit || kind == kKindCredit) {
            if (end - p < 16) return std::nullopt;
            Line line{};
            line.account = readU64(p);
            line.amount = static_cast<Amount>(readU64(p + 8));
            p += 16;
            if (kind == kKindDebit) r.txn.debits.push_back(line);
            else r.txn.credits.push_back(line);
        } else if (kind == kKindMemo) {
            if (end - p < 2) return std::nullopt;
            std::uint16_t len = readU16(p);
            p += 2;
            if (end - p < len) return std::nullopt;
            r.txn.memo.assign(reinterpret_cast<const char*>(p), len);
            p += len;
        } else if (kind == 0x03) {
            if (end - p < 2) return std::nullopt;
            std::uint16_t len = readU16(p);
            p += 2;
            if (end - p < len) return std::nullopt;
            r.txn.idempotency.value.assign(reinterpret_cast<const char*>(p), len);
            p += len;
        } else {
            return std::nullopt;
        }
    }
    if (!saw_end || p != end) return std::nullopt;
    return r;
}

}  // namespace ledgercore::wal
