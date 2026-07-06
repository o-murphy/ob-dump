#!/usr/bin/env bash
# Runs exactly what dart.yml's CI job and release.yml's test-dart job both
# do — kept as one script specifically so it can be run locally
# (./scripts/ci/test-dart.sh) instead of only discoverable by pushing and
# watching a workflow run. Missing dart_lmdb2:fetch_native here is exactly
# the kind of thing that has, in practice, only been caught by an actual CI
# failure so far.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../dart"

dart pub get

# dart_lmdb2 ships prebuilt native libraries but doesn't fetch them on
# `pub get` — required before first use, per its README.
dart run dart_lmdb2:fetch_native

dart analyze
dart test
