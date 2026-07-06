#!/usr/bin/env bash
# Configure + build + test the C++ core — shared by cpp.yml's regular CI
# and release.yml's build-cpp matrix (which only differs by passing a real
# OB_DUMP_VERSION and extra CMake flags, e.g. macOS's universal-binary
# CMAKE_OSX_ARCHITECTURES). Runnable locally as-is:
#   ./scripts/ci/build-cpp.sh
#   OB_DUMP_VERSION=1.2.3 ./scripts/ci/build-cpp.sh -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

: "${OB_DUMP_VERSION:=0.1.0-dev}"

cmake -B build -DOB_DUMP_VERSION="$OB_DUMP_VERSION" "$@"
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure -C Release
