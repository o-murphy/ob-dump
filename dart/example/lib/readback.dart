import 'dart:io';

import 'package:ob_dump_reader/ob_dump_reader.dart';
// Test-support code of the package (not part of its public API): writes the
// small LMDB store this app reads back, so no data file has to be bundled.
// ignore: implementation_imports
import 'package:ob_dump_reader/src/fixture_writer.dart';

/// Writes a tiny ObjectBox-shaped LMDB store (two `Ammo` records, one
/// `Weapon` record, one relation link) to a temp directory and reads it back
/// with the package. Returns what was read, as "entity/object=bytes" strings.
///
/// This is the whole point of the app: the native LMDB library compiled by the
/// package's build hook has to be loadable and working in the built app.
Future<List<String>> writeAndReadStore() async {
  final dir = Directory.systemTemp.createTempSync('ob_dump_example_');
  try {
    writeFixture(dir.path, {
      dataKey(1, 1): [1, 2, 3],
      dataKey(1, 2): [4, 5, 6, 7],
      dataKey(2, 1): [9],
      schemaKey(1): [0xAA], // not a record: must be ignored
      relationKey(1, 0, 1, 2): const [],
    });

    final records = <ObRecord>[];
    await readObjectBoxRecords(dir.path, records.add);
    final targets = await readToManyTargets(dir.path, 1, 1);
    if (targets.join(',') != '2') {
      throw StateError('relation link 1 -> 2 was not read back');
    }
    records.sort((a, b) {
      final byEntity = a.entityId.compareTo(b.entityId);
      return byEntity != 0 ? byEntity : a.objectId.compareTo(b.objectId);
    });
    final got = [
      for (final r in records) '${r.entityId}/${r.objectId}=${r.data}',
    ];
    const want = ['1/1=[1, 2, 3]', '1/2=[4, 5, 6, 7]', '2/1=[9]'];
    if (got.join(';') != want.join(';')) {
      throw StateError('read back $got, expected $want');
    }
    return got;
  } finally {
    dir.deleteSync(recursive: true);
  }
}
