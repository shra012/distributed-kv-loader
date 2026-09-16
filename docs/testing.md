# Testing

## Commands

```bash
./scripts/test.sh
./scripts/test.sh --sanitizers
./scripts/test.sh --stress 20
```

The first command builds Debug and runs every CTest case. Sanitizer mode additionally runs the full
suite under AddressSanitizer plus UndefinedBehaviorSanitizer and ThreadSanitizer. Stress mode repeats
all E2E and NET cases.

Every production target and API test compiles against the `include/core_engine/*.hpp` headers
directly. On a toolchain that also supports C++23 named modules, a `MODULE-SMOKE` test additionally
validates that `import core_engine;` builds and links against the re-exported module API.

Each dependency-free C++ test is registered separately with CTest, has an isolated temporary
directory and deterministic seed, and reports its stable ID on failure. Integration tests invoke
only built executables.

| Group | Coverage |
|---|---|
| `CFG-*`, `SCH-*` | Configuration, limits, path resolution, schema |
| `PAR-*` | Streaming boundaries, escapes, integer grammar, raw bytes |
| `HSH-*` | Canonical keys, FNV vectors, Jump Hash balance and movement |
| `COD-*` | Frame round trips, malformed headers, bounds, checksum |
| `NET-*`, `QUE-*` | Socket semantics, backpressure, faults, cancellation |
| `STO-*`, `REF-*` | Duplicate policy, ownership scan, independent oracle |
| `E2E-*` | One-to-five nodes, failures, determinism, generation, verification |
| `RPT-*`, `GEN-*` | Invariants, report grammar, manifests, deterministic data |
| `MODULE-SMOKE` | Optional: the C++23 module builds and `import core_engine;` links, when supported |

To run one case:

```bash
ctest --test-dir build-Debug -R '^E2E-16$' --output-on-failure
```

Add a test with `CORE_ENGINE_TEST(function_name, "ID")`, register the ID in `CMakeLists.txt`, use
the supplied `TestContext`, and set an explicit timeout for long-running cases.
