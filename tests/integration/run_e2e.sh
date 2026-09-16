#!/usr/bin/env bash
# Runs executable-level generated-data and verification cases.

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <core_engine_binary> <E2E-14|E2E-16|E2E-FAILURES>" >&2
  exit 2
fi

binary="$1"
case_id="$2"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/core-engine-e2e.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

write_config() {
  local path="$1"
  local count="$2"
  {
    echo "version=1"
    echo "node_count=${count}"
    echo "schema=id:string,value:int32"
    echo "key_field=id"
    local node
    for ((node = 0; node < count; ++node)); do
      echo "input.${node}=node${node}.tsv"
    done
    echo "batch_bytes=32768"
    echo "channel_bytes=65536"
    echo "inbound_queue_batches=4"
    echo "max_record_bytes=4096"
    echo "read_buffer_bytes=65536"
  } >"$path"
}

run_generated_case() {
  local count="$1"
  local directory="${work_dir}/nodes-${count}"
  mkdir -p "$directory"
  write_config "${directory}/config.conf" "$count"
  "$binary" generate --config "${directory}/config.conf" --bytes-per-file 1000000 --seed 42 \
    --duplicate-percent 5 --manifest "${directory}/data.manifest" >/dev/null
  "$binary" load --config "${directory}/config.conf" --manifest "${directory}/data.manifest" \
    --report "${directory}/report.txt" >"${directory}/stdout.txt"
  cmp "${directory}/stdout.txt" "${directory}/report.txt"
  grep -q '^EXPECT manifest=PASS ' "${directory}/report.txt"
  if grep -q '^VERIFY .*FAIL' "${directory}/report.txt"; then
    return 1
  fi
  "$binary" verify --report "${directory}/report.txt" --manifest "${directory}/data.manifest"
}

assert_failure() {
  local config="$1"
  local error_code="$2"
  shift 2
  set +e
  "$binary" load --config "$config" "$@" >"${work_dir}/failure.out" \
    2>"${work_dir}/failure.err"
  local exit_status=$?
  set -e
  [[ $exit_status -eq 3 ]]
  [[ ! -s "${work_dir}/failure.out" ]]
  [[ "$(wc -l <"${work_dir}/failure.err")" -eq 1 ]]
  grep -q "^ERROR code=${error_code} " "${work_dir}/failure.err"
}

if [[ "$case_id" == "E2E-14" ]]; then
  for count in 1 2 3 5; do
    run_generated_case "$count"
  done
elif [[ "$case_id" == "E2E-16" ]]; then
  run_generated_case 2
  directory="${work_dir}/nodes-2"
  awk -F= '
    /^expected\.0=/ { first=$2; print "expected.0=" $2-1; next }
    /^expected\.1=/ { print "expected.1=" $2+1; next }
    { print }
  ' "${directory}/data.manifest" >"${directory}/mismatch.manifest"
  set +e
  "$binary" verify --report "${directory}/report.txt" \
    --manifest "${directory}/mismatch.manifest" >"${directory}/verify.out" 2>"${directory}/verify.err"
  verify_status=$?
  set -e
  [[ $verify_status -eq 5 ]]
  [[ ! -s "${directory}/verify.out" ]]
  grep -q '^ERROR code=MANIFEST_MISMATCH ' "${directory}/verify.err"
elif [[ "$case_id" == "E2E-FAILURES" ]]; then
  failure_dir="${work_dir}/failures"
  mkdir -p "$failure_dir"
  write_config "${failure_dir}/config.conf" 1
  assert_failure "${failure_dir}/config.conf" INPUT_FILE
  printf 'good\t1\nbad\n' >"${failure_dir}/node0.tsv"
  assert_failure "${failure_dir}/config.conf" MALFORMED_RECORD
  printf 'same\t1\nsame\t2\n' >"${failure_dir}/node0.tsv"
  assert_failure "${failure_dir}/config.conf" CONFLICTING_DUPLICATE
  printf 'good\t1\n' >"${failure_dir}/node0.tsv"
  assert_failure "${failure_dir}/config.conf" INPUT_FILE --report "${failure_dir}/missing/report.txt"
else
  usage
  exit 2
fi
