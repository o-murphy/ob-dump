/// Reads ObjectBox `ToMany` relation links directly from LMDB.
///
/// `ToMany` relations live in a separate LMDB key structure, not in the
/// FlatBuffers table [readObjectBoxRecords] hands you — so `flatc --dart`
/// output has no accessor for them at all, regardless of how the rest of an
/// entity is decoded. See ob-dump's `docs/BACKLOG.md` ("ToMany relations")
/// for the confirmed key format this mirrors (determined the same way as
/// everything else in this package: built a real ObjectBox-Dart project and
/// inspected the resulting `data.mdb` directly).
library;

import 'dart:io';

import 'package:dart_lmdb2/lmdb.dart';

// 12-byte key: [0x08][0x00][0x00][(relation_id << 2) | direction][source_id:u32 BE][target_id:u32 BE].
// Value is always empty — the key alone is the link.
const int _keyTypeRelation = 0x08;
const int _relationDirectionForward = 0;

/// Every target object id linked from [sourceObjectId] via `ToMany`
/// relation [relationId] (forward direction — the declared direction,
/// owning entity's id -> target entity's id). Both `relationId` and
/// `sourceObjectId` come from your own model: `ob_dump --schema` lists each
/// entity's relations (id, name, target entity); `sourceObjectId` is the
/// owning object's id, same as [ObRecord.objectId].
///
/// Opens the store the same safe way as [readObjectBoxRecords] — a
/// temporary copy, never the original files directly. For a large number of
/// lookups against the same store, prefer [readToManyTargetsUnsafe] and
/// reuse one directory copy yourself rather than copying per call.
Future<List<int>> readToManyTargets(
  String objectboxDir,
  int relationId,
  int sourceObjectId,
) {
  return _readToManyTargets(
    objectboxDir,
    relationId,
    sourceObjectId,
    copyToTemp: true,
  );
}

/// Same as [readToManyTargets], but skips the safety copy and opens
/// [objectboxDir] directly — see [readObjectBoxRecordsUnsafe] for when
/// that's safe (same caveats apply here).
Future<List<int>> readToManyTargetsUnsafe(
  String objectboxDir,
  int relationId,
  int sourceObjectId,
) {
  return _readToManyTargets(
    objectboxDir,
    relationId,
    sourceObjectId,
    copyToTemp: false,
  );
}

Future<List<int>> _readToManyTargets(
  String objectboxDir,
  int relationId,
  int sourceObjectId, {
  required bool copyToTemp,
}) async {
  final tmp = copyToTemp
      ? Directory.systemTemp.createTempSync('ob_dump_reader_')
      : null;
  final dbDir = tmp?.path ?? objectboxDir;

  try {
    if (tmp != null) {
      for (final name in ['data.mdb', 'lock.mdb']) {
        final src = File('$objectboxDir/$name');
        if (src.existsSync()) src.copySync('${tmp.path}/$name');
      }
    }

    final db = LMDB();
    await db.init(
      dbDir,
      config: LMDBInitConfig(maxDbs: 4, mapSize: 512 * 1024 * 1024),
    );

    final regTxn = await db.txnStart();
    await db.cursorOpen(regTxn);
    await db.txnCommit(regTxn);

    final txn = await db.txnStart(flags: LMDBFlagSet()..add(MDB_RDONLY));
    final cursor = await db.cursorOpen(txn);

    // All links for this (relation, direction, source) share the same
    // 8-byte prefix and vary only in the trailing target id — LMDB sorts
    // keys byte-for-byte, so they're contiguous. Seek to the smallest
    // possible key with that prefix (target id 0), then walk forward while
    // the prefix still matches.
    final prefix = <int>[
      _keyTypeRelation,
      0x00,
      0x00,
      ((relationId << 2) | _relationDirectionForward) & 0xFF,
      ..._uint32BE(sourceObjectId),
    ];
    final searchKey = <int>[...prefix, ..._uint32BE(0)];

    final targets = <int>[];
    var row = await db.cursorGet(cursor, searchKey, CursorOp.setRange);
    while (row != null) {
      final key = row.key;
      if (key.length != 12 || !_startsWith(key, prefix)) break;
      targets.add(_readUint32BE(key, 8));
      row = await db.cursorGet(cursor, null, CursorOp.next);
    }

    db.cursorClose(cursor);
    await db.txnAbort(txn);
    db.close();

    return targets;
  } finally {
    tmp?.deleteSync(recursive: true);
  }
}

List<int> _uint32BE(int v) => [
  (v >> 24) & 0xFF,
  (v >> 16) & 0xFF,
  (v >> 8) & 0xFF,
  v & 0xFF,
];

int _readUint32BE(List<int> bytes, int offset) {
  return (bytes[offset] << 24) |
      (bytes[offset + 1] << 16) |
      (bytes[offset + 2] << 8) |
      bytes[offset + 3];
}

bool _startsWith(List<int> key, List<int> prefix) {
  for (var i = 0; i < prefix.length; i++) {
    if (key[i] != prefix[i]) return false;
  }
  return true;
}
