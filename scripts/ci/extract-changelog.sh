#!/usr/bin/env bash
# Prints the release-notes body for $1 from the CHANGELOG.md at $2 — the
# section under "## [<version>]", falling back to "## [Unreleased]" if
# there's no exact match (forgetting to rename it before tagging is a
# common, easy slip). Prints nothing (exit 0) if neither is found; the
# caller decides whether that's fatal.
#
# Pulled out of release.yml specifically because this bit of awk/sed was
# genuinely fiddly to get right (dot-escaping in the version regex, not
# letting the CHANGELOG's own trailing link-reference lines leak into the
# extracted body) — much easier to iterate on and verify locally, e.g.:
#   ./scripts/ci/extract-changelog.sh 0.1.0-alpha.0 dart/CHANGELOG.md
# than only inside a workflow run.
set -euo pipefail

VERSION="${1:?usage: extract-changelog.sh <version> <changelog-file>}"
CHANGELOG="${2:?usage: extract-changelog.sh <version> <changelog-file>}"

extract_section() {
    # $1: heading to match inside "## [...]", already regex-escaped by the caller.
    awk "/^## \[$1\]/{found=1; next} /^## \[/{if(found) exit} /^\[.+\]:/{if(found) exit} found{print}" \
        "$CHANGELOG" | sed -e '/./,$!d' -e 's/[[:space:]]*$//'
}

# Escape dots — an unescaped "." in awk's regex matches any character, and
# every real version has some.
V_RE="${VERSION//./\\.}"

notes="$(extract_section "$V_RE")"
if [ -z "$notes" ]; then
    echo "No changelog entry for $VERSION in $CHANGELOG — trying [Unreleased]." >&2
    notes="$(extract_section 'Unreleased')"
fi

printf '%s' "$notes"
