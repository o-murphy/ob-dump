#!/usr/bin/env bash
# Confirms the C++ core, dart/src/lmdb/, and flutter/src/lmdb/ all agree on
# the same LMDB release — see LMDB_VERSION at the repo root for why this has
# to be checked by hand instead of shared via one git submodule (three
# separately-published/built consumers, no reliable cross-boundary path).
#
# Default (no network): reads MDB_VERSION_{MAJOR,MINOR,PATCH} straight out of
# each vendored lmdb.h and compares against LMDB_VERSION, plus greps the C++
# core's CMakeLists.txt for the same tag. Cheap enough to run on every push.
#
# --full (network required): additionally clones the pinned tag fresh and
# diffs it byte-for-byte against both vendored copies, to catch someone
# hand-editing a vendored file without bumping LMDB_VERSION (the macro check
# alone can't see that). Meant for CI/periodic maintenance, not every push.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

full=0
if [ "${1:-}" = "--full" ]; then
  full=1
fi

pinned=$(head -1 LMDB_VERSION)
echo "Pinned LMDB version: $pinned"

# LMDB_0.9.31 -> 0.9.31 -> 0 9 31
pinned_dotted="${pinned#LMDB_}"
IFS='.' read -r pinned_major pinned_minor pinned_patch <<< "$pinned_dotted"

status=0

check_header_version() {
  local label="$1" header="$2"
  if [ ! -f "$header" ]; then
    echo "::error::$label: $header not found"
    status=1
    return
  fi
  local major minor patch
  major=$(grep -m1 '^#define MDB_VERSION_MAJOR' "$header" | awk '{print $3}')
  minor=$(grep -m1 '^#define MDB_VERSION_MINOR' "$header" | awk '{print $3}')
  patch=$(grep -m1 '^#define MDB_VERSION_PATCH' "$header" | awk '{print $3}')
  if [ "$major.$minor.$patch" != "$pinned_dotted" ]; then
    echo "::error::$label: $header reports $major.$minor.$patch, expected $pinned_dotted (LMDB_VERSION)"
    status=1
  else
    echo "$label: OK ($major.$minor.$patch)"
  fi
}

check_header_version "dart/src/lmdb"    dart/src/lmdb/lmdb.h
check_header_version "flutter/src/lmdb" flutter/src/lmdb/lmdb.h

# C++ core doesn't vendor a copy — it FetchContents the pinned tag fresh at
# configure time — so just confirm CMakeLists.txt reads the same file rather
# than a stale hardcoded tag.
if ! grep -q 'GIT_TAG        \${LMDB_VERSION}' CMakeLists.txt; then
  echo "::error::CMakeLists.txt no longer reads LMDB_VERSION for its LMDB GIT_TAG"
  status=1
else
  echo "CMakeLists.txt (C++ core): OK (reads LMDB_VERSION)"
fi

if [ "$full" = 1 ]; then
  echo "--full: cloning $pinned fresh and diffing vendored copies..."
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' EXIT
  git clone --depth 1 --branch "$pinned" https://github.com/LMDB/lmdb.git "$tmp/lmdb" --quiet

  for dest in dart/src/lmdb flutter/src/lmdb; do
    for f in mdb.c midl.c lmdb.h midl.h; do
      if ! diff -q "$tmp/lmdb/libraries/liblmdb/$f" "$dest/$f" > /dev/null 2>&1; then
        echo "::error::$dest/$f differs from upstream $pinned — hand-edited without bumping LMDB_VERSION?"
        status=1
      fi
    done
  done
  [ "$status" = 0 ] && echo "Full diff: OK, vendored copies match upstream $pinned exactly"
fi

exit $status
