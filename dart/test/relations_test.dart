// Builds a small synthetic LMDB store with real ObjectBox ToMany relation
// link keys (see docs/BACKLOG.md "ToMany relations" for how this 12-byte
// format was determined) and verifies readToManyTargets resolves the
// forward-direction links only, ignoring the auto-maintained backward ones.
import 'dart:io';

import 'package:ob_dump_reader/ob_dump_reader.dart';
import 'package:test/test.dart';

import 'test_fixture.dart';

Future<void> main() async {
  test('resolves forward-direction ToMany links, ignoring backward ones', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    writeFixture(srcDir.path, {
      // Book#1 --relation 1 (forward)--> Author#1, Author#2.
      relationKey(1, 0, 1, 1): const [],
      relationKey(1, 0, 1, 2): const [],
      // Auto-maintained backward links (Author -> Book) — must not leak in.
      relationKey(1, 2, 1, 1): const [],
      relationKey(1, 2, 2, 1): const [],
      // A second relation on the same source id — must not leak in either.
      relationKey(2, 0, 1, 99): const [],
    });

    final targets = await readToManyTargets(srcDir.path, 1, 1);
    expect(targets, [1, 2]);
  });

  test('returns an empty list when the source object has no links', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    writeFixture(srcDir.path, {relationKey(1, 0, 1, 1): const []});

    final targets = await readToManyTargets(srcDir.path, 1, 2);
    expect(targets, isEmpty);
  });

  test('reads the directory directly, in place, no copy', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    writeFixture(srcDir.path, {relationKey(1, 0, 5, 7): const []});

    final targets = await readToManyTargets(srcDir.path, 1, 5);
    expect(targets, [7]);
  });
}
