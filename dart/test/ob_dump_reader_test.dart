// Builds a small synthetic LMDB store using ObjectBox's exact key format
// (rather than depending on any real ObjectBox database, so this test is
// hermetic and portable) and verifies readObjectBoxRecords() extracts
// records correctly, ignores non-data keys, and cleans up its temp copy.
import 'dart:io';
import 'dart:typed_data';

import 'package:dart_lmdb2/lmdb.dart';
import 'package:ob_dump_reader/ob_dump_reader.dart';
import 'package:test/test.dart';

List<int> _dataKey(int entityId, int objectId) {
  final key = Uint8List(8);
  key[0] = 0x18; // type: object data
  key[3] = entityId * 4;
  key.buffer.asByteData().setUint32(4, objectId, Endian.big);
  return key;
}

List<int> _schemaKey(int entityId) {
  final key = Uint8List(8);
  key[0] = 0x00; // type: schema entry
  key[7] = entityId;
  return key;
}

Future<void> main() async {
  test('extracts data records and ignores non-data keys', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    final db = LMDB();
    await db.init(srcDir.path, config: LMDBInitConfig(maxDbs: 4, mapSize: 64 * 1024 * 1024));

    final txn = await db.txnStart();
    final cursor = await db.cursorOpen(txn);
    // Two Ammo (entityId=1) records and one Weapon (entityId=2) record.
    await db.cursorPut(cursor, _dataKey(1, 1), [1, 2, 3], 0);
    await db.cursorPut(cursor, _dataKey(1, 2), [4, 5, 6, 7], 0);
    await db.cursorPut(cursor, _dataKey(2, 1), [9], 0);
    // A schema entry — must NOT be surfaced as a record.
    await db.cursorPut(cursor, _schemaKey(1), [0xAA], 0);
    db.cursorClose(cursor);
    await db.txnCommit(txn);
    db.close();

    final found = <ObRecord>[];
    await readObjectBoxRecords(srcDir.path, found.add);

    found.sort((a, b) {
      final byEntity = a.entityId.compareTo(b.entityId);
      return byEntity != 0 ? byEntity : a.objectId.compareTo(b.objectId);
    });

    expect(found, hasLength(3));

    expect(found[0].entityId, 1);
    expect(found[0].objectId, 1);
    expect(found[0].data, [1, 2, 3]);

    expect(found[1].entityId, 1);
    expect(found[1].objectId, 2);
    expect(found[1].data, [4, 5, 6, 7]);

    expect(found[2].entityId, 2);
    expect(found[2].objectId, 1);
    expect(found[2].data, [9]);
  });

  test('does not mutate or delete the source directory', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    final db = LMDB();
    await db.init(srcDir.path, config: LMDBInitConfig(maxDbs: 4, mapSize: 64 * 1024 * 1024));
    final txn = await db.txnStart();
    final cursor = await db.cursorOpen(txn);
    await db.cursorPut(cursor, _dataKey(1, 1), [42], 0);
    db.cursorClose(cursor);
    await db.txnCommit(txn);
    db.close();

    final dataFile = File('${srcDir.path}/data.mdb');
    final sizeBefore = dataFile.statSync().size;

    await readObjectBoxRecords(srcDir.path, (_) {});

    expect(dataFile.existsSync(), isTrue);
    expect(dataFile.statSync().size, sizeBefore);
  });

  test('readObjectBoxRecordsUnsafe reads the directory directly', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    final db = LMDB();
    await db.init(srcDir.path, config: LMDBInitConfig(maxDbs: 4, mapSize: 64 * 1024 * 1024));
    final txn = await db.txnStart();
    final cursor = await db.cursorOpen(txn);
    await db.cursorPut(cursor, _dataKey(1, 1), [7, 8, 9], 0);
    db.cursorClose(cursor);
    await db.txnCommit(txn);
    db.close();

    final found = <ObRecord>[];
    await readObjectBoxRecordsUnsafe(srcDir.path, found.add);

    expect(found, hasLength(1));
    expect(found[0].entityId, 1);
    expect(found[0].objectId, 1);
    expect(found[0].data, [7, 8, 9]);
  });
}
