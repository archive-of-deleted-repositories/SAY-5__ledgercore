#include "rpc/server.h"

#include <grpcpp/grpcpp.h>

#include "ledger.grpc.pb.h"

namespace ledgercore::rpc {

namespace pb = ledgercore::v1;

namespace {

pb::Outcome toPbOutcome(Outcome o) {
    switch (o) {
        case Outcome::Committed:
            return pb::OUTCOME_COMMITTED;
        case Outcome::Rejected:
            return pb::OUTCOME_REJECTED;
        case Outcome::Replayed:
            return pb::OUTCOME_REPLAYED;
    }
    return pb::OUTCOME_UNSPECIFIED;
}

pb::RejectReason toPbReason(RejectReason r) {
    return static_cast<pb::RejectReason>(static_cast<int>(r));
}

Transaction fromPb(const pb::Transaction& in) {
    Transaction t;
    t.idempotency.value = in.idempotency_key();
    t.memo = in.memo();
    t.debits.reserve(in.debits_size());
    for (const auto& l : in.debits()) {
        t.debits.push_back({l.account(), l.amount()});
    }
    t.credits.reserve(in.credits_size());
    for (const auto& l : in.credits()) {
        t.credits.push_back({l.account(), l.amount()});
    }
    return t;
}

class LedgerServiceImpl final : public pb::LedgerService::Service {
public:
    LedgerServiceImpl(std::shared_ptr<Ledger> ledger,
                       std::shared_ptr<intake::Dispatcher> dispatcher)
        : ledger_(std::move(ledger)), dispatcher_(std::move(dispatcher)) {}

    grpc::Status Submit(grpc::ServerContext*, const pb::SubmitRequest* req,
                        pb::SubmitResponse* res) override {
        auto txn = fromPb(req->txn());
        auto r = dispatcher_->submit(txn);
        res->set_outcome(toPbOutcome(r.outcome));
        res->set_reason(toPbReason(r.reason));
        res->set_txn_id(r.txn_id);
        res->set_idempotency_hit(r.idempotency_hit);
        return grpc::Status::OK;
    }

    grpc::Status OpenAccount(grpc::ServerContext*, const pb::OpenAccountRequest* req,
                              pb::OpenAccountResponse* res) override {
        res->set_ok(ledger_->openAccount(req->account(), req->opening()));
        return grpc::Status::OK;
    }

    grpc::Status Balance(grpc::ServerContext*, const pb::BalanceRequest* req,
                          pb::BalanceResponse* res) override {
        auto b = ledger_->balanceOf(req->account());
        res->set_exists(b.has_value());
        res->set_balance(b.value_or(0));
        return grpc::Status::OK;
    }

private:
    std::shared_ptr<Ledger> ledger_;
    std::shared_ptr<intake::Dispatcher> dispatcher_;
};

}  // namespace

struct Server::Impl {
    std::string addr;
    std::shared_ptr<Ledger> ledger;
    std::shared_ptr<idem::Cache> cache;
    std::shared_ptr<intake::Dispatcher> dispatcher;
    std::unique_ptr<grpc::Server> server;
};

Server::Server(std::string listen_addr, std::shared_ptr<Ledger> ledger,
               std::shared_ptr<idem::Cache> cache)
    : impl_(std::make_unique<Impl>()) {
    impl_->addr = std::move(listen_addr);
    impl_->ledger = std::move(ledger);
    impl_->cache = std::move(cache);
    impl_->dispatcher =
        std::make_shared<intake::Dispatcher>(1, impl_->cache, impl_->ledger);
}

Server::~Server() { shutdown(); }

void Server::run() {
    LedgerServiceImpl svc(impl_->ledger, impl_->dispatcher);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(impl_->addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&svc);
    impl_->server = builder.BuildAndStart();
    if (impl_->server) impl_->server->Wait();
}

void Server::shutdown() {
    if (impl_ && impl_->server) {
        impl_->server->Shutdown();
        impl_->server.reset();
    }
}

}  // namespace ledgercore::rpc
