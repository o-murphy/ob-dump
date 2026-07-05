cmake_minimum_required(VERSION 3.16)
project(ob_dump LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(FetchContent)

# ── LMDB ──────────────────────────────────────────────────────────────────────
# Upstream ships no CMake support; vendor mdb.c/midl.c and build them ourselves.
# Same "source-buildable, no prebuilt binary" pattern as dart_lmdb2/dart_bclibc.
FetchContent_Declare(
  lmdb
  GIT_REPOSITORY https://github.com/LMDB/lmdb.git
  GIT_TAG        LMDB_0.9.31
)
FetchContent_MakeAvailable(lmdb)

add_library(lmdb STATIC
  ${lmdb_SOURCE_DIR}/libraries/liblmdb/mdb.c
  ${lmdb_SOURCE_DIR}/libraries/liblmdb/midl.c
)
target_include_directories(lmdb PUBLIC ${lmdb_SOURCE_DIR}/libraries/liblmdb)

# ── FlatBuffers (header-only, generic Table/Verifier API — no flatc, no .fbs) ─
# We deliberately never build the compiled `flatbuffers`/`flatc` targets:
# everything we use (Table, Verifier, GetRoot, Vector, String) lives entirely
# in headers. Pulling in the compiled library would mean building
# idl_parser.cpp/reflection.cpp — schema-parsing machinery for .fbs files we
# intentionally don't use (see docs/BACKLOG.md).
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATLIB OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  flatbuffers
  GIT_REPOSITORY https://github.com/google/flatbuffers.git
  GIT_TAG        v25.9.23
)
FetchContent_MakeAvailable(flatbuffers)

add_library(flatbuffers_headers INTERFACE)
target_include_directories(flatbuffers_headers INTERFACE ${flatbuffers_SOURCE_DIR}/include)

# ── nlohmann/json (header-only) ───────────────────────────────────────────────
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

# ── ob_dump core ───────────────────────────────────────────────────────────────
# OBJECT library: compiled once, reused by both the shared lib (CFFI
# consumers) and the CLI executable via $<TARGET_OBJECTS:...> — avoids either
# a double compile or a linker whole-archive hack to keep the public symbols
# from being dropped as "unreferenced" when wrapped in a library with no
# sources of its own.
add_library(ob_dump_core OBJECT
  src/schema.cpp
  src/lmdb_reader.cpp
  src/fb_decode.cpp
  src/dumper.cpp
  src/ob_dump.cpp
)
target_include_directories(ob_dump_core PUBLIC include)
target_compile_definitions(ob_dump_core PRIVATE OB_DUMP_BUILD)
target_link_libraries(ob_dump_core PUBLIC lmdb flatbuffers_headers nlohmann_json::nlohmann_json)
set_target_properties(ob_dump_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Shared library exposing the public C ABI, for CFFI consumers.
add_library(ob_dump SHARED $<TARGET_OBJECTS:ob_dump_core>)
target_link_libraries(ob_dump PRIVATE ob_dump_core)
set_target_properties(ob_dump PROPERTIES
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

# ── CLI ────────────────────────────────────────────────────────────────────────
# main.c is plain C, but ob_dump_core is C++ under the hood — force the C++
# driver for the final link so libstdc++/exception-handling symbols resolve.
add_executable(ob_dump_cli cli/main.c)
target_link_libraries(ob_dump_cli PRIVATE ob_dump_core)
set_target_properties(ob_dump_cli PROPERTIES
  OUTPUT_NAME ob_dump
  LINKER_LANGUAGE CXX
)

enable_testing()
add_subdirectory(tests)
