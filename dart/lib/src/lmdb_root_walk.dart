/// Low-level, read-only LMDB access to the root/unnamed database — the one
/// thing this package's public API needs. Opens the *environment* itself as
/// `MDB_RDONLY` (not just the transaction), so reading is safe alongside a
/// concurrently running writer process by LMDB's own MVCC design — no
/// write transaction of any kind is needed, unlike `dart_lmdb2`'s wrapper
/// (which required one purely to register the root dbi handle). Matches
/// ob-dump's own C++ `LmdbReader` exactly: no copy-to-temp/"unsafe" variant
/// distinction needed either, since reading in place was never actually
/// unsafe once the environment itself is opened read-only.
library;

import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'lmdb_bindings.dart';
import 'lmdb_native_library.dart';

class LmdbException implements Exception {
  final String message;
  LmdbException(this.message);
  @override
  String toString() => 'LmdbException: $message';
}

final LmdbBindings _bindings = LmdbBindings(loadLmdbLibrary());

String _strerror(int code) {
  final ptr = _bindings.mdbStrError(code);
  return ptr == nullptr ? 'error $code' : ptr.cast<Utf8>().toDartString();
}

void _check(int rc, String what) {
  if (rc != mdbSuccess) {
    throw LmdbException('$what: ${_strerror(rc)}');
  }
}

Uint8List _valToBytes(MDBVal val) {
  if (val.mvSize == 0) return Uint8List(0);
  return val.mvData.cast<Uint8>().asTypedList(val.mvSize).sublist(0);
}

/// Opens [path] read-only and calls [onEntry] once per key/value pair in
/// the root database, in key order, stopping early if it returns `false`.
/// Always cleans up (cursor/txn/env) even if [onEntry] throws.
void forEachRootEntry(
  String path,
  bool Function(Uint8List key, Uint8List value) onEntry,
) {
  final b = _bindings;
  final envPtrPtr = calloc<Pointer<MDBEnv>>();
  try {
    _check(b.mdbEnvCreate(envPtrPtr), 'mdb_env_create');
    final env = envPtrPtr.value;
    try {
      _check(b.mdbEnvSetMaxdbs(env, 1), 'mdb_env_set_maxdbs');
      _check(b.mdbEnvSetMapsize(env, 512 * 1024 * 1024), 'mdb_env_set_mapsize');

      final isFile = FileSystemEntity.isFileSync(path);
      final flags = mdbRdOnly | (isFile ? mdbNoSubdir : 0);
      final pathPtr = path.toNativeUtf8();
      try {
        final rc = b.mdbEnvOpen(env, pathPtr.cast(), flags, 0x1a4 /* 0644 */);
        if (rc != mdbSuccess) {
          throw LmdbException('mdb_env_open($path): ${_strerror(rc)}');
        }
      } finally {
        calloc.free(pathPtr);
      }

      final txnPtrPtr = calloc<Pointer<MDBTxn>>();
      try {
        _check(b.mdbTxnBegin(env, nullptr, mdbRdOnly, txnPtrPtr), 'mdb_txn_begin');
        final txn = txnPtrPtr.value;
        try {
          final dbiPtr = calloc<Uint32>();
          final int dbi;
          try {
            _check(b.mdbDbiOpen(txn, nullptr, 0, dbiPtr), 'mdb_dbi_open');
            dbi = dbiPtr.value;
          } finally {
            calloc.free(dbiPtr);
          }

          final cursorPtrPtr = calloc<Pointer<MDBCursor>>();
          try {
            _check(b.mdbCursorOpen(txn, dbi, cursorPtrPtr), 'mdb_cursor_open');
            final cursor = cursorPtrPtr.value;
            try {
              final keyVal = calloc<MDBVal>();
              final dataVal = calloc<MDBVal>();
              try {
                var rc = b.mdbCursorGet(cursor, keyVal, dataVal, mdbFirst);
                while (rc == mdbSuccess) {
                  final key = _valToBytes(keyVal.ref);
                  final value = _valToBytes(dataVal.ref);
                  if (!onEntry(key, value)) break;
                  rc = b.mdbCursorGet(cursor, keyVal, dataVal, mdbNext);
                }
                if (rc != mdbSuccess && rc != mdbNotFound) {
                  throw LmdbException('mdb_cursor_get: ${_strerror(rc)}');
                }
              } finally {
                calloc.free(keyVal);
                calloc.free(dataVal);
              }
            } finally {
              b.mdbCursorClose(cursor);
            }
          } finally {
            calloc.free(cursorPtrPtr);
          }
        } finally {
          b.mdbTxnAbort(txn);
        }
      } finally {
        calloc.free(txnPtrPtr);
      }
    } finally {
      b.mdbEnvClose(env);
    }
  } finally {
    calloc.free(envPtrPtr);
  }
}

/// Opens [path] read-only and returns every target object id linked from
/// [sourceObjectId] via `ToMany` relation [relationId] (forward direction —
/// see docs/BACKLOG.md "ToMany relations" for the key format this walks).
List<int> readRelationTargets(String path, int relationId, int sourceObjectId) {
  final b = _bindings;
  final targets = <int>[];

  final envPtrPtr = calloc<Pointer<MDBEnv>>();
  try {
    _check(b.mdbEnvCreate(envPtrPtr), 'mdb_env_create');
    final env = envPtrPtr.value;
    try {
      _check(b.mdbEnvSetMaxdbs(env, 1), 'mdb_env_set_maxdbs');
      _check(b.mdbEnvSetMapsize(env, 512 * 1024 * 1024), 'mdb_env_set_mapsize');

      final isFile = FileSystemEntity.isFileSync(path);
      final flags = mdbRdOnly | (isFile ? mdbNoSubdir : 0);
      final pathPtr = path.toNativeUtf8();
      try {
        final rc = b.mdbEnvOpen(env, pathPtr.cast(), flags, 0x1a4);
        if (rc != mdbSuccess) {
          throw LmdbException('mdb_env_open($path): ${_strerror(rc)}');
        }
      } finally {
        calloc.free(pathPtr);
      }

      final txnPtrPtr = calloc<Pointer<MDBTxn>>();
      try {
        _check(b.mdbTxnBegin(env, nullptr, mdbRdOnly, txnPtrPtr), 'mdb_txn_begin');
        final txn = txnPtrPtr.value;
        try {
          final dbiPtr = calloc<Uint32>();
          final int dbi;
          try {
            _check(b.mdbDbiOpen(txn, nullptr, 0, dbiPtr), 'mdb_dbi_open');
            dbi = dbiPtr.value;
          } finally {
            calloc.free(dbiPtr);
          }

          final cursorPtrPtr = calloc<Pointer<MDBCursor>>();
          try {
            _check(b.mdbCursorOpen(txn, dbi, cursorPtrPtr), 'mdb_cursor_open');
            final cursor = cursorPtrPtr.value;
            try {
              // 12-byte key: [0x08][0x00][0x00][(relId<<2)|dir][srcId:BE][tgtId:BE].
              // All links for this (relation, forward, source) share the
              // same 8-byte prefix — LMDB sorts keys byte-for-byte, so seek
              // to the smallest possible key with that prefix (target 0),
              // then walk forward while the prefix still matches.
              final prefix = Uint8List(8);
              prefix[0] = 0x08;
              prefix[3] = ((relationId << 2) | 0) & 0xFF;
              ByteData.view(prefix.buffer).setUint32(4, sourceObjectId, Endian.big);

              final searchKey = Uint8List(12)..setRange(0, 8, prefix);

              final keyVal = calloc<MDBVal>();
              final dataVal = calloc<MDBVal>();
              final searchKeyPtr = calloc<Uint8>(12);
              try {
                searchKeyPtr.asTypedList(12).setAll(0, searchKey);
                keyVal.ref.mvSize = 12;
                keyVal.ref.mvData = searchKeyPtr.cast();

                var rc = b.mdbCursorGet(cursor, keyVal, dataVal, mdbSetRange);
                while (rc == mdbSuccess) {
                  final key = _valToBytes(keyVal.ref);
                  if (key.length != 12 ||
                      !_startsWith(key, prefix)) {
                    break;
                  }
                  targets.add(ByteData.sublistView(key, 8, 12).getUint32(0, Endian.big));
                  rc = b.mdbCursorGet(cursor, keyVal, dataVal, mdbNext);
                }
                if (rc != mdbSuccess && rc != mdbNotFound) {
                  throw LmdbException('mdb_cursor_get: ${_strerror(rc)}');
                }
              } finally {
                calloc.free(keyVal);
                calloc.free(dataVal);
                calloc.free(searchKeyPtr);
              }
            } finally {
              b.mdbCursorClose(cursor);
            }
          } finally {
            calloc.free(cursorPtrPtr);
          }
        } finally {
          b.mdbTxnAbort(txn);
        }
      } finally {
        calloc.free(txnPtrPtr);
      }
    } finally {
      b.mdbEnvClose(env);
    }
  } finally {
    calloc.free(envPtrPtr);
  }

  return targets;
}

bool _startsWith(Uint8List data, Uint8List prefix) {
  for (var i = 0; i < prefix.length; i++) {
    if (data[i] != prefix[i]) return false;
  }
  return true;
}
