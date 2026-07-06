# Builds liblmdb (shared) from the vendored LMDB_0.9.31 source in
# ./lmdb — the exact same source ob-dump's own C++ core builds and has
# verified. No FetchContent, no network, no prebuilt binary: mdb.c/midl.c
# are committed directly into this package (see ../README.md "Native
# library"). Shared by every platform's own CMakeLists.txt via
# add_subdirectory — same pattern the official `flutter create
# --template=plugin_ffi` scaffold uses, so each platform only has to say
# "bundle $<TARGET_FILE:lmdb>", not repeat the compile step.
cmake_minimum_required(VERSION 3.10)
project(ob_dump_reader_flutter_lmdb LANGUAGES C)

add_library(lmdb SHARED
  lmdb/mdb.c
  lmdb/midl.c
)
set_target_properties(lmdb PROPERTIES
  OUTPUT_NAME "lmdb"
  # MSVC DLLs export no symbols by default (unlike ELF .so) — mdb.c/midl.c
  # have no __declspec(dllexport) decorations of their own, so without
  # this every mdb_* function is invisible to GetProcAddress/dart:ffi's
  # DynamicLibrary.lookup ("Failed to lookup symbol 'mdb_env_create'"),
  # caught by a real Windows CI run against dart/'s identical CMakeLists.txt,
  # not assumed. Auto-generates a .def exporting every public symbol, same
  # effect as hand-writing one. Harmless on non-Windows platforms.
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# mdb.c uses pthread_mutex/pthread-specific data. Android's NDK C library
# already provides these symbols (no separate libpthread.so since API 21),
# so only link Threads::Threads on the platforms that actually need it —
# relying on it being pulled in transitively isn't portable across glibc
# versions/runtimes (e.g. glibc >= 2.34 merges pthread into libc.so.6, but
# that's not guaranteed on every target).
if(NOT ANDROID)
  find_package(Threads REQUIRED)
  target_link_libraries(lmdb PRIVATE Threads::Threads)
endif()
