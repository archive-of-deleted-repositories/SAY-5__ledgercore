#include "wal/writer.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace ledgercore::wal {

WalWriter::WalWriter(const std::filesystem::path& path, bool fsync_on_append)
    : path_(path), fsync_on_append_(fsync_on_append) {
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0640);
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "open wal: " + path_.string());
    }
}

WalWriter::~WalWriter() {
    if (fd_ >= 0) {
        if (fsync_on_append_) ::fsync(fd_);
        ::close(fd_);
    }
}

bool WalWriter::append(const Transaction& txn, TxnId id, const IdempotencyKeyHash& key_hash) {
    // Cross-check the invariant. If the caller is a buggy ledger this stops
    // the bad record before it ever hits disk. A corrupted intake therefore
    // cannot bypass durability.
    auto d = sumDebits(txn);
    auto c = sumCredits(txn);
    if (!d.has_value() || !c.has_value() || *d != *c) {
        return false;
    }

    Record r;
    r.txn_id = id;
    r.key_hash = key_hash;
    r.txn = txn;
    auto bytes = encode(r);

    std::lock_guard lock(mu_);
    const std::uint8_t* p = bytes.data();
    std::size_t left = bytes.size();
    while (left > 0) {
        ssize_t w = ::write(fd_, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += w;
        left -= static_cast<std::size_t>(w);
    }
    bytes_ += bytes.size();
    if (fsync_on_append_) {
        if (::fsync(fd_) != 0) return false;
        ++fsyncs_;
    }
    return true;
}

}  // namespace ledgercore::wal
