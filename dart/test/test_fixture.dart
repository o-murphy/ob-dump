// Shared LMDB fixture-writing helper for tests — uses this package's own
// FFI bindings directly (not a separate dependency) to `mdb_put` raw
// key/value pairs, matching ObjectBox's exact key format.
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';
import 'package:ob_dump_reader/src/lmdb_bindings.dart';
import 'package:ob_dump_reader/src/lmdb_native_library.dart';

final LmdbBindings _bindings = LmdbBindings(loadLmdbLibrary());

void _check(int rc, String what) {
  if (rc != mdbSuccess) {
    final msg = _bindings.mdbStrError(rc);
    throw StateError('$what failed: ${msg.cast<Utf8>().toDartString()}');
  }
}

/// Creates a fresh LMDB store at [dir] (must already exist as a directory)
/// with each `key -> value` pair in [entries] put into the root database.
void writeFixture(String dir, Map<List<int>, List<int>> entries) {
  final envPtrPtr = calloc<Pointer<MDBEnv>>();
  final env = () {
    _check(_bindings.mdbEnvCreate(envPtrPtr), 'mdb_env_create');
    return envPtrPtr.value;
  }();
  calloc.free(envPtrPtr);

  _check(_bindings.mdbEnvSetMaxdbs(env, 1), 'mdb_env_set_maxdbs');
  _check(_bindings.mdbEnvSetMapsize(env, 64 * 1024 * 1024), 'mdb_env_set_mapsize');

  final pathPtr = dir.toNativeUtf8();
  final openRc = _bindings.mdbEnvOpen(env, pathPtr.cast(), 0, 0x1a4);
  calloc.free(pathPtr);
  _check(openRc, 'mdb_env_open');

  final txnPtrPtr = calloc<Pointer<MDBTxn>>();
  _check(_bindings.mdbTxnBegin(env, nullptr, 0, txnPtrPtr), 'mdb_txn_begin');
  final txn = txnPtrPtr.value;
  calloc.free(txnPtrPtr);

  final dbiPtr = calloc<Uint32>();
  _check(_bindings.mdbDbiOpen(txn, nullptr, 0, dbiPtr), 'mdb_dbi_open');
  final dbi = dbiPtr.value;
  calloc.free(dbiPtr);

  for (final entry in entries.entries) {
    final keyBytes = Uint8List.fromList(entry.key);
    final valBytes = Uint8List.fromList(entry.value);

    final keyPtr = calloc<Uint8>(keyBytes.length > 0 ? keyBytes.length : 1);
    final valPtr = calloc<Uint8>(valBytes.length > 0 ? valBytes.length : 1);
    keyPtr.asTypedList(keyBytes.length).setAll(0, keyBytes);
    valPtr.asTypedList(valBytes.length).setAll(0, valBytes);

    final keyVal = calloc<MDBVal>();
    final dataVal = calloc<MDBVal>();
    keyVal.ref.mvSize = keyBytes.length;
    keyVal.ref.mvData = keyPtr.cast();
    dataVal.ref.mvSize = valBytes.length;
    dataVal.ref.mvData = valPtr.cast();

    _check(_bindings.mdbPut(txn, dbi, keyVal, dataVal, 0), 'mdb_put');

    calloc.free(keyVal);
    calloc.free(dataVal);
    calloc.free(keyPtr);
    calloc.free(valPtr);
  }

  _check(_bindings.mdbTxnCommit(txn), 'mdb_txn_commit');
  _bindings.mdbEnvClose(env);
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

List<int> relationKey(int relationId, int direction, int sourceId, int targetId) {
  final key = Uint8List(12);
  key[0] = 0x08; // type: relation link
  key[3] = (relationId << 2) | direction;
  key.buffer.asByteData().setUint32(4, sourceId, Endian.big);
  key.buffer.asByteData().setUint32(8, targetId, Endian.big);
  return key;
}
