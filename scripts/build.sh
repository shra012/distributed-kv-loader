#!/usr/bin/env bash
# Builds Core Engine in a selected configuration.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 Debug|Release|Asan|Tsan" >&2
  exit 2
fi

configuration="$1"
build_type="$configuration"
sanitizer="None"

case "$configuration" in
  Debug | Release) ;;
  Asan)
    build_type="Debug"
    sanitizer="Asan"
    ;;
  Tsan)
    build_type="Debug"
    sanitizer="Tsan"
    ;;
  *)
    echo "Usage: $0 Debug|Release|Asan|Tsan" >&2
    exit 2
    ;;
esac

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build-${configuration}"
compiler="${CXX:-}"

generator="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  generator="Ninja"
fi

if [[ -z "$compiler" && -x /opt/homebrew/opt/llvm/bin/clang++ ]]; then
  compiler="/opt/homebrew/opt/llvm/bin/clang++"
elif [[ -z "$compiler" && -x /usr/local/opt/llvm/bin/clang++ ]]; then
  compiler="/usr/local/opt/llvm/bin/clang++"
fi

cmake_options=(
  -G "$generator"
  -S "$project_dir"
  -B "$build_dir"
  -DCMAKE_BUILD_TYPE="$build_type"
  -DCORE_ENGINE_SANITIZER="$sanitizer"
)

if [[ -n "$compiler" ]]; then
  cmake_options+=("-DCMAKE_CXX_COMPILER=$compiler")
fi

if [[ "$(uname -s)" == "Darwin" && "$compiler" == *g++-* ]]; then
  sdk_path="$(xcrun --show-sdk-path)"
  macos_version="$(sw_vers -productVersion)"
  export SDKROOT="$sdk_path"
  export MACOSX_DEPLOYMENT_TARGET="${macos_version%%.*}.0"
  cmake_options+=("-DCMAKE_OSX_SYSROOT=$sdk_path")
  cmake_options+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET")
fi

if [[ -f "${build_dir}/CMakeCache.txt" ]] &&
  ! grep -q "^CMAKE_GENERATOR:INTERNAL=${generator}\$" "${build_dir}/CMakeCache.txt"; then
  cmake --fresh "${cmake_options[@]}"
else
  cmake "${cmake_options[@]}"
fi
cmake --build "$build_dir" --parallel
