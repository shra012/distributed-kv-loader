#!/usr/bin/env bash
# Runs the full generated-data demonstration for one to five nodes.

set -euo pipefail

if [[ $# -gt 1 || ( $# -eq 1 && ! "$1" =~ ^[1-5]$ ) ]]; then
  echo "Usage: $0 [node_count]" >&2
  exit 2
fi

node_count="${1:-5}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"${project_dir}/scripts/build.sh" Release
binary="${project_dir}/build-Release/core_engine"
demo_dir="${project_dir}/data/demo-${node_count}"
result_dir="${project_dir}/results/demo-${node_count}"
mkdir -p "$demo_dir" "$result_dir"
config="${demo_dir}/config.conf"
manifest="${demo_dir}/data.manifest"
report="${result_dir}/report.txt"

{
  echo "version=1"
  echo "node_count=${node_count}"
  echo "schema=id:string,name:string,age:int32"
  echo "key_field=id"
  for ((node = 0; node < node_count; ++node)); do
    echo "input.${node}=node${node}.tsv"
  done
  echo "batch_bytes=262144"
  echo "channel_bytes=1048576"
  echo "inbound_queue_batches=8"
  echo "max_record_bytes=65536"
  echo "read_buffer_bytes=1048576"
} >"$config"

"$binary" generate --config "$config" --bytes-per-file 100000000 --seed 42 \
  --duplicate-percent 5 --manifest "$manifest"
"$binary" load --config "$config" --manifest "$manifest" --report "$report"
"$binary" verify --report "$report" --manifest "$manifest"
grep '^VERIFY\|^EXPECT\|^TIMING' "$report"
