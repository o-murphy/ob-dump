/* MSVC only: export the LMDB functions the Dart bindings call (see
 * lib/src/lmdb_bindings.dart). mdb.c has no __declspec(dllexport), so a DLL
 * built from it would otherwise export nothing ("Failed to lookup symbol
 * mdb_env_create"). hook/build.dart fails the build if one is missing. */
#ifdef _MSC_VER
#pragma comment(linker, "/EXPORT:mdb_env_create")
#pragma comment(linker, "/EXPORT:mdb_env_set_mapsize")
#pragma comment(linker, "/EXPORT:mdb_env_set_maxdbs")
#pragma comment(linker, "/EXPORT:mdb_env_open")
#pragma comment(linker, "/EXPORT:mdb_env_close")
#pragma comment(linker, "/EXPORT:mdb_txn_begin")
#pragma comment(linker, "/EXPORT:mdb_txn_commit")
#pragma comment(linker, "/EXPORT:mdb_txn_abort")
#pragma comment(linker, "/EXPORT:mdb_dbi_open")
#pragma comment(linker, "/EXPORT:mdb_put")
#pragma comment(linker, "/EXPORT:mdb_cursor_open")
#pragma comment(linker, "/EXPORT:mdb_cursor_close")
#pragma comment(linker, "/EXPORT:mdb_cursor_get")
#else
/* An empty translation unit is not valid ISO C. */
typedef int ob_dump_reader_exports_unused;
#endif
