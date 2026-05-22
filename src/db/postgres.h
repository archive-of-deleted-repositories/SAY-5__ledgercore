// Thin libpqxx wrapper used by the integration test harness. Only built when
// LEDGERCORE_ENABLE_POSTGRES is on.
#pragma once

#include <memory>
#include <string>

namespace ledgercore::db {

class Postgres {
public:
    explicit Postgres(const std::string& url);
    ~Postgres();

    // Execute a one-shot statement. Throws on failure.
    void exec(const std::string& sql);

    // Returns the result of `SELECT 1` as a sanity check.
    int ping();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ledgercore::db
