#!/usr/bin/env bash
# Regression check for "reading must fail loudly when the native LMDB library
# cannot be loaded". The @Native bindings resolve the library lazily, so
# without the eager probe in readObjectBoxRecords() the failure would surface
# as an ArgumentError ("Couldn't resolve native function 'mdb_env_create'")
# deep inside the first read.
#
# Builds a tiny console app that depends on this package, runs it from the
# build bundle (the read must succeed), deletes liblmdb from the bundle and
# runs it again (the read must throw an LmdbException that names the library).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../dart" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"

mkdir bin
cat > pubspec.yaml <<YAML
name: missing_library_check
environment:
  sdk: ^3.10.0
dependencies:
  ob_dump_reader:
    path: $ROOT
YAML
cat > bin/check.dart <<'DART'
import 'dart:io';

import 'package:ob_dump_reader/ob_dump_reader.dart';
// ignore: implementation_imports
import 'package:ob_dump_reader/src/fixture_writer.dart';

// check <dir>: writes a store to <dir> unless it already has one, then reads it.
Future<void> main(List<String> args) async {
  final dir = Directory(args.single);
  try {
    if (!File('${dir.path}/data.mdb').existsSync()) {
      writeFixture(dir.path, {dataKey(1, 1): [1, 2, 3]});
    }
    var n = 0;
    await readObjectBoxRecords(dir.path, (_) => n++);
    stdout.writeln('read ok: $n record');
  } catch (e) {
    stdout.writeln('read threw: $e');
  }
}
DART

dart pub get >/dev/null
dart build cli --target bin/check.dart -o out >/dev/null
EXE="$(find out -type f -name check -o -type f -name check.exe | head -1)"

STORE="$TMP/store"; mkdir "$STORE"
OUT="$("$EXE" "$STORE" 2>&1 || true)"
echo "intact bundle: $OUT"
[ "$OUT" = "read ok: 1 record" ] || { echo "ERROR: reading should succeed with the library present" >&2; exit 1; }

find out \( -name 'liblmdb.*' -o -name 'lmdb.dll' \) -delete
OUT="$("$EXE" "$STORE" 2>&1 || true)"
echo "library removed: ${OUT%%$'\n'*}"
case "$OUT" in
  "read threw: LmdbException: Could not load the native LMDB library"*) echo "OK: reading fails fast when the library is missing" ;;
  *) echo "ERROR: reading did not fail with a clear error with the library removed:" >&2; echo "$OUT" >&2; exit 1 ;;
esac
