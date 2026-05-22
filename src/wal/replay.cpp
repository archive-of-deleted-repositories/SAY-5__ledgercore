#include "wal/replay.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <fstream>
#include <vector>

#include "wal/format.h"

namespace ledgercore::wal {

ReplayStats replay(const std::filesystem::path& path, Ledger& ledger) {
    ReplayStats stats;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // No file => nothing to replay. Not an error.
        return stats;
    }
    std::vector<std::uint8_t> buf;
    while (true) {
        // Read length header.
        std::uint8_t header[kHeaderBytes];
        in.read(reinterpret_cast<char*>(header), kHeaderBytes);
        if (in.gcount() == 0) break;  // EOF
        if (in.gcount() != static_cast<std::streamsize>(kHeaderBytes)) {
            stats.tail_truncated = true;
            break;
        }
        std::uint32_t payload_len = static_cast<std::uint32_t>(header[0]) |
                                    (static_cast<std::uint32_t>(header[1]) << 8) |
                                    (static_cast<std::uint32_t>(header[2]) << 16) |
                                    (static_cast<std::uint32_t>(header[3]) << 24);
        // Sanity: reject obviously bogus lengths so a corrupted header does
        // not cause us to allocate gigabytes.
        if (payload_len > (1u << 24)) {
            stats.tail_truncated = true;
            break;
        }
        const std::size_t record_size = kHeaderBytes + payload_len + kTrailerBytes;
        buf.resize(record_size);
        std::memcpy(buf.data(), header, kHeaderBytes);
        in.read(reinterpret_cast<char*>(buf.data() + kHeaderBytes),
                payload_len + kTrailerBytes);
        if (in.gcount() != static_cast<std::streamsize>(payload_len + kTrailerBytes)) {
            stats.tail_truncated = true;
            break;
        }
        auto rec = decode(buf.data(), buf.size());
        if (!rec.has_value()) {
            stats.tail_truncated = true;
            break;
        }
        ++stats.records_seen;
        auto res = ledger.applyFromWal(rec->txn, rec->txn_id);
        if (res.outcome == Outcome::Committed) {
            ++stats.records_applied;
        }
        stats.bytes_consumed += record_size;
    }
    return stats;
}

}  // namespace ledgercore::wal
