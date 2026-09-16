# Architecture

## Headers and the optional module

`include/core_engine/*.hpp` is the canonical, always-built API: one focused header per component
(`error`, `schema`, `store`, `node`, `cluster`, and so on), included directly by the library
sources, the executable, and the tests. This is a portable C++17 build with no toolchain-specific
requirements.

`modules/core_engine.cppm` additionally re-exports that same API as a named C++23 module, so
`import core_engine;` works wherever the toolchain supports it (Ninja, CMake 3.28+, Clang 18+). It
is a thin `export using` wrapper over the headers, not a second declaration surface, and it builds
alongside the primary target without affecting it; unsupported toolchains simply skip it.

## Workflow

1. `load_config` validates the versioned configuration, schema, paths, limits, and temporary-memory
   budget before any thread starts.
2. `Cluster` creates one `Node` per configured node plus a shared `MockNetwork`, cancellation token,
   and first-failure collector, then pre-sizes every store.
3. Every node binds and listens; all outbound connections are created; all inbound connections are
   accepted before loading begins.
4. Each reader streams its local TSV file, packs each line straight into one reused buffer, hashes
   its canonical key once to find the owner, and appends the packed bytes to a local or remote batch.
5. Receiver threads validate each frame and move the whole validated frame buffer into a bounded
   inbound queue; records are not split or copied on the receive path.
6. One store worker per node walks each batch payload, inserts records, drops byte-identical
   duplicates, and rejects conflicts.
7. The coordinator joins every thread, scans every stored key, checks accounting and completion,
   then emits the report.

## Thread and startup sequence

```text
coordinator        node readers          mock channels       receivers       store workers
bind all
connect all
accept all
start workers ---------------------------------------------------------------> wait
start receivers ---------------------------------------------> wait
start readers -------> parse, hash, batch -> DATA frames ----> validate, queue -> insert
                      flush, END frames ---> END ------------> queue END -----> complete
join all
scan ownership and report
```

The shared cancellation token wakes blocked channels and queues. Every thread boundary captures its
first typed error. The coordinator joins all started threads on success and failure.

## Parsing

The reader finds line ends with `memchr` over a fixed read buffer. Inside a line, one SWAR pass
(SIMD within a 64-bit register) tests eight bytes at a time for tab, backslash, and NUL using the
zero-byte test `(w - 0x01..01) & ~w & 0x80..80`; only a hit falls back to byte-by-byte scanning. A
field with no backslash is copied in one `append`; only escaped fields are decoded byte by byte. The
packed record and canonical key are written into buffers reused for the whole file, so parsing does
not allocate per record.

## Ownership

Canonical string keys are `0x01` followed by decoded bytes. Canonical `int32` keys are `0x02`
followed by four big-endian bytes. FNV-1a 64 hashes those bytes, and Jump Consistent Hash maps the
hash to `[0,node_count)`. For example, canonical string key `alpha` maps to node 2 in a five-node
cluster. Type tags keep string `"42"` distinct from integer `42`.

Jump Hash gives deterministic ownership, statistical balance, and minimal movement when a node is
added. FNV-1a is computed once per record, by the reader. Ownership never uses `std::hash`.

## Storage

Each store is an open-addressing hash table with linear probing over a power-of-two slot array. A
24-byte slot holds a cached table hash, a pointer to the record's bytes, and the key and record
lengths. Keys and records are copied back to back into 4 MiB append-only arena blocks, so inserting
a record performs no heap allocation. The slot index is Fibonacci hashing, `(h * 2^64/phi) >> shift`,
which takes the well-mixed high bits of the product. The table grows by doubling at 70% load.

The table hash is a word-at-a-time 64-bit mixer, separate from FNV-1a: the store only needs a fast,
well-distributed index, while FNV-1a remains the fixed ownership contract. The post-join ownership
scan recomputes FNV-1a from the stored key bytes, so verification does not trust any cached value.

Before loading, stores are sized from the manifest's `expected.N` when given; otherwise from the line
density of the first 64 KiB of input. Both hints are capped by what the input bytes could hold.

## Batching and wire format

Each reader keeps one batch builder per destination node, itself included. A batch is
flushed before the next record would exceed `batch_bytes`. Local and remote batches carry the same
concatenated packed-record payload, so the store worker treats both identically. Queue and channel
capacities bound temporary memory independently of input size.

Frames contain fixed-width big-endian fields: magic, version, type, source, destination, sequence,
record count, payload size, and FNV checksum. A sender transmits the 32-byte header and then the
batch payload in place, without copying the payload into a frame buffer; the bytes on the wire are
unchanged. Receivers bound the declared payload size before sizing the frame buffer, then validate
the full header, endpoint, monotonic sequence, checksum, and complete record consumption.

## Completion and duplicates

Every source sends one FIFO-ordered `END` marker to every destination, including a local marker. A
store worker finishes only after receiving exactly `node_count` markers and all preceding batches.

The store keeps one packed value per key. A repeated key with identical packed bytes increments the
duplicate count and is dropped. A differing value fails the job. This avoids arrival-order-dependent
first-wins or last-wins behavior.
