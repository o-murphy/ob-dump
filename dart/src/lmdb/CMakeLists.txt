# Builds liblmdb (shared) from the vendored LMDB_0.9.31 source in this
# directory — the exact same source ob-dump's own C++ core builds and has
# verified. No FetchContent, no network: mdb.c/midl.c are committed
# directly into this package (see ../README.md "Native library").
cmake_minimum_required(VERSION 3.10)
project(ob_dump_reader_lmdb LANGUAGES C)

add_library(lmdb SHARED
  mdb.c
  midl.c
)
set_target_properties(lmdb PROPERTIES
  OUTPUT_NAME "lmdb"
  # MSVC DLLs export no symbols by default (unlike ELF .so) — mdb.c/midl.c
  # have no __declspec(dllexport) decorations of their own, so without
  # this every mdb_* function is invisible to GetProcAddress/dart:ffi's
  # DynamicLibrary.lookup ("Failed to lookup symbol 'mdb_env_create'"),
  # caught by a real Windows CI run, not assumed. Auto-generates a .def
  # exporting every public symbol, same effect as hand-writing one.
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(lmdb PRIVATE Threads::Threads)
endif()
