/// Reads ObjectBox `ToMany` relation links directly from LMDB.
///
/// `ToMany` relations live in a separate LMDB key structure, not in the
/// FlatBuffers table `readObjectBoxRecords` hands you — so `flatc --dart`
/// output has no accessor for them at all, regardless of how the rest of an
/// entity is decoded. See ob-dump's `docs/BACKLOG.md` ("ToMany relations")
/// for the confirmed key format this mirrors (determined the same way as
/// everything else in this package: built a real ObjectBox-Dart project and
/// inspected the resulting `data.mdb` directly).
library;

import 'lmdb_root_walk.dart';

/// Every target object id linked from [sourceObjectId] via `ToMany`
/// relation [relationId] (forward direction — the declared direction,
/// owning entity's id -> target entity's id). Both `relationId` and
/// `sourceObjectId` come from your own model: `ob_dump --schema` lists each
/// entity's relations (id, name, target entity); `sourceObjectId` is the
/// owning object's id, same as `ObRecord.objectId`.
///
/// Reads [objectboxDir] directly, in place — see `readObjectBoxRecords` for
/// why that's always safe (the environment is opened `MDB_RDONLY`, LMDB's
/// own MVCC design already handles concurrent readers/writers).
Future<List<int>> readToManyTargets(
  String objectboxDir,
  int relationId,
  int sourceObjectId,
) async {
  return readRelationTargets(objectboxDir, relationId, sourceObjectId);
}
