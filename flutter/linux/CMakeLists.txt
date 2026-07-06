# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

# Project-level configuration.
set(PROJECT_NAME "ob_dump_reader_flutter")
project(${PROJECT_NAME} LANGUAGES C)

# Invoke the build for native code shared with every other target platform
# (../src/CMakeLists.txt + ../src/lmdb/*.c) — same pattern the official
# `flutter create --template=plugin_ffi` scaffold uses. No plugin
# registration class needed on any platform: this is a pure `ffiPlugin`
# (see ../pubspec.yaml), not a method-channel plugin.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../src" "${CMAKE_CURRENT_BINARY_DIR}/shared")

# List of absolute paths to libraries that should be bundled with the
# application build. Flutter's Linux build copies these into
# build/linux/<arch>/release/bundle/lib/ — the executable-relative location
# this package's Dart-side library loader looks in once
# Isolate.resolvePackageUriSync is unavailable (a compiled AOT release
# build).
set(ob_dump_reader_flutter_bundled_libraries
  "$<TARGET_FILE:lmdb>"
  PARENT_SCOPE
)
