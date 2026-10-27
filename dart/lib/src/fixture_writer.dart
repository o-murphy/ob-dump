/// LMDB fixture-writing helper -- not exported from `ob_dump_reader.dart`, used
/// only by this package's tests and by `example/` (a Flutter app that CI builds
/// and runs on every platform, which needs a store to read). Uses this
/// package's own FFI bindings directly to `mdb_put` raw key/value pairs,
/// matching ObjectBox's exact key format.
library;

import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'lmdb_bindings.dart';
import 'lmdb_root_walk.dart' show ensureLmdbLoaded;

void _check(int rc, String what) {
  if (rc != mdbSuccess) {
    final msg = mdbStrError(rc);
    throw StateError('$what failed: ${msg.cast<Utf8>().toDartString()}');
  }
}

/// Creates a fresh LMDB store at [dir] (must already exist as a directory)
/// with each `key -> value` pair in [entries] put into the root database.
void writeFixture(String dir, Map<List<int>, List<int>> entries) {
  ensureLmdbLoaded();
  final envPtrPtr = calloc<Pointer<MDBEnv>>();
  final env = () {
    _check(mdbEnvCreate(envPtrPtr), 'mdb_env_create');
    return envPtrPtr.value;
  }();
  calloc.free(envPtrPtr);

  _check(mdbEnvSetMaxdbs(env, 1), 'mdb_env_set_maxdbs');
  _check(mdbEnvSetMapsize(env, 64 * 1024 * 1024), 'mdb_env_set_mapsize');

  final pathPtr = dir.toNativeUtf8();
  final openRc = mdbEnvOpen(env, pathPtr.cast(), 0, 0x1a4);
  calloc.free(pathPtr);
  _check(openRc, 'mdb_env_open');

  final txnPtrPtr = calloc<Pointer<MDBTxn>>();
  _check(mdbTxnBegin(env, nullptr, 0, txnPtrPtr), 'mdb_txn_begin');
  final txn = txnPtrPtr.value;
  calloc.free(txnPtrPtr);

  final dbiPtr = calloc<Uint32>();
  _check(mdbDbiOpen(txn, nullptr, 0, dbiPtr), 'mdb_dbi_open');
  final dbi = dbiPtr.value;
  calloc.free(dbiPtr);

  for (final entry in entries.entries) {
    final keyBytes = Uint8List.fromList(entry.key);
    final valBytes = Uint8List.fromList(entry.value);

    final keyPtr = calloc<Uint8>(keyBytes.isNotEmpty ? keyBytes.length : 1);
    final valPtr = calloc<Uint8>(valBytes.isNotEmpty ? valBytes.length : 1);
    keyPtr.asTypedList(keyBytes.length).setAll(0, keyBytes);
    valPtr.asTypedList(valBytes.length).setAll(0, valBytes);

    final keyVal = calloc<MDBVal>();
    final dataVal = calloc<MDBVal>();
    keyVal.ref.mvSize = keyBytes.length;
    keyVal.ref.mvData = keyPtr.cast();
    dataVal.ref.mvSize = valBytes.length;
    dataVal.ref.mvData = valPtr.cast();

    _check(mdbPut(txn, dbi, keyVal, dataVal, 0), 'mdb_put');

    calloc.free(keyVal);
    calloc.free(dataVal);
    calloc.free(keyPtr);
    calloc.free(valPtr);
  }

  _check(mdbTxnCommit(txn), 'mdb_txn_commit');
  mdbEnvClose(env);
}

List<int> dataKey(int entityId, int objectId) {
  final key = Uint8List(8);
  key[0] = 0x18; // type: object data
  key[3] = entityId * 4;
  key.buffer.asByteData().setUint32(4, objectId, Endian.big);
  return key;
}

List<int> schemaKey(int entityId) {
  final key = Uint8List(8);
  key[0] = 0x00; // type: schema entry
  key[7] = entityId;
  return key;
}

List<int> relationKey(
  int relationId,
  int direction,
  int sourceId,
  int targetId,
) {
  final key = Uint8List(12);
  key[0] = 0x08; // type: relation link
  key[3] = (relationId << 2) | direction;
  key.buffer.asByteData().setUint32(4, sourceId, Endian.big);
  key.buffer.asByteData().setUint32(8, targetId, Endian.big);
  return key;
}
