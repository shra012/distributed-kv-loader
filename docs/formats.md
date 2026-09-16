# Formats

## Configuration

Configuration is strict line-oriented `key=value`. Blank lines and lines beginning with `#` are
ignored. Unknown, duplicate, missing, malformed, or out-of-range values are rejected.

Required keys are `version=1`, `node_count` from 1 through 5, `schema`, `key_field`, and every
contiguous `input.N`. An input value of `-` creates a receiver-only node. Relative paths resolve from
the configuration directory.

Schema fields use `name:string` or `name:int32`, separated by commas. Names match
`[A-Za-z_][A-Za-z0-9_]*` and are unique.

| Key | Default |
|---|---:|
| `batch_bytes` | 262144 |
| `channel_bytes` | 1048576 |
| `inbound_queue_batches` | 8 |
| `max_record_bytes` | 65536 |
| `read_buffer_bytes` | 1048576 |

Sizes must be positive `uint32` values. `max_record_bytes <= batch_bytes <= channel_bytes` and
`max_record_bytes <= read_buffer_bytes`. The computed temporary-memory budget may not exceed 1 GiB.

## TSV input

Fields follow schema order and use a tab separator. Supported escapes are `\\`, `\t`, `\n`, and
`\r`. LF, CRLF, and an unterminated final line are accepted. String values may be empty, but string
keys may not. NUL, bad escapes, wrong field counts, and oversized lines are rejected.

Integers allow `0`, negative values, and positive digits in the signed 32-bit range. A plus sign,
whitespace, overflow, empty text, and leading zeros are rejected.

## Manifest

The manifest is strict `key=value` with `version`, `seed`, `bytes_per_file`, `duplicate_percent`,
`files`, `lines`, `unique`, `duplicates`, and contiguous `expected.N` entries. It must satisfy
`lines = unique + duplicates` and the sum of `expected.N` must equal `unique`.

## Report

The line order is `JOB`, `NODE` by node ID, `DIST`, per-node `VERIFY`, global `VERIFY`, optional
`EXPECT`, then `TIMING`. All lines except `TIMING` are deterministic for fixed input.

## Exit codes

| Exit | Meaning |
|---:|---|
| 0 | Success |
| 2 | Command line or configuration |
| 3 | Schema, input, malformed record, or conflicting duplicate |
| 4 | Transport or codec |
| 5 | Verification or manifest mismatch |
| 6 | Internal invariant |

Errors contain only code and optional node, file, line, and sanitized detail fields. Record contents
are never printed.
