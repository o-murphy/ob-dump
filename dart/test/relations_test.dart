// Builds a small synthetic LMDB store with real ObjectBox ToMany relation
// link keys (see docs/BACKLOG.md "ToMany relations" for how this 12-byte
// format was determined) and verifies readToManyTargets resolves the
// forward-direction links only, ignoring the auto-maintained backward ones.
import 'dart:io';
import 'dart:typed_data';

import 'package:dart_lmdb2/lmdb.dart';
import 'package:ob_dump_reader/ob_dump_reader.dart';
import 'package:test/test.dart';

List<int> _relationKey(int relationId, int direction, int sourceId, int targetId) {
  final key = Uint8List(12);
  key[0] = 0x08; // type: relation link
  key[3] = (relationId << 2) | direction;
  key.buffer.asByteData().setUint32(4, sourceId, Endian.big);
  key.buffer.asByteData().setUint32(8, targetId, Endian.big);
  return key;
}

Future<void> main() async {
  test('resolves forward-direction ToMany links, ignoring backward ones', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    final db = LMDB();
    await db.init(srcDir.path, config: LMDBInitConfig(maxDbs: 4, mapSize: 64 * 1024 * 1024));

    final txn = await db.txnStart();
    final cursor = await db.cursorOpen(txn);
    // Book#1 --relation 1 (forward)--> Author#1, Author#2.
    await db.cursorPut(cursor, _relationKey(1, 0, 1, 1), const [], 0);
    await db.cursorPut(cursor, _relationKey(1, 0, 1, 2), const [], 0);
    // Auto-maintained backward links (Author -> Book) — must not leak in.
    await db.cursorPut(cursor, _relationKey(1, 2, 1, 1), const [], 0);
    await db.cursorPut(cursor, _relationKey(1, 2, 2, 1), const [], 0);
    // A second relation on the same source id — must not leak in either.
    await db.cursorPut(cursor, _relationKey(2, 0, 1, 99), const [], 0);
    db.cursorClose(cursor);
    await db.txnCommit(txn);
    db.close();

    final targets = await readToManyTargets(srcDir.path, 1, 1);
    expect(targets, [1, 2]);
  });

  test('returns an empty list when the source object has no links', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    final db = LMDB();
    await db.init(srcDir.path, config: LMDBInitConfig(maxDbs: 4, mapSize: 64 * 1024 * 1024));
    final txn = await db.txnStart();
    final cursor = await db.cursorOpen(txn);
    await db.cursorPut(cursor, _relationKey(1, 0, 1, 1), const [], 0);
    db.cursorClose(cursor);
    await db.txnCommit(txn);
    db.close();

    final targets = await readToManyTargets(srcDir.path, 1, 2);
    expect(targets, isEmpty);
  });

  test('readToManyTargetsUnsafe reads the directory directly', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    final db = LMDB();
    await db.init(srcDir.path, config: LMDBInitConfig(maxDbs: 4, mapSize: 64 * 1024 * 1024));
    final txn = await db.txnStart();
    final cursor = await db.cursorOpen(txn);
    await db.cursorPut(cursor, _relationKey(1, 0, 5, 7), const [], 0);
    db.cursorClose(cursor);
    await db.txnCommit(txn);
    db.close();

    final targets = await readToManyTargetsUnsafe(srcDir.path, 1, 5);
    expect(targets, [7]);
  });
}
