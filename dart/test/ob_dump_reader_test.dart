// Builds a small synthetic LMDB store using ObjectBox's exact key format
// (rather than depending on any real ObjectBox database, so this test is
// hermetic and portable) and verifies readObjectBoxRecords() extracts
// records correctly and ignores non-data keys.
import 'dart:io';

import 'package:ob_dump_reader/ob_dump_reader.dart';
import 'package:test/test.dart';

import 'test_fixture.dart';

Future<void> main() async {
  test('extracts data records and ignores non-data keys', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    writeFixture(srcDir.path, {
      // Two Ammo (entityId=1) records and one Weapon (entityId=2) record.
      dataKey(1, 1): [1, 2, 3],
      dataKey(1, 2): [4, 5, 6, 7],
      dataKey(2, 1): [9],
      // A schema entry — must NOT be surfaced as a record.
      schemaKey(1): [0xAA],
    });

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

    writeFixture(srcDir.path, {dataKey(1, 1): [42]});

    final dataFile = File('${srcDir.path}/data.mdb');
    final sizeBefore = dataFile.statSync().size;

    await readObjectBoxRecords(srcDir.path, (_) {});

    expect(dataFile.existsSync(), isTrue);
    expect(dataFile.statSync().size, sizeBefore);
  });

  test('reads the directory directly, in place, no copy', () async {
    final srcDir = Directory.systemTemp.createTempSync('ob_dump_reader_fixture_');
    addTearDown(() => srcDir.deleteSync(recursive: true));

    writeFixture(srcDir.path, {dataKey(1, 1): [7, 8, 9]});

    final found = <ObRecord>[];
    await readObjectBoxRecords(srcDir.path, found.add);

    expect(found, hasLength(1));
    expect(found[0].entityId, 1);
    expect(found[0].objectId, 1);
    expect(found[0].data, [7, 8, 9]);
  });
}
