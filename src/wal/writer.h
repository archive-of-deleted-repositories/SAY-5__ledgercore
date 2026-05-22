// WAL writer. append() encodes a Record, writes it to the open file, then
// fsync()s. Returns false if the cross-check of the double-entry invariant
// fails or if the underlying I/O fails. A single mutex serializes appends so
// the file offsets and the fsync()s are atomic w.r.t. each other.
#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

#include "core/transaction.h"
#include "wal/format.h"

namespace ledgercore::wal {

class WalWriter {
public:
    // The fsync_on_append flag is for benchmark mode only. Production paths
    // must keep it true; turning it off is exclusively for measuring the
    // fsync cost.
    explicit WalWriter(const std::filesystem::path& path, bool fsync_on_append = true);
    ~WalWriter();

    WalWriter(const WalWriter&) = delete;
    WalWriter& operator=(const WalWriter&) = delete;

    // Append the txn under the given id+key_hash. Returns false on cross-check
    // failure (sum-of-debits != sum-of-credits) or on I/O error.
    bool append(const Transaction& txn, TxnId id, const IdempotencyKeyHash& key_hash);

    // For tests: total bytes written since open. Cheap; does not stat.
    std::uint64_t bytesWritten() const noexcept { return bytes_; }

    // For tests/bench: count of fsync()s issued.
    std::uint64_t fsyncs() const noexcept { return fsyncs_; }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    int fd_{-1};
    bool fsync_on_append_{true};
    std::mutex mu_;
    std::uint64_t bytes_{0};
    std::uint64_t fsyncs_{0};
};

}  // namespace ledgercore::wal
