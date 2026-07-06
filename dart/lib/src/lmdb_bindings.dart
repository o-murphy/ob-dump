/// Minimal `dart:ffi` bindings to the subset of LMDB's C API this package
/// actually needs: open an environment read-only, walk the root/unnamed
/// database with a cursor. Plus `mdb_put`/`mdb_txn_commit`, used only by
/// this package's own tests to build fixture databases.
///
/// Hand-written rather than `ffigen`-generated: the surface is small (13
/// functions) and stable (LMDB's C API hasn't changed shape in over a
/// decade), so a generator dependency isn't worth it. Struct layout and
/// function signatures are taken directly from the vendored `src/lmdb.h`
/// (LMDB_0.9.31) — not guessed.
library;

import 'dart:ffi' as ffi;

// Opaque handle types — LMDB never exposes their layout, only pointers to
// them, so these have no accessible fields on the Dart side either.
final class MDBEnv extends ffi.Opaque {}

final class MDBTxn extends ffi.Opaque {}

final class MDBCursor extends ffi.Opaque {}

/// `mdb_dbi_open`'s handle type — a plain `unsigned int`, not a pointer.
typedef MDBDbi = ffi.UnsignedInt;

/// Mirrors `MDB_val` from lmdb.h exactly: `{ size_t mv_size; void *mv_data; }`.
final class MDBVal extends ffi.Struct {
  @ffi.Size()
  external int mvSize;

  external ffi.Pointer<ffi.Void> mvData;
}

// Env flags (lmdb.h) — only the ones this package sets.
const int mdbRdOnly = 0x20000;
const int mdbNoSubdir = 0x4000;

// mdb_cursor_get ops (MDB_cursor_op enum, lmdb.h) — ordinal values, not
// re-declared as a Dart enum since they cross the FFI boundary as plain ints.
const int mdbFirst = 0;
const int mdbNext = 8;
const int mdbSetRange = 17;

const int mdbSuccess = 0;
const int mdbNotFound = -30798;

// mdb_env_create(MDB_env **env)
typedef _MdbEnvCreateNative = ffi.Int32 Function(
    ffi.Pointer<ffi.Pointer<MDBEnv>> env);
typedef _MdbEnvCreateDart = int Function(ffi.Pointer<ffi.Pointer<MDBEnv>> env);

// mdb_env_set_mapsize(MDB_env *env, size_t size)
typedef _MdbEnvSetMapsizeNative = ffi.Int32 Function(
    ffi.Pointer<MDBEnv> env, ffi.Size size);
typedef _MdbEnvSetMapsizeDart = int Function(
    ffi.Pointer<MDBEnv> env, int size);

// mdb_env_set_maxdbs(MDB_env *env, MDB_dbi dbs)
typedef _MdbEnvSetMaxdbsNative = ffi.Int32 Function(
    ffi.Pointer<MDBEnv> env, ffi.Uint32 dbs);
typedef _MdbEnvSetMaxdbsDart = int Function(ffi.Pointer<MDBEnv> env, int dbs);

// mdb_env_open(MDB_env *env, const char *path, unsigned int flags, mdb_mode_t mode)
typedef _MdbEnvOpenNative = ffi.Int32 Function(ffi.Pointer<MDBEnv> env,
    ffi.Pointer<ffi.Char> path, ffi.Uint32 flags, ffi.Int32 mode);
typedef _MdbEnvOpenDart = int Function(ffi.Pointer<MDBEnv> env,
    ffi.Pointer<ffi.Char> path, int flags, int mode);

// void mdb_env_close(MDB_env *env)
typedef _MdbEnvCloseNative = ffi.Void Function(ffi.Pointer<MDBEnv> env);
typedef _MdbEnvCloseDart = void Function(ffi.Pointer<MDBEnv> env);

// mdb_txn_begin(MDB_env *env, MDB_txn *parent, unsigned int flags, MDB_txn **txn)
typedef _MdbTxnBeginNative = ffi.Int32 Function(
    ffi.Pointer<MDBEnv> env,
    ffi.Pointer<MDBTxn> parent,
    ffi.Uint32 flags,
    ffi.Pointer<ffi.Pointer<MDBTxn>> txn);
typedef _MdbTxnBeginDart = int Function(
    ffi.Pointer<MDBEnv> env,
    ffi.Pointer<MDBTxn> parent,
    int flags,
    ffi.Pointer<ffi.Pointer<MDBTxn>> txn);

// mdb_txn_commit(MDB_txn *txn)
typedef _MdbTxnCommitNative = ffi.Int32 Function(ffi.Pointer<MDBTxn> txn);
typedef _MdbTxnCommitDart = int Function(ffi.Pointer<MDBTxn> txn);

// void mdb_txn_abort(MDB_txn *txn)
typedef _MdbTxnAbortNative = ffi.Void Function(ffi.Pointer<MDBTxn> txn);
typedef _MdbTxnAbortDart = void Function(ffi.Pointer<MDBTxn> txn);

// mdb_dbi_open(MDB_txn *txn, const char *name, unsigned int flags, MDB_dbi *dbi)
typedef _MdbDbiOpenNative = ffi.Int32 Function(
    ffi.Pointer<MDBTxn> txn,
    ffi.Pointer<ffi.Char> name,
    ffi.Uint32 flags,
    ffi.Pointer<ffi.Uint32> dbi);
typedef _MdbDbiOpenDart = int Function(ffi.Pointer<MDBTxn> txn,
    ffi.Pointer<ffi.Char> name, int flags, ffi.Pointer<ffi.Uint32> dbi);

// mdb_put(MDB_txn *txn, MDB_dbi dbi, MDB_val *key, MDB_val *data, unsigned int flags)
typedef _MdbPutNative = ffi.Int32 Function(
    ffi.Pointer<MDBTxn> txn,
    ffi.Uint32 dbi,
    ffi.Pointer<MDBVal> key,
    ffi.Pointer<MDBVal> data,
    ffi.Uint32 flags);
typedef _MdbPutDart = int Function(ffi.Pointer<MDBTxn> txn, int dbi,
    ffi.Pointer<MDBVal> key, ffi.Pointer<MDBVal> data, int flags);

// mdb_cursor_open(MDB_txn *txn, MDB_dbi dbi, MDB_cursor **cursor)
typedef _MdbCursorOpenNative = ffi.Int32 Function(
    ffi.Pointer<MDBTxn> txn,
    ffi.Uint32 dbi,
    ffi.Pointer<ffi.Pointer<MDBCursor>> cursor);
typedef _MdbCursorOpenDart = int Function(ffi.Pointer<MDBTxn> txn, int dbi,
    ffi.Pointer<ffi.Pointer<MDBCursor>> cursor);

// void mdb_cursor_close(MDB_cursor *cursor)
typedef _MdbCursorCloseNative = ffi.Void Function(
    ffi.Pointer<MDBCursor> cursor);
typedef _MdbCursorCloseDart = void Function(ffi.Pointer<MDBCursor> cursor);

// mdb_cursor_get(MDB_cursor *cursor, MDB_val *key, MDB_val *data, MDB_cursor_op op)
typedef _MdbCursorGetNative = ffi.Int32 Function(
    ffi.Pointer<MDBCursor> cursor,
    ffi.Pointer<MDBVal> key,
    ffi.Pointer<MDBVal> data,
    ffi.Int32 op);
typedef _MdbCursorGetDart = int Function(ffi.Pointer<MDBCursor> cursor,
    ffi.Pointer<MDBVal> key, ffi.Pointer<MDBVal> data, int op);

// const char *mdb_strerror(int err)
typedef _MdbStrErrorNative = ffi.Pointer<ffi.Char> Function(ffi.Int32 err);
typedef _MdbStrErrorDart = ffi.Pointer<ffi.Char> Function(int err);

/// Looked-up function pointers, bound once against a loaded [ffi.DynamicLibrary].
class LmdbBindings {
  final ffi.DynamicLibrary _lib;

  late final _MdbEnvCreateDart mdbEnvCreate =
      _lib.lookupFunction<_MdbEnvCreateNative, _MdbEnvCreateDart>('mdb_env_create');
  late final _MdbEnvSetMapsizeDart mdbEnvSetMapsize = _lib
      .lookupFunction<_MdbEnvSetMapsizeNative, _MdbEnvSetMapsizeDart>(
          'mdb_env_set_mapsize');
  late final _MdbEnvSetMaxdbsDart mdbEnvSetMaxdbs = _lib
      .lookupFunction<_MdbEnvSetMaxdbsNative, _MdbEnvSetMaxdbsDart>(
          'mdb_env_set_maxdbs');
  late final _MdbEnvOpenDart mdbEnvOpen =
      _lib.lookupFunction<_MdbEnvOpenNative, _MdbEnvOpenDart>('mdb_env_open');
  late final _MdbEnvCloseDart mdbEnvClose = _lib
      .lookupFunction<_MdbEnvCloseNative, _MdbEnvCloseDart>('mdb_env_close');
  late final _MdbTxnBeginDart mdbTxnBegin = _lib
      .lookupFunction<_MdbTxnBeginNative, _MdbTxnBeginDart>('mdb_txn_begin');
  late final _MdbTxnCommitDart mdbTxnCommit = _lib
      .lookupFunction<_MdbTxnCommitNative, _MdbTxnCommitDart>('mdb_txn_commit');
  late final _MdbTxnAbortDart mdbTxnAbort = _lib
      .lookupFunction<_MdbTxnAbortNative, _MdbTxnAbortDart>('mdb_txn_abort');
  late final _MdbDbiOpenDart mdbDbiOpen =
      _lib.lookupFunction<_MdbDbiOpenNative, _MdbDbiOpenDart>('mdb_dbi_open');
  late final _MdbPutDart mdbPut =
      _lib.lookupFunction<_MdbPutNative, _MdbPutDart>('mdb_put');
  late final _MdbCursorOpenDart mdbCursorOpen = _lib
      .lookupFunction<_MdbCursorOpenNative, _MdbCursorOpenDart>(
          'mdb_cursor_open');
  late final _MdbCursorCloseDart mdbCursorClose = _lib
      .lookupFunction<_MdbCursorCloseNative, _MdbCursorCloseDart>(
          'mdb_cursor_close');
  late final _MdbCursorGetDart mdbCursorGet = _lib
      .lookupFunction<_MdbCursorGetNative, _MdbCursorGetDart>(
          'mdb_cursor_get');
  late final _MdbStrErrorDart mdbStrError = _lib
      .lookupFunction<_MdbStrErrorNative, _MdbStrErrorDart>('mdb_strerror');

  LmdbBindings(this._lib);
}
