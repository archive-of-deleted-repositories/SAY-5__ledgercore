#include "idem/persist.h"

#include <pqxx/pqxx>

#include <iostream>
#include <mutex>

namespace ledgercore::idem {

namespace {

std::string hexHash(const IdempotencyKeyHash& h) {
    static const char* kHex = "0123456789abcdef";
    std::string s(32, ' ');
    for (std::size_t i = 0; i < h.size(); ++i) {
        s[i * 2] = kHex[(h[i] >> 4) & 0xF];
        s[i * 2 + 1] = kHex[h[i] & 0xF];
    }
    return s;
}

}  // namespace

struct Persist::Impl {
    explicit Impl(const std::string& url) : conn(url) {}
    pqxx::connection conn;
    std::mutex mu;
};

Persist::Persist(const std::string& pg_url) : impl_(std::make_unique<Impl>(pg_url)) {}
Persist::~Persist() = default;

void Persist::ensureSchema() {
    std::lock_guard lock(impl_->mu);
    pqxx::work tx(impl_->conn);
    tx.exec(R"sql(
        CREATE TABLE IF NOT EXISTS idempotency_keys (
            key_hash    CHAR(32)    PRIMARY KEY,
            outcome     SMALLINT    NOT NULL,
            reason      SMALLINT    NOT NULL,
            txn_id      BIGINT      NOT NULL,
            last_seen   TIMESTAMPTZ NOT NULL DEFAULT NOW()
        );
        CREATE UNIQUE INDEX IF NOT EXISTS idempotency_keys_hash_unique
            ON idempotency_keys (key_hash);
    )sql");
    tx.commit();
}

void Persist::put(const IdempotencyKeyHash& key, const CachedOutcome& outcome) {
    try {
        std::lock_guard lock(impl_->mu);
        pqxx::work tx(impl_->conn);
        tx.exec_params(
            R"sql(
              INSERT INTO idempotency_keys (key_hash, outcome, reason, txn_id, last_seen)
              VALUES ($1, $2, $3, $4, NOW())
              ON CONFLICT (key_hash) DO UPDATE
                SET outcome = EXCLUDED.outcome,
                    reason  = EXCLUDED.reason,
                    txn_id  = EXCLUDED.txn_id,
                    last_seen = NOW()
            )sql",
            hexHash(key), static_cast<int>(outcome.outcome), static_cast<int>(outcome.reason),
            static_cast<long long>(outcome.txn_id));
        tx.commit();
    } catch (const std::exception& e) {
        // Mirror is best-effort. Log and move on.
        std::cerr << "idem persist put failed: " << e.what() << "\n";
    }
}

void Persist::warm(Cache& cache, std::size_t max_entries) {
    try {
        std::lock_guard lock(impl_->mu);
        pqxx::work tx(impl_->conn);
        auto rows = tx.exec_params(
            "SELECT key_hash, outcome, reason, txn_id FROM idempotency_keys "
            "ORDER BY last_seen DESC LIMIT $1",
            static_cast<long long>(max_entries));
        for (const auto& row : rows) {
            const auto hex = row[0].as<std::string>();
            if (hex.size() != 32) continue;
            IdempotencyKeyHash h{};
            for (std::size_t i = 0; i < 16; ++i) {
                auto parse = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                int hi = parse(hex[i * 2]);
                int lo = parse(hex[i * 2 + 1]);
                if (hi < 0 || lo < 0) {
                    h[0] = 0xFF;
                    break;
                }
                h[i] = static_cast<std::uint8_t>((hi << 4) | lo);
            }
            CachedOutcome co{};
            co.outcome = static_cast<Outcome>(row[1].as<int>());
            co.reason = static_cast<RejectReason>(row[2].as<int>());
            co.txn_id = static_cast<TxnId>(row[3].as<long long>());
            cache.insert(h, co);
        }
    } catch (const std::exception& e) {
        std::cerr << "idem persist warm failed: " << e.what() << "\n";
    }
}

}  // namespace ledgercore::idem
