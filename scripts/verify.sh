#!/usr/bin/env bash
# Verifies a saved report against a manifest.

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <report> <manifest>" >&2
  exit 2
fi

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${project_dir}/build-Release/core_engine"
if [[ ! -x "$binary" ]]; then
  echo "Release binary not found; run ./scripts/build.sh Release first." >&2
  exit 2
fi

exec "$binary" verify --report "$1" --manifest "$2"
