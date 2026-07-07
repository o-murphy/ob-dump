#!/usr/bin/env bash
# Runs exactly what ci.yml's test-js job does — kept as one script
# specifically so it can be run locally (./scripts/ci/test-js.sh) instead of
# only discoverable by pushing and watching a workflow run.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../js"

yarn install --frozen-lockfile
yarn build
yarn test
