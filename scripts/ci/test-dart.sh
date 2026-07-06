#!/usr/bin/env bash
# Runs exactly what dart.yml's CI job and release.yml's test-dart job both
# do — kept as one script specifically so it can be run locally
# (./scripts/ci/test-dart.sh) instead of only discoverable by pushing and
# watching a workflow run.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../dart"

dart pub get

# Compiles the vendored LMDB source (src/lmdb/) into the shared library the
# tests' FFI bindings load — no prebuilt binary, needs a C compiler + CMake
# on the runner (present by default on GitHub's ubuntu/macos/windows images).
dart run ob_dump_reader:build

dart analyze
dart test
