#!/usr/bin/env bash
# Generates deterministic Core Engine input files and their manifest.

set -euo pipefail

usage() {
  echo "Usage: $0 <config> <bytes_per_file> <seed>" >&2
  echo "Example: $0 config/example.conf 1000000 42" >&2
}

if [[ $# -ne 3 ]]; then
  usage
  exit 2
fi

config="$1"
bytes_per_file="$2"
seed="$3"

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${project_dir}/build-Release/core_engine"

if [[ ! -x "$binary" ]]; then
  echo "Release binary not found at $binary; run ./scripts/build.sh Release first." >&2
  exit 2
fi

command=("$binary" generate --config "$config" --bytes-per-file "$bytes_per_file" --seed "$seed"
         --duplicate-percent 5 --manifest "${project_dir}/data/$(basename "${config%.*}").manifest")

exec "${command[@]}"
