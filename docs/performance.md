# Performance

## Method

Measurements used Release, deterministic seed 42, 100 MB per active input file, Homebrew Clang
22.1.8, and an Apple M2 (4 performance and 4 efficiency cores). `scripts/benchmark.sh` records
report throughput and macOS peak resident set size. Results include parsing, routing, transfer,
storage, and post-join verification.

Baseline table, recorded before the hot-path pass below:

| Nodes | Dataset | Records/s | MB/s | Load ms | Peak RSS |
|---:|---:|---:|---:|---:|---:|
| 1 | 100 MB | 2,562,330 | 106.3 | 941 | 283,230,208 B |
| 2 | 200 MB | 3,760,014 | 156.9 | 1,275 | 568,131,584 B |
| 3 | 300 MB | 4,852,455 | 202.8 | 1,479 | 857,767,936 B |
| 5 | 500 MB | 5,338,441 | 224.1 | 2,231 | 1,429,504,000 B |

## Hot-path pass

Direct `core_engine load --config <config>` runs on the same data, no manifest, idle machine.
`TIMING elapsed_ms` now also covers cluster construction and store pre-sizing, which it previously
excluded, so the after column is a stricter measurement than the before column. After also
includes the later cleanup pass: one routing path for local and remote batches, shared field
encoders, and indexed counters.

| Config | Before (median ms) | After (median ms) | Peak RSS before | Peak RSS after |
|---|---:|---:|---:|---:|
| `one_node.conf`, 100 MB | 813 | 297 | 283,230,208 B | 226,869,248 B |
| `five_nodes.conf`, 500 MB | 2,641 | 805 | 1,429,504,000 B | 1,155,956,736 B |

Peak RSS before is the baseline table's value; after is `/usr/bin/time -l` on the direct run. Store
memory fell because a 24-byte slot plus arena bytes replaces a heap map node plus separately
allocated key and record strings.

Profiling with `sample` before the pass showed about 920 ns of CPU per record, spent in:

1. `std::unordered_map` insertion and rehashing: one heap node per record and a cache miss per
   bucket chain, plus doubling rehashes without a reserve.
2. Hashing each key twice with byte-at-a-time FNV-1a, once in the reader and once in the store.
3. Three to five heap allocations per record: decoded field strings, a field vector, the packed
   string, the canonical key string, and a copy of the source path on every local insert.
4. Up to seven `memchr` calls per line and byte-at-a-time escape decoding.
5. A global socket-table mutex taken on every `send` and `recv` call, and a full payload copy into
   each encoded frame.

Changes, each preserving the report bytes and the full test suite:

- The store became an open-addressing table over an arena (see `architecture.md`). Inserts no longer
  allocate, and a probe usually touches one cache line.
- FNV-1a runs once per record for ownership; the store uses its own word-at-a-time mixer.
- The parser packs directly into reused buffers and hands out views: no per-record allocation.
- Field scanning uses one SWAR pass for tab, backslash, and NUL instead of several `memchr` calls.
- Receivers queue whole validated frames; the store worker walks the payload once.
- `send_all` and `recv_exact` resolve the socket once instead of on every partial transfer.
- Senders write the frame header and the payload separately, removing one copy of every remote byte.
- Stores are pre-sized from sampled line density when no manifest is given.

## Bottlenecks

With allocation and double hashing removed, a single node is reader-bound: one reader parses 100 MB
in roughly 300 ms on a performance core while the store worker keeps up on a second core.

Five nodes run five readers, five store workers, and twenty mostly idle receivers on eight cores, four
of them efficiency cores. Wall time is set by the slowest reader, and at least one reader runs on an
efficiency core at roughly half speed while competing with store workers. That keeps 500 MB near
0.75 to 0.95 s with one reader thread per file. Remaining measurable costs are the ring copies
that model the network, FNV-1a's byte-serial loop, first-touch page faults for about 1.16 GB of peak
memory, and condition-variable wakeups per frame.

The generated five-node distribution had 2,262,443 to 2,263,679 unique records per node, a maximum
deviation of 0.03% from the mean.

The final validation repeated both full-size cases after the cleanup. The one-node median was
297 ms, and the five-node median was 805 ms. A 1 MiB batch-size trial overlapped the 256 KiB
baseline under interleaved runs, so the smaller default was retained.

## Further options

These were not applied; each trades design simplicity or a plan contract for speed:

- Split each input file into newline-aligned chunks parsed by several threads, removing the
  one-reader-per-file floor. Line numbers need per-chunk offsets.
- Vectorize line and field scanning with platform intrinsics (the simdjson structural-index approach)
  instead of portable SWAR.
- Memory-map input files to avoid the read-buffer copy.
- Adopt received frame buffers as store arena blocks instead of copying records out of them.
- Replace mutex-and-condition-variable rings with single-producer single-consumer lock-free rings.

## Tuning

The main knobs are `batch_bytes`, `channel_bytes`, `inbound_queue_batches`, and
`read_buffer_bytes`. Configuration validation keeps their combined temporary allocation under 1 GiB.
