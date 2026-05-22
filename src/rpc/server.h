// gRPC server. Only built when LEDGERCORE_ENABLE_GRPC is on.
#pragma once

#include <memory>
#include <string>

#include "core/ledger.h"
#include "idem/cache.h"
#include "intake/dispatcher.h"

namespace ledgercore::rpc {

class Server {
public:
    Server(std::string listen_addr, std::shared_ptr<Ledger> ledger,
           std::shared_ptr<idem::Cache> cache);
    ~Server();

    // Blocks until shutdown() is called from another thread.
    void run();
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ledgercore::rpc
