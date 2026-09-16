#!/usr/bin/env bash
# Creates the source-only Core Engine release archive.

set -euo pipefail

if [[ $# -ne 0 ]]; then
  echo "Usage: $0" >&2
  exit 2
fi

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(awk '/project\(CoreEngine VERSION/ {print $3}' "${project_dir}/CMakeLists.txt")"
archive="${project_dir}/core_engine-${version}.tar.gz"
staging="$(mktemp -d "${TMPDIR:-/tmp}/core-engine-package.XXXXXX")"
trap 'rm -rf "$staging"' EXIT
root="${staging}/core_engine-${version}"
mkdir -p "$root"

for path in CMakeLists.txt .clang-format .gitignore README.md cmake config docs include modules \
  scripts src tests; do
  cp -R "${project_dir}/${path}" "$root/"
done

tar -czf "$archive" -C "$staging" "core_engine-${version}"
echo "$archive"
