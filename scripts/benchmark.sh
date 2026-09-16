#!/usr/bin/env bash
# Benchmarks one full-scale configuration and records a reproducible result row.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <config>" >&2
  exit 2
fi

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
config="$1"
[[ -f "$config" ]] || { echo "Configuration not found: $config" >&2; exit 2; }
"${project_dir}/scripts/build.sh" Release
binary="${project_dir}/build-Release/core_engine"
name="$(basename "${config%.*}")"
benchmark_dir="${project_dir}/data/benchmark-${name}"
result_dir="${project_dir}/results/benchmark-${name}"
manifest="${benchmark_dir}/data.manifest"
report="${result_dir}/report.txt"
timing="${result_dir}/time.txt"
mkdir -p "$benchmark_dir" "$result_dir" "${project_dir}/results"

"$binary" generate --config "$config" --bytes-per-file 100000000 --seed 42 \
  --duplicate-percent 5 --manifest "$manifest"

if [[ "$(uname -s)" == "Darwin" ]]; then
  /usr/bin/time -l -o "$timing" "$binary" load --config "$config" --manifest "$manifest" \
    --report "$report" >/dev/null
  peak_rss="$(awk '/maximum resident set size/ {print $1}' "$timing")"
else
  /usr/bin/time -v -o "$timing" "$binary" load --config "$config" --manifest "$manifest" \
    --report "$report" >/dev/null
  peak_rss="$(awk -F: '/Maximum resident set size/ {gsub(/ /, "", $2); print $2 * 1024}' "$timing")"
fi

"$binary" verify --report "$report" --manifest "$manifest"
nodes="$(awk -F= '$1 == "node_count" {print $2}' "$config")"
files="$(awk -F= '$1 == "files" {print $2}' "$manifest")"
records_per_s="$(awk -F'[ =]' '/^TIMING / {print $5}' "$report")"
mb_per_s="$(awk -F'[ =]' '/^TIMING / {print $7}' "$report")"
elapsed_ms="$(awk -F'[ =]' '/^TIMING / {print $3}' "$report")"
compiler="$(${CXX:-/opt/homebrew/opt/llvm/bin/clang++} --version | head -n 1 | tr '|' '/')"
hardware="$(uname -m)"
if [[ "$(uname -s)" == "Darwin" ]]; then
  hardware="$(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m)"
fi
table="${project_dir}/results/benchmarks.md"
if [[ ! -f "$table" ]]; then
  {
    echo "# Benchmarks"
    echo
    echo "| UTC | Nodes | Files | MB/file | Seed | Records/s | MB/s | Load ms | Peak RSS bytes | Compiler | Hardware | Build | Config |"
    echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|"
  } >"$table"
fi
printf '| %s | %s | %s | 100 | 42 | %s | %s | %s | %s | %s | %s | Release | %s |\n' \
  "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$nodes" "$files" "$records_per_s" "$mb_per_s" \
  "$elapsed_ms" "$peak_rss" "$compiler" "$hardware" "$config" >>"$table"
tail -n 1 "$table"
