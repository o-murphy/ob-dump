/// Minimal ObjectBox LMDB reader toolkit.
///
/// This package does exactly one thing: walk a `data.mdb` file and hand you
/// each stored object's raw FlatBuffers table bytes, plus its entity id and
/// object id. It knows nothing about any specific entity's fields.
///
/// The intended workflow, pairing this with the `ob_dump` CLI/library
/// (https://github.com/o-murphy/ob-dump):
///
///   1. `ob_dump --fbs objectbox-model.json -o schema.fbs`
///   2. `flatc --dart schema.fbs` (official FlatBuffers compiler, NOT
///      `flatcc` — that's a separate, C-only implementation with no Dart
///      backend) — produces typed Dart classes backed by the `flat_buffers`
///      pub package, no native/FFI code involved.
///   3. `ob_dump --schema objectbox-model.json` gives you the entityId ->
///      entity name/shape mapping needed to know which generated type to
///      use for a given [ObRecord.entityId].
///   4. Use this package's [readObjectBoxRecords] to get each record's raw
///      bytes, dispatch on `entityId`, and decode with the matching
///      `flatc`-generated type — entirely in Dart, no dependency on
///      ob-dump's own C++ core.
///
/// See ob-dump's `docs/BACKLOG.md` ("Schema export") for the exact split of
/// responsibilities and its limits.
///
/// Two things `flatc --dart` output can't handle on its own, also covered
/// by this package:
///   - `ToMany` relations aren't part of the FlatBuffers table at all — use
///     [readToManyTargets].
///   - `Flex` fields and ObjectBox's `ExternalPropertyType` annotation
///     (`Uuid`, `Json`, etc.) decode to raw bytes/strings via `flatc` alone —
///     use [decodeFlex]/[bytesToHex]/[bytesToUuidString]/
///     [tryParseJsonString] on the matching field's value.
library ob_dump_reader;

import 'dart:io';
import 'dart:typed_data';

import 'package:dart_lmdb2/lmdb.dart';

export 'src/decode_helpers.dart';
export 'src/relations.dart';

/// One ObjectBox object-data record: its entity id, its own object id
/// (ObjectBox's primary key), and the raw FlatBuffers table bytes — decode
/// those with whatever `flatc --dart`-generated type matches [entityId]
/// (see `ob_dump --schema` for that mapping).
class ObRecord {
  final int entityId;
  final int objectId;
  final Uint8List data;

  const ObRecord({
    required this.entityId,
    required this.objectId,
    required this.data,
  });
}

// LMDB key format (8 bytes, root/unnamed db only — ObjectBox never uses
// named sub-databases): [type:1][0x00][0x00][entity_id*4:1][object_id:u32 BE].
// type: 0x00 = schema entry, 0x18 = object data, 0x20 = index. See
// ob-dump's docs/BACKLOG.md for how this was determined.
const int _keyTypeData = 0x18;

/// Reads every ObjectBox object-data record from the LMDB store at
/// [objectboxDir] (a directory containing `data.mdb`, optionally
/// `lock.mdb`), invoking [onRecord] once per record.
///
/// Works on a temporary copy of the database, never the original files
/// directly: `dart_lmdb2` needs one write-capable transaction (just to
/// register the root db handle, nothing is actually mutated) to open an
/// ObjectBox store, and doing that against a live database risks colliding
/// with a running ObjectBox process or a read-only source location. This
/// copy costs disk I/O and temporary space proportional to the database
/// size — for a large database where that's unwelcome, and you're sure the
/// source is safe to open directly (nothing else has it open, and it's on
/// writable storage), see [readObjectBoxRecordsUnsafe].
Future<void> readObjectBoxRecords(
  String objectboxDir,
  void Function(ObRecord record) onRecord,
) {
  return _readObjectBoxRecords(objectboxDir, onRecord, copyToTemp: true);
}

/// Same as [readObjectBoxRecords], but skips the safety copy and opens
/// [objectboxDir] directly.
///
/// **Unsafe**: this still opens a write-capable LMDB transaction against
/// `objectboxDir` (see [readObjectBoxRecords] for why — nothing is actually
/// written, but the env must be opened write-capable regardless). Against
/// the *original* files instead of a disposable copy, that means:
/// - if anything else (e.g. a running ObjectBox-using app) has the database
///   open at the same time, you risk lock contention or reading a torn page
///   from a concurrent write;
/// - the location must be on writable storage, or opening will fail outright.
///
/// Only reach for this once you know the source isn't in use by anything
/// else (e.g. the owning app is fully closed) — the payoff is skipping a
/// full copy of `data.mdb`/`lock.mdb`, which matters mainly for large
/// databases.
Future<void> readObjectBoxRecordsUnsafe(
  String objectboxDir,
  void Function(ObRecord record) onRecord,
) {
  return _readObjectBoxRecords(objectboxDir, onRecord, copyToTemp: false);
}

Future<void> _readObjectBoxRecords(
  String objectboxDir,
  void Function(ObRecord record) onRecord, {
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

    // Single write txn to open the root unnamed DB (dart_lmdb2 internals
    // require this — nothing is written, just the dbi handle registration).
    final regTxn = await db.txnStart();
    await db.cursorOpen(regTxn);
    await db.txnCommit(regTxn);

    final txn = await db.txnStart(flags: LMDBFlagSet()..add(MDB_RDONLY));
    final cursor = await db.cursorOpen(txn);

    CursorEntry? row = await db.cursorGet(cursor, null, CursorOp.first);
    while (row != null) {
      final key = row.key;
      if (key.length == 8 && key[0] == _keyTypeData) {
        final entityId = key[3] ~/ 4;
        final objectId = _readUint32BE(key, 4);
        // Copied out of the cursor's buffer — row.data isn't guaranteed
        // valid once the transaction below is aborted.
        onRecord(ObRecord(
          entityId: entityId,
          objectId: objectId,
          data: Uint8List.fromList(row.data),
        ));
      }
      row = await db.cursorGet(cursor, null, CursorOp.next);
    }

    db.cursorClose(cursor);
    await db.txnAbort(txn);
    db.close();
  } finally {
    tmp?.deleteSync(recursive: true);
  }
}

int _readUint32BE(List<int> bytes, int offset) {
  return (bytes[offset] << 24) |
      (bytes[offset + 1] << 16) |
      (bytes[offset + 2] << 8) |
      bytes[offset + 3];
}
