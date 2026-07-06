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

# Overridden by the release workflow (-DOB_DUMP_VERSION=X.Y.Z, derived from
# the git tag) so ob_dump_version() reports the real release version
# instead of staying hardcoded forever.
set(OB_DUMP_VERSION "0.1.0-alpha.0" CACHE STRING "ob-dump version string, reported by ob_dump_version()")

include(FetchContent)

# ── LMDB ──────────────────────────────────────────────────────────────────────
# Upstream ships no CMake support; vendor mdb.c/midl.c and build them ourselves.
# Same "source-buildable, no prebuilt binary" pattern as dart_lmdb2/dart_bclibc.
#
# Version pinned once, in LMDB_VERSION at the repo root — dart/src/lmdb/ and
# flutter/src/lmdb/'s own vendored copies must be bumped to match by hand
# (they're separately-published pub.dev packages, so they can't share this
# FetchContent step); scripts/ci/verify-lmdb-vendor.sh checks all three stay
# in sync.
#
# Must be a release tag (LMDB_MAJOR.MINOR.PATCH, e.g. LMDB_0.9.31) — never a
# branch. Upstream (https://github.com/LMDB/lmdb) uses OpenLDAP's
# release-engineering branch naming: `mdb.master`/`mdb.master3` are moving
# development branches (mdb.master3 is the 0.9.x line's current tip, not a
# fixed point), and `mdb.RE/0.9`/`mdb.RE/1.0` are release-engineering branches
# that also move as patch releases land. Only the `LMDB_x.y.z` tags are
# immutable.
#
# LMDB_VERSION itself is kept to a single bare line, no comments — CMake's
# file(STRINGS) extracts printable-ASCII runs (it's meant for binary/text
# blobs), so a non-ASCII character (e.g. an em dash) in a comment line
# fragments it mid-line and can leak a fragment through as "the version",
# even with a REGEX filter for lines starting with `#` (caught by actually
# running this, not assumed).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/LMDB_VERSION" _lmdb_version_lines LIMIT_COUNT 1)
list(GET _lmdb_version_lines 0 LMDB_VERSION)

FetchContent_Declare(
  lmdb
  GIT_REPOSITORY https://github.com/LMDB/lmdb.git
  GIT_TAG        ${LMDB_VERSION}
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
  src/schema_json.cpp
  src/fbs_gen.cpp
  src/lmdb_reader.cpp
  src/fb_decode.cpp
  src/dumper.cpp
  src/ob_dump.cpp
)
target_include_directories(ob_dump_core PUBLIC include)
# FLATBUFFERS_LOCALE_INDEPENDENT=0: flatbuffers/base.h auto-enables (=1) the
# locale-independent strtod_l/strtoull_l path on Unix, but the ClassicLocale
# type backing it is defined in util.cpp — part of the *compiled* flatbuffers
# library we deliberately don't build (see the FlatBuffers section above:
# header-only only). Forcing plain strtod/strtoull instead is fine — nothing
# in this project calls setlocale(), so the "current" and "classic" C
# locales are the same thing here regardless.
target_compile_definitions(ob_dump_core PRIVATE
  OB_DUMP_BUILD
  OB_DUMP_VERSION="${OB_DUMP_VERSION}"
  FLATBUFFERS_LOCALE_INDEPENDENT=0
)
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
  # On Windows, MSVC/CMake's Visual Studio generator gives even a plain
  # .exe an /IMPLIB import-lib output slot — normally harmless, but here it
  # collides with the ob_dump *shared library* target's own real
  # ob_dump.lib/.exp in the same output directory (both named "ob_dump" via
  # OUTPUT_NAME; Linux/macOS never hit this since "lib"+".so"/".dylib"
  # already makes the filenames distinct). Redirect just that incidental
  # archive output elsewhere — the .exe itself still lands at the same
  # documented path (build/ob_dump / build/Release/ob_dump.exe).
  ARCHIVE_OUTPUT_NAME ob_dump_cli_implib
  LINKER_LANGUAGE CXX
)

enable_testing()
add_subdirectory(tests)
