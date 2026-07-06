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
)

if(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(lmdb PRIVATE Threads::Threads)
endif()
