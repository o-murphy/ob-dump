#!/usr/bin/env bash
# Runs exactly what pytest.yml's CI job and release.yml's test-python job both
# do — kept as one script specifically so it can be run locally
# (./scripts/ci/test-python.sh) instead of only discoverable by pushing and
# watching a workflow run.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../py"

uv sync --dev
uv run ruff check .
uv run pytest
