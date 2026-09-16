# Running and verification

Run commands from the project root.

## Build

```bash
./scripts/build.sh Release
```

The build requires CMake 3.16 or newer and any C++17 compiler (GCC or Clang). On macOS,
`scripts/build.sh` prefers Homebrew LLVM when installed, and otherwise falls back to the default
toolchain. When the toolchain also has Ninja, CMake 3.28 or newer, and Clang 18 or newer, the build
additionally produces an `import`-able C++23 module and its smoke test; other toolchains simply
skip that optional target.

## Generate and load

```bash
./scripts/generate.sh config/example.conf 1000000 42
./scripts/run.sh config/example.conf data/example.manifest
```

`run.sh` prints the report and writes its path as `REPORT_PATH=...` on stderr. Confirm every
`VERIFY` and `EXPECT` field is `PASS`, then independently check the saved report:

```bash
./scripts/verify.sh results/latest/report.txt data/example.manifest
```

The verifier exits silently with 0 on a match and prints one error with exit 5 on a mismatch.

## Node counts and integer keys

```bash
./scripts/demo.sh 1
./scripts/demo.sh 2
./scripts/demo.sh 5
./build-Release/core_engine generate --config config/int_key.conf --bytes-per-file 1000000 --seed 42 --manifest data/int-key.manifest
./build-Release/core_engine load --config config/int_key.conf --manifest data/int-key.manifest
```

Each demo uses 100 MB per source file. The integer-key commands provide a smaller direct example.

## Expected failures

An unknown option or invalid configuration exits 2. Missing input, malformed data, schema errors,
and conflicting duplicates exit 3. Transport or frame failures exit 4. Failed ownership,
accounting, completion, or manifest verification exits 5. Unexpected invariant failures exit 6.
Failure stdout is empty; stderr contains one `ERROR code=...` line.

## Troubleshooting

- If CMake is missing or too old, install CMake 3.16 or newer.
- The primary build works without Ninja; install it only if you also want the optional C++23
  module target.
- On macOS, install Homebrew LLVM if the default compiler cannot build the project.
- If a Release script says the binary is missing, run `./scripts/build.sh Release`.
- Ensure the demo has at least 600 MB disk and about 2 GB memory available.
- Generated inputs are under `data/`; reports and measurements are under `results/`.
