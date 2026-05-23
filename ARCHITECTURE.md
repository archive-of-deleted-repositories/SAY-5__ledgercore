# Architecture

## Shape

```
   client
      |
      | gRPC Submit(Transaction)
      v
   +---------------+    per-worker SPSC ring    +-----------+
   | gRPC server   |--------------------------->| worker N  |
   +---------------+                             +-----+-----+
                                                       |
                                  +---------+ <--------+
                                  | WAL     |  append, fsync
                                  | writer  |
                                  +----+----+
                                       |
                                       v
                              +----------------+
                              | apply in-memory|
                              | + Postgres     |
                              +----------------+
```

## The double-entry invariant

A `Transaction` is N debit lines + N credit lines. The invariant is:

```
sum(debits) == sum(credits)
```

Enforced at three layers:

1. **Intake.** `Transaction::Validate` rejects unbalanced requests
   before the WAL is touched.
2. **WAL append.** The serializer re-checks the sums when writing the
   record, so a corrupted intake cannot bypass.
3. **Apply.** At apply time a final `assert` (not just a check) aborts
   the process on violation. This is the safety net against memory
   corruption; we would rather crash than make an unbalanced commit.

## Lock-free SPSC intake

Each worker owns one `SpscRing<Transaction>` (power-of-two slots, atomic
head/tail with acquire/release ordering). The gRPC handler thread is the
single producer; the worker thread is the single consumer. No mutexes on
the hot path.

A `Slot` constructs `Transaction` in place via placement new because the
type is not trivially constructible (it holds `std::vector` lines). The
SPSC implementation supports this from day one; the test under TSan
stresses 1M items through a single ring without races.

## WAL durability

Wire format (`docs/wal-format.md`):

```
[u32 length][u64 txn_id][u64 idempotency_key_hash][bytes payload][u32 crc32c]
```

`WalWriter::Append` writes the record, calls `fsync(2)`, then signals
the worker to apply. Recovery on startup reads the WAL forward; any
record whose payload CRC matches and which isn't already in the Postgres
`txns_applied` table is replayed.

The bench harness reports throughput in both WAL-on and WAL-off modes so
the fsync cost is visible.

## Idempotency at three layers

1. In-memory LRU of recent `idempotency_key` outcomes
2. Postgres `idempotency_keys` table with the mirrored LRU contents
3. UNIQUE index on `(idempotency_key)` in the same table as a final
   safety net

A client replaying the same `Submit` gets the original outcome without
re-applying. The LRU is bounded; on miss we read Postgres; on Postgres
miss the request is treated as new.

## Partitioning + cross-partition 2PC

Accounts are hash-partitioned across workers. Single-partition
transactions are FIFO through one worker's SPSC ring. Cross-partition
transactions go through a two-phase commit between the involved workers:
prepare all, then commit all, then ack to client. Abort path is the
mirror.

## What is deliberately not here

- Real payments switch (no ISO 8583, no card networks)
- Real bank ledger (single-currency, integer cents, no FX, no settlement
  windows)
- Multi-region replication (single host)
- Regulatory compliance (WAL is durable but no AML hooks, no audit
  workflow)
- Online schema migration (down-the-line concern)
