// WAL replay. Opens the file in read-only mode, scans records one-by-one, and
// applies each through Ledger::applyFromWal(). Stops at the first framing or
// CRC error (treating that record and everything after it as torn-write
// garbage from a crash before fsync completed). Returns the number of records
// successfully replayed.
#pragma once

#include <cstdint>
#include <filesystem>

#include "core/ledger.h"

namespace ledgercore::wal {

struct ReplayStats {
    std::uint64_t records_seen{0};
    std::uint64_t records_applied{0};
    std::uint64_t bytes_consumed{0};
    bool tail_truncated{false};  // true if we stopped on a bad record
};

ReplayStats replay(const std::filesystem::path& path, Ledger& ledger);

}  // namespace ledgercore::wal
