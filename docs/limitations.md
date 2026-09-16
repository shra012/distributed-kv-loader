# Limitations

- The primary C++17 header build works with any modern GCC or Clang and CMake 3.16 or newer; both
  Homebrew Clang 22.1.8 and GCC 14.2.0 are validated on macOS.
- The optional `import`-able C++23 module needs Ninja, CMake 3.28 or newer, and Clang 18 or newer;
  GCC modules do not compile against the tested macOS 26 SDK, so on GCC only the module target is
  skipped, and the primary build and every test still run normally.
- Node servers are simulated inside one process; this is not a multi-process or multi-host system.
- The final key-value store is in memory and has no persistence or spill path.
- Strings are opaque bytes, not Unicode-validated text; NUL remains forbidden by the TSV contract.
- A repeated key with differing bytes fails the whole job; no merge policy is provided.
- Cancellation stops the job, but there is no recovery, retry, or checkpoint resume.
- FNV-1a is a stable distribution hash and corruption checksum, not a cryptographic primitive.
- The store copies each canonical key beside its packed record, so key bytes are stored twice.
- Membership is fixed for a job; there is no live rebalancing or replication.
- Manifest expectations rely on the production ownership function, protected by independent golden
  vectors rather than a second hash implementation.
