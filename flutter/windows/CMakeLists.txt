# The Flutter tooling requires that developers have a version of Visual
# Studio installed that includes CMake 3.14 or later. You should not
# increase this version, as doing so will cause the plugin to fail to
# compile for some customers of the plugin.
#
# NOTE: unlike linux/CMakeLists.txt and android/build.gradle (both
# empirically built and run this session — a real Flutter Linux release
# build and a real Android emulator, respectively), this file has not been
# built on a real Windows machine — no Windows toolchain is available in
# this environment. Written by pattern, matching the official
# `flutter create --template=plugin_ffi` scaffold exactly (confirmed by
# generating that scaffold and diffing against it), not verified here.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "ob_dump_reader_flutter")
project(${PROJECT_NAME} LANGUAGES CXX)

# Invoke the build for native code shared with every other target platform
# (../src/CMakeLists.txt + ../src/lmdb/*.c). No plugin registration class
# needed on any platform: this is a pure `ffiPlugin` (see ../pubspec.yaml),
# not a method-channel plugin.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../src" "${CMAKE_CURRENT_BINARY_DIR}/shared")

# List of absolute paths to libraries that should be bundled with the
# application build. Flutter's Windows build copies these into
# build/windows/<arch>/runner/Release/ — the executable-relative location
# this package's Dart-side library loader looks in once
# Isolate.resolvePackageUriSync is unavailable (a compiled AOT release
# build).
set(ob_dump_reader_flutter_bundled_libraries
  "$<TARGET_FILE:lmdb>"
  PARENT_SCOPE
)
