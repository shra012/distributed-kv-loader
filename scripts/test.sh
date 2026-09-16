#!/usr/bin/env bash
# Runs the Core Engine test, sanitizer, and stress suites.

set -euo pipefail

usage() {
  echo "Usage: $0 [--sanitizers] [--stress N]" >&2
}

sanitizers=0
stress=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --sanitizers)
      [[ $sanitizers -eq 0 ]] || { usage; exit 2; }
      sanitizers=1
      shift
      ;;
    --stress)
      [[ $stress -eq 0 && $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || { usage; exit 2; }
      stress="$2"
      shift 2
      ;;
    *)
      usage
      exit 2
      ;;
  esac
done

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"${project_dir}/scripts/build.sh" Debug
ctest --test-dir "${project_dir}/build-Debug" --output-on-failure

if [[ $sanitizers -eq 1 ]]; then
  for build_type in Asan Tsan; do
    "${project_dir}/scripts/build.sh" "$build_type"
    ctest --test-dir "${project_dir}/build-${build_type}" --output-on-failure
  done
fi

for ((iteration = 0; iteration < stress; ++iteration)); do
  ctest --test-dir "${project_dir}/build-Debug" --output-on-failure -R '^(E2E|NET)'
done
