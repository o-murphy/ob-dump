#!/usr/bin/env bash
# Runs exactly what flutter.yml's CI job and release.yml's test-flutter job
# both do — see test-dart.sh for why this lives as a standalone, locally
# runnable script rather than being duplicated inline in each workflow.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../flutter"

flutter pub get
flutter analyze
