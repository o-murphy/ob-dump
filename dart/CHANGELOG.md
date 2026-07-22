# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Generated from the repo root's `CHANGELOG.md` at publish time — see that
file if you're reading this in the published package and want the full
picture across the other packages in this repo too.

## [0.1.0-beta.1] - 2026-07-22

### Changed

- Version bump only — republished in lockstep with `flutter`/`py` (see
  the intro above), no functional changes.

## [0.1.0-alpha.2] - 2026-07-06

### Added

- **`dart run ob_dump_reader:build`** — compiles the vendored LMDB source
  into a shared library for the current desktop platform (Linux/macOS/
  Windows), via a plain CMake configure+build. No network, no
  FetchContent, no prebuilt binary. Only needed for plain-Dart (non-
  Flutter) use; `ob_dump_reader_flutter` bundles its own build
  automatically as a real Flutter plugin.
- `readToManyTargets()` — resolves ObjectBox `ToMany` relation links
  directly from LMDB (not part of the FlatBuffers table `flatc --dart`
  output can decode).
- `decodeFlex()`, `bytesToHex()`, `bytesToUuidString()`,
  `tryParseJsonString()` — decode helpers for `Flex` fields and the
  `Uuid`/`Int128`/`Decimal128`/`Bson`/`Json`/`JavaScript`/`JsonToNative`
  `ExternalPropertyType` annotations, which `flatc --dart` output only
  gives you as raw bytes/strings. Mirrors `ob-dump`'s own C++ decode
  exactly.

### Changed

- **No longer depends on `dart_lmdb2`** — replaced with a small,
  purpose-built `dart:ffi` binding over the vendored LMDB C source
  (`src/lmdb/`, LMDB_0.9.31 — the same commit `ob-dump`'s own C++ core
  builds and has verified). Reasons, both confirmed empirically rather
  than assumed:
  - `dart_lmdb2`'s own native-library path resolution
    (`Isolate.resolvePackageUriSync()`) throws `UnsupportedError`
    outright in a compiled AOT release build — reproduced with a real
    `flutter build linux --release`, copying only the output bundle to
    an isolated directory (no source project, no pub cache) and running
    it. Not Flatpak-specific: breaks every compiled Flutter Linux/Windows
    release build depending on it.
  - `flutter_lmdb2` (the Flutter-plugin wrapper) has no Linux or Windows
    support at all as published — both platforms are commented out in
    its own `pubspec.yaml`.
  - This package's own loader (`src/lmdb_native_library.dart`) doesn't
    repeat either mistake: tries `package:` URI resolution first
    (unchanged `dart run`/`flutter run` behaviour), then falls back per
    platform to where each platform's own native-library bundling
    convention actually places the library (confirmed against the
    official `flutter create --template=plugin_ffi` scaffold, not
    guessed).
- **`readObjectBoxRecords()` no longer copies to a temporary directory**
  — reads the original files directly, in place. The copy was only ever
  needed because `dart_lmdb2` required a write-capable LMDB environment
  just to register the root database handle; this package's own binding
  opens the environment itself `MDB_RDONLY` from the start (matching
  `ob-dump`'s own C++ `LmdbReader`), which is exactly what LMDB's MVCC
  design is for: any number of readers can safely run alongside one
  concurrent writer, in any process, with zero risk to the original data.

### Removed

- **`readObjectBoxRecordsUnsafe()` / `readToManyTargetsUnsafe()`** — no
  longer needed now that the safe, default functions read in place with
  no copy (see above); there was no longer a distinction for these to
  make.

## [0.1.0-alpha.0]

### Added

- `readObjectBoxRecords()` — walks an ObjectBox LMDB store and yields each
  record's raw FlatBuffers table bytes plus its entity id and object id. No
  FlatBuffers/schema knowledge; pair with `flatc --dart` output generated
  from `ob_dump --fbs` (see the parent [`ob-dump`](.) project).

[0.1.0-beta.1]: https://github.com/o-murphy/ob-dump/compare/v0.1.0-alpha.2...v0.1.0-beta.1
[0.1.0-alpha.2]: https://github.com/o-murphy/ob-dump/releases/tag/v0.1.0-alpha.2
[0.1.0-alpha.0]: https://pub.dev/packages/ob_dump_reader/versions/0.1.0-alpha.0
