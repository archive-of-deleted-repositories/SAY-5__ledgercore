# ledgercore

A concurrent double-entry payments ledger in C++20. Lock-free SPSC ring
buffers per worker for transaction intake, a write-ahead log for durability
before any state mutation, idempotency keys that prevent double-charges, and
a multi-threaded worker pool that sustains high transaction throughput on a
single host.

## What this studies

- **The double-entry invariant.** Every transaction is N debit lines + N
  credit lines whose sums must be equal. The invariant is enforced in three
  places: at intake (request-shape check), at WAL append (cross-checked so a
  corrupted intake cannot bypass), and at apply time (a final assertion that
  abort()s on memory corruption).
- **Lock-free intake.** One SPSC ring buffer per worker thread; the gRPC
  handler is the producer, the worker is the consumer. Acquire/release
  atomics on head/tail; no mutexes on the hot path.
- **WAL durability.** Each transaction is serialized to a binary record,
  fsync'd, then applied. Recovery on startup replays any uncommitted record.
- **Idempotency keys.** A bounded LRU of seen `(idempotency_key)` outcomes
  mirrored to Postgres, plus a UNIQUE index at the database layer as a final
  safety net.

## How this differs from `SAY-5/orderbook-sim` and `SAY-5/inference-router`

| Axis | orderbook-sim | inference-router | ledgercore |
| ---- | ------------- | ---------------- | ---------- |
| Domain | matching engine | TCP request router | payments ledger |
| Invariant | price-time priority | round-robin / least-conn | balanced debits == credits |
| Persistence | in-memory only | none | WAL + Postgres |
| Idempotency | n/a | n/a | three-layer keys |
| Concurrency | SPSC ring (one consumer) | epoll + thread pool | SPSC per worker, partitioned by account |

## Architecture

```
client --gRPC Submit(Txn)-->  +----------+   per-worker SPSC   +---------+
                              | gRPC svr |-------------------->| worker N|
                              +----------+                     +----+----+
                                                                    |
                                          +-----------+  WAL fsync  |
                                          | WAL writer|<------------+
                                          +-----------+             |
                                                                    v
                                                          +------------------+
                                                          | apply to memory  |
                                                          | + Postgres state |
                                                          +------------------+
```

## Quickstart

```
make build       # cmake + build
make test        # ctest
make bench       # the 1M-tx bench harness
docker compose up   # ledger + postgres
```

## What this is *not*

- Not a real payments switch (no ISO 8583, no card networks).
- Not a real bank ledger (single-currency, integer cents, no FX).
- Not multi-region (single host; replication is a separate study).
- Not regulatory-compliant (WAL is durable but no AML hooks, no audit trail
  beyond the WAL itself).

## License

MIT.
