#!/usr/bin/env bash
# Runs a Release load and saves its report under results.

set -euo pipefail

usage() {
  echo "Usage: $0 <config> [manifest]" >&2
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage
  exit 2
fi

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${project_dir}/build-Release/core_engine"
if [[ ! -x "$binary" ]]; then
  echo "Release binary not found; run ./scripts/build.sh Release first." >&2
  exit 2
fi

result_dir="${project_dir}/results/$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir -p "$result_dir"
command=("$binary" load --config "$1" --report "${result_dir}/report.txt")
if [[ $# -eq 2 ]]; then
  command+=(--manifest "$2")
fi
"${command[@]}"
mkdir -p "${project_dir}/results/latest"
cp "${result_dir}/report.txt" "${project_dir}/results/latest/report.txt"
echo "REPORT_PATH=${result_dir}/report.txt" >&2
