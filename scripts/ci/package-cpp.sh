#!/usr/bin/env bash
# Packages the CLI binary + shared lib + public header from an existing
# build/ directory into ob-dump-<name>.tar.gz. Only used by release.yml
# right now, but pulled out into its own script (rather than inlined in the
# workflow) so the OS-conditional path logic can be tried locally against
# whatever this machine's own build/ looks like, instead of only being
# discoverable by pushing and reading a matrix job's log.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

name="${1:?usage: package-cpp.sh <artifact-name>, e.g. linux-x64}"

mkdir -p dist
if [ -f build/Release/ob_dump.exe ]; then
  cp build/Release/ob_dump.exe build/Release/ob_dump.dll build/Release/ob_dump.lib dist/
elif [ -f build/ob_dump.exe ]; then
  cp build/ob_dump.exe build/ob_dump.dll build/ob_dump.lib dist/
else
  cp build/ob_dump dist/
  cp build/libob_dump.* dist/
fi
cp include/ob_dump.h dist/

tar -czf "ob-dump-${name}.tar.gz" -C dist .
echo "Wrote ob-dump-${name}.tar.gz"
