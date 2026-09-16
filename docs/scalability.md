# Scalability

The current transport seam can be replaced by real sockets while preserving frame encoding,
backpressure, routing, and store interfaces. Scaling to hundreds of machines additionally requires:

- a versioned membership snapshot and explicit partition epochs;
- authenticated transport, endpoint discovery, and health monitoring;
- batch IDs, acknowledgements, retries, and durable checkpoints;
- spillable queues and an LSM or sorted-run store when data exceeds memory;
- credit-based cross-host backpressure and measured compression;
- per-partition metrics, tracing, and skew detection;
- controlled between-job rebalancing using Jump Hash movement;
- replication and consensus as a separate durability design.

At-least-once delivery can remain correct because inserts are idempotent for byte-identical records
and conflicting values fail. Exactly-once job semantics require durable batch progress and atomic
checkpoint advancement. Membership changes should create a new epoch; frames from stale epochs must
be rejected, and ownership migration should finish before the next load becomes visible.
