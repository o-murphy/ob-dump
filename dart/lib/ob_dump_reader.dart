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

import 'dart:typed_data';

import 'src/lmdb_root_walk.dart';

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
/// Reads the original files directly, in place — no temporary copy. Opens
/// the LMDB *environment* itself read-only (`MDB_RDONLY`, not just the
/// transaction), which is exactly what LMDB's own MVCC design exists for:
/// any number of readers, including in other processes, can safely run
/// alongside one concurrent writer, with zero risk to the original data.
/// (An earlier version of this function copied to a temp directory first —
/// that was only ever needed because of a now-removed dependency that
/// required a write-capable environment just to register the root
/// database handle; nothing was ever actually written.)
///
/// This does synchronous, CPU-bound native work — for a large database on
/// a UI isolate, consider running it via `compute()`/`Isolate.run()`.
Future<void> readObjectBoxRecords(
  String objectboxDir,
  void Function(ObRecord record) onRecord,
) async {
  forEachRootEntry(objectboxDir, (key, value) {
    if (key.length == 8 && key[0] == _keyTypeData) {
      final entityId = key[3] ~/ 4;
      final objectId = _readUint32BE(key, 4);
      onRecord(ObRecord(entityId: entityId, objectId: objectId, data: value));
    }
    return true;
  });
}

int _readUint32BE(List<int> bytes, int offset) {
  return (bytes[offset] << 24) |
      (bytes[offset + 1] << 16) |
      (bytes[offset + 2] << 8) |
      bytes[offset + 3];
}
