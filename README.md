# Core Engine

Core Engine is a C++17 implementation of a configurable one-to-five-node distributed record
loader. Each mock node owns an isolated runtime, communicates only through bounded socket-like
channels, and stores each key on exactly one owner selected by stable hashing.

## Prerequisites

- CMake 3.16 or newer
- Any C++17 compiler with pthread and sanitizer support (GCC or Clang, any modern version)
- Bash, CTest, and standard Unix command-line tools
- About 2 GB of free memory and 600 MB of disk space for the five-node demo

On macOS, `scripts/build.sh` automatically prefers Homebrew LLVM when present, and otherwise falls
back to the default toolchain; either GCC or Clang builds the full test suite. The validated
toolchains are Homebrew Clang 22.1.8 and GCC 14.2.0 with CMake 4.4.3.

An import-able C++23 module re-exporting the same API (`modules/core_engine.cppm`) is built
additionally, with its own smoke test, whenever the toolchain supports named modules (Ninja, CMake
3.28+, Clang 18+). It is optional: unsupported toolchains simply skip it and build the primary
target normally. See [Architecture](docs/architecture.md) for details.

## Quickstart

```bash
./scripts/build.sh Release
./scripts/demo.sh 5
./scripts/test.sh
```

The demo generates 100 MB for each source node, loads the data, writes
`results/demo-5/report.txt`, and independently verifies that report against the generated manifest.

## Report

A successful report contains `JOB`, one `NODE` per configured node, `DIST`, per-node and global
`VERIFY`, optional `EXPECT`, and `TIMING` lines. All `VERIFY` values and `EXPECT manifest` must be
`PASS`. `TIMING` is the only non-deterministic line.

```text
VERIFY node=0 scanned=2262742 misowned=0 result=PASS
VERIFY ownership=PASS uniqueness=PASS accounting=PASS completion=PASS
EXPECT manifest=PASS unique=11315085 duplicates=594978
```

Failures print one sanitized `ERROR code=...` line to stderr and no success report.

## Documentation

- [Architecture](docs/architecture.md)
- [Formats](docs/formats.md)
- [Running](docs/running.md)
- [Testing](docs/testing.md)
- [Performance](docs/performance.md)
- [Scalability](docs/scalability.md)
- [Limitations](docs/limitations.md)
