#!/usr/bin/env bash
# Runs exactly what test.yml's test-dart job and release.yml (via test.yml) do
# -- kept as one script specifically so it can be run locally
# (./scripts/ci/test-dart.sh) instead of only discoverable by pushing and
# watching a workflow run.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../dart"

# --no-example: dart/example is a Flutter app (needs `flutter pub get`).
dart pub get --no-example

# `dart test` runs the package's build hook (hook/build.dart), which compiles
# the vendored LMDB source (src/lmdb/) -- no prebuilt binary and no separate
# build step; a C compiler is all that's needed (present by default on GitHub's
# ubuntu/macos/windows images).
dart analyze --fatal-infos
dart test
