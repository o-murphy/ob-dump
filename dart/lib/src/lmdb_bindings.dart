/// Minimal `dart:ffi` bindings to the subset of LMDB's C API this package
/// actually needs: open an environment read-only, walk the root/unnamed
/// database with a cursor. Plus `mdb_put`/`mdb_txn_commit`, used only by
/// this package's own tests to build fixture databases.
///
/// Hand-written `@Native` bindings rather than `ffigen`-generated: the surface
/// is small (13 functions) and stable (LMDB's C API hasn't changed shape in
/// over a decade), so a generator dependency isn't worth it. They resolve
/// against the code asset `hook/build.dart` compiles from the vendored LMDB
/// source (asset id `package:ob_dump_reader/src/lmdb_bindings.dart`), the
/// same way for `dart run`, `dart test`, `flutter run`/`build`/`test` and AOT
/// release builds. Struct layout and
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
@ffi.Native<ffi.Int32 Function(ffi.Pointer<ffi.Pointer<MDBEnv>> env)>(
  symbol: 'mdb_env_create',
)
external int mdbEnvCreate(ffi.Pointer<ffi.Pointer<MDBEnv>> env);

// mdb_env_set_mapsize(MDB_env *env, size_t size)
@ffi.Native<ffi.Int32 Function(ffi.Pointer<MDBEnv> env, ffi.Size size)>(
  symbol: 'mdb_env_set_mapsize',
)
external int mdbEnvSetMapsize(ffi.Pointer<MDBEnv> env, int size);

// mdb_env_set_maxdbs(MDB_env *env, MDB_dbi dbs)
@ffi.Native<ffi.Int32 Function(ffi.Pointer<MDBEnv> env, ffi.Uint32 dbs)>(
  symbol: 'mdb_env_set_maxdbs',
)
external int mdbEnvSetMaxdbs(ffi.Pointer<MDBEnv> env, int dbs);

// mdb_env_open(MDB_env *env, const char *path, unsigned int flags, mdb_mode_t mode)
@ffi.Native<
  ffi.Int32 Function(
    ffi.Pointer<MDBEnv> env,
    ffi.Pointer<ffi.Char> path,
    ffi.Uint32 flags,
    ffi.Int32 mode,
  )
>(symbol: 'mdb_env_open')
external int mdbEnvOpen(
  ffi.Pointer<MDBEnv> env,
  ffi.Pointer<ffi.Char> path,
  int flags,
  int mode,
);

// void mdb_env_close(MDB_env *env)
@ffi.Native<ffi.Void Function(ffi.Pointer<MDBEnv> env)>(symbol: 'mdb_env_close')
external void mdbEnvClose(ffi.Pointer<MDBEnv> env);

// mdb_txn_begin(MDB_env *env, MDB_txn *parent, unsigned int flags, MDB_txn **txn)
@ffi.Native<
  ffi.Int32 Function(
    ffi.Pointer<MDBEnv> env,
    ffi.Pointer<MDBTxn> parent,
    ffi.Uint32 flags,
    ffi.Pointer<ffi.Pointer<MDBTxn>> txn,
  )
>(symbol: 'mdb_txn_begin')
external int mdbTxnBegin(
  ffi.Pointer<MDBEnv> env,
  ffi.Pointer<MDBTxn> parent,
  int flags,
  ffi.Pointer<ffi.Pointer<MDBTxn>> txn,
);

// mdb_txn_commit(MDB_txn *txn)
@ffi.Native<ffi.Int32 Function(ffi.Pointer<MDBTxn> txn)>(
  symbol: 'mdb_txn_commit',
)
external int mdbTxnCommit(ffi.Pointer<MDBTxn> txn);

// void mdb_txn_abort(MDB_txn *txn)
@ffi.Native<ffi.Void Function(ffi.Pointer<MDBTxn> txn)>(symbol: 'mdb_txn_abort')
external void mdbTxnAbort(ffi.Pointer<MDBTxn> txn);

// mdb_dbi_open(MDB_txn *txn, const char *name, unsigned int flags, MDB_dbi *dbi)
@ffi.Native<
  ffi.Int32 Function(
    ffi.Pointer<MDBTxn> txn,
    ffi.Pointer<ffi.Char> name,
    ffi.Uint32 flags,
    ffi.Pointer<ffi.Uint32> dbi,
  )
>(symbol: 'mdb_dbi_open')
external int mdbDbiOpen(
  ffi.Pointer<MDBTxn> txn,
  ffi.Pointer<ffi.Char> name,
  int flags,
  ffi.Pointer<ffi.Uint32> dbi,
);

// mdb_put(MDB_txn *txn, MDB_dbi dbi, MDB_val *key, MDB_val *data, unsigned int flags)
@ffi.Native<
  ffi.Int32 Function(
    ffi.Pointer<MDBTxn> txn,
    ffi.Uint32 dbi,
    ffi.Pointer<MDBVal> key,
    ffi.Pointer<MDBVal> data,
    ffi.Uint32 flags,
  )
>(symbol: 'mdb_put')
external int mdbPut(
  ffi.Pointer<MDBTxn> txn,
  int dbi,
  ffi.Pointer<MDBVal> key,
  ffi.Pointer<MDBVal> data,
  int flags,
);

// mdb_cursor_open(MDB_txn *txn, MDB_dbi dbi, MDB_cursor **cursor)
@ffi.Native<
  ffi.Int32 Function(
    ffi.Pointer<MDBTxn> txn,
    ffi.Uint32 dbi,
    ffi.Pointer<ffi.Pointer<MDBCursor>> cursor,
  )
>(symbol: 'mdb_cursor_open')
external int mdbCursorOpen(
  ffi.Pointer<MDBTxn> txn,
  int dbi,
  ffi.Pointer<ffi.Pointer<MDBCursor>> cursor,
);

// void mdb_cursor_close(MDB_cursor *cursor)
@ffi.Native<ffi.Void Function(ffi.Pointer<MDBCursor> cursor)>(
  symbol: 'mdb_cursor_close',
)
external void mdbCursorClose(ffi.Pointer<MDBCursor> cursor);

// mdb_cursor_get(MDB_cursor *cursor, MDB_val *key, MDB_val *data, MDB_cursor_op op)
@ffi.Native<
  ffi.Int32 Function(
    ffi.Pointer<MDBCursor> cursor,
    ffi.Pointer<MDBVal> key,
    ffi.Pointer<MDBVal> data,
    ffi.Int32 op,
  )
>(symbol: 'mdb_cursor_get')
external int mdbCursorGet(
  ffi.Pointer<MDBCursor> cursor,
  ffi.Pointer<MDBVal> key,
  ffi.Pointer<MDBVal> data,
  int op,
);

// const char *mdb_strerror(int err)
@ffi.Native<ffi.Pointer<ffi.Char> Function(ffi.Int32 err)>(
  symbol: 'mdb_strerror',
)
external ffi.Pointer<ffi.Char> mdbStrError(int err);
