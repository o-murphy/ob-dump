#!/usr/bin/env bash
# Configure + build + test the C++ core — shared by cpp.yml's regular CI
# and release.yml's build-cpp matrix (which only differs by passing a real
# OB_DUMP_VERSION and extra CMake flags, e.g. macOS's universal-binary
# CMAKE_OSX_ARCHITECTURES). Runnable locally as-is:
#   ./scripts/ci/build-cpp.sh
#   OB_DUMP_VERSION=1.2.3 ./scripts/ci/build-cpp.sh -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

# No fallback hardcoded here — if OB_DUMP_VERSION isn't set, omit the
# -D entirely so CMakeLists.txt's own `set(OB_DUMP_VERSION ... CACHE STRING
# ...)` default is what applies. Two hardcoded defaults drift out of sync
# with each other (this one already had, silently, after a real release
# bumped the CMake one but not this file).
#
# A string, not an array: macOS's default /bin/bash is 3.2 (last GPL v2
# release, Apple never shipped GPLv3 bash), where `"${arr[@]}"` on a
# *declared-but-empty* array throws "unbound variable" under `set -u` —
# fixed in bash 4.4+, but this needs to run as-is on macOS CI. Caught by
# a real macOS run failing (see git history), not proactively — no OB_DUMP_
# VERSION-shaped value here ever contains spaces, so plain word-splitting
# below is safe.
cmake_version_flag=""
if [ -n "${OB_DUMP_VERSION:-}" ]; then
  cmake_version_flag="-DOB_DUMP_VERSION=$OB_DUMP_VERSION"
fi

cmake -B build $cmake_version_flag "$@"
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure -C Release
