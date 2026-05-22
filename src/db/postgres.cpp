#include "db/postgres.h"

#include <pqxx/pqxx>

namespace ledgercore::db {

struct Postgres::Impl {
    explicit Impl(const std::string& url) : conn(url) {}
    pqxx::connection conn;
};

Postgres::Postgres(const std::string& url) : impl_(std::make_unique<Impl>(url)) {}
Postgres::~Postgres() = default;

void Postgres::exec(const std::string& sql) {
    pqxx::work tx(impl_->conn);
    tx.exec(sql);
    tx.commit();
}

int Postgres::ping() {
    pqxx::work tx(impl_->conn);
    auto r = tx.exec("SELECT 1");
    tx.commit();
    return r.at(0).at(0).as<int>();
}

}  // namespace ledgercore::db
