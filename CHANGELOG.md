# Changelog

All notable changes across the whole `ob-dump` repo (the C++ core, `dart/`,
`flutter/`, `py/`) are documented in this one file — the single source of
truth. Each package's own `CHANGELOG.md` (`dart/CHANGELOG.md`,
`flutter/CHANGELOG.md`, `py/CHANGELOG.md`) is **generated** from this file
by `scripts/ci/sync_changelogs.py` — a physical copy still has to exist in
each package's own directory because pub.dev refuses to publish a package
without one, so a purely root-level file can't fully replace them. **Edit
this file, then run `scripts/ci/sync_changelogs.py`** (CI regenerates and
fails the build if a package's copy is out of sync — see
`.github/workflows/changelog-sync.yml`).

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
One version number covers the C++ core and all three packages together —
every release is driven by a single `vX.Y.Z` git tag (see
`.github/workflows/release.yml`) — but not every package necessarily
changed in every release, so a version heading below may have only some of
`### dart/`, `### flutter/`, `### py/`, `### core` (the C++ core itself has
no package registry of its own to publish to, so entries land only here,
never copied anywhere). Only `0.1.0-alpha.0` has actually shipped so far —
published by hand directly to pub.dev to reserve the package names, never
through a git tag/`release.yml` run, so it has no GitHub tag to link to
below (see `docs/BACKLOG.md` item 18). Everything else is still
`[Unreleased]`, regardless of what `dart/pubspec.yaml`/`flutter/pubspec.yaml`
are currently bumped to in preparation for the next real release.

## [Unreleased]

### dart/

#### Added

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

#### Changed

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

#### Removed

- **`readObjectBoxRecordsUnsafe()` / `readToManyTargetsUnsafe()`** — no
  longer needed now that the safe, default functions read in place with
  no copy (see above); there was no longer a distinction for these to
  make.

### flutter/

#### Changed

- **Now a real Flutter plugin (`ffiPlugin: true`) for all five platforms
  — Android, iOS, Linux, macOS, Windows** — bundling the same vendored-
  LMDB `dart:ffi` binding `ob_dump_reader` itself uses, compiled from
  source per platform (CMake on Linux/Windows/Android via NDK, a
  CocoaPods podspec + forwarder `.c` files on iOS/macOS). No
  `flutter_lmdb2` dependency — it has no Linux/Windows support at all as
  published, and provided no benefit here now that this package compiles
  and bundles the library itself directly. Uses `ffiPlugin: true` rather
  than `pluginClass:` since there's no method-channel surface at all —
  confirmed against the official `flutter create --template=plugin_ffi`
  scaffold, not guessed.
- **Verified empirically, not just built** — Linux: a real
  `flutter build linux --release`, output bundle copied to an isolated
  directory (no source project, no pub cache) and run directly, reading
  a real ObjectBox database successfully. Android: a real debug/release
  APK built and installed on an emulator, `liblmdb.so` present for all
  three ABIs (arm64-v8a, armeabi-v7a, x86_64), confirmed working via
  logcat output from an on-device LMDB write+read round trip. Windows/
  macOS/iOS: written to match the official `plugin_ffi` scaffold exactly
  (verified by generating that scaffold and diffing against it), but not
  built on real hardware — no toolchain available in the environment
  this was developed in.

### py/

#### Fixed

- **`decode_flex()` crashed on every real call** — assumed
  `flatbuffers.flexbuffers.Ref` had a `.json` property; it doesn't (only a
  debug `__repr__`), so `json.loads()` was fed something like
  `"Ref(buf[21:], parent_width=1, byte_width=1, type_=9)"` and raised
  immediately. Replaced with a proper recursive `Ref` → native-Python-object
  converter (`Is*`/`As*` are properties, not methods — also wrong in the
  original), matching the same map/vector/scalar/blob handling as `ob-dump`'s
  own C++ `flexToJson` and the Dart package's `decodeFlex`. Covered by
  `tests/test_decode_helpers.py` (a map root and a bare vector root, both
  round-tripped through real `flatbuffers.flexbuffers.Dumps()` output).
- **`ob_dump_reader.aio` couldn't be imported at all** —
  `from ob_dump_reader.decode_helpers import K_KEY_TYPE_DATA, ...` referenced
  a name that has never existed in that module (`ImportError`), caught by
  actually importing the module, not assumed.
- **`ob_dump_reader.aio` misused `lmdb.aio`'s actual API** even once the
  import was fixed: `AsyncEnvironment(path, map_size=..., ...)` isn't how
  it's constructed — `lmdb.aio.wrap()` wraps an already-open
  `lmdb.Environment` instead (confirmed by reading `lmdb.aio`'s own
  docstring and source, not guessed). Separately, `AsyncCursor` has no
  `__aiter__`, so `async for key, value in cursor` was never going to work;
  its `iternext()` also fully materializes every remaining entry into a list
  in one executor call rather than streaming, which would have meant loading
  the *entire rest of the database* into memory before the first callback
  fires — replaced with an explicit `first()`/`next()`/`item()` step loop
  instead, keeping this a genuine per-record callback. Verified against a
  real fixture (`tests/test_aio.py`), not just read.
- **`read_objectbox_to_many_targets()` was a no-op stub** (sync and async) —
  opened an environment and did nothing else, silently returning `None`
  despite being part of the public API. Implemented for real: the same
  12-byte relation-key format ([type=0x08][2 reserved bytes][(relation_id
  << 2) | direction][source_id u32 BE][target_id u32 BE]) `ob-dump`'s C++
  core and the Dart package already use, via `Cursor.set_range()` positioned
  at the relation/source-id prefix, walking forward while the prefix still
  matches. Forward direction only (0) — direction 2 is an auto-maintained
  reverse index for ObjectBox's own query engine, not needed for a
  one-directional dump. Covered by `tests/test_ob_dump_reader.py` and
  `tests/test_aio.py` (forward links returned, backward/other-relation
  links ignored, empty list when the source object has no links).

#### Changed

- `KEY_TYPE_DATA`/`KEY_TYPE_RELATION`/relation-direction constants moved to
  a shared `_constants.py`, imported by both `__init__.py` and `aio.py` —
  the two had already drifted out of sync once (see the `aio.py` import fix
  above), so keeping one copy removes the way that happens again.
- Fixed a real typo: `RELATION_DIRECTION_FFORWARD` → `RELATION_DIRECTION_FORWARD`.
- Removed dead, commented-out "write transaction to initialize the root
  handle" code — a leftover assumption from `dart_lmdb2`-style bindings
  that turned out to be unnecessary once the actual read path was verified:
  LMDB's MVCC design lets any number of readers run safely alongside one
  concurrent writer once the environment itself is opened `readonly=True`
  (same finding as `dart/`'s own entry above, reached independently for
  the Dart package first).
- `__main__.py` no longer hardcodes a developer's personal local database
  path — replaced with a real `argparse`-based CLI
  (`python -m ob_dump_reader <objectbox_dir>` / the new `ob-dump-reader`
  console script, see below).
- Dropped the `read_ob_records`/`read_ob_to_many_targets` short aliases —
  undocumented duplicate names for `read_objectbox_records`/
  `read_objectbox_to_many_targets` with no clear purpose; one canonical name
  per function, matching the Dart/C++ packages.

#### Added

- `tests/` — this package had no tests directory at all despite `pytest`
  already being declared as a dev dependency and `testpaths = ["tests"]`
  already configured. 15 tests across `test_ob_dump_reader.py`,
  `test_aio.py`, and `test_decode_helpers.py`, using a shared
  `tests/_fixture.py` LMDB-writing helper (mirrors `dart/test/test_fixture.dart`).
- `[project.scripts]` entry point: `ob-dump-reader = "ob_dump_reader.__main__:main"`.

#### Removed

- Unused `protovalidate`/`pydantic` dev dependencies — not referenced
  anywhere in `src/` or the new `tests/`; pulled in 16 transitive packages
  for nothing.
- The blanket `__init__.py` exclusion from the `[tool.ruff]` config —
  `__init__.py` here holds real logic (not just re-exports), so excluding
  it from linting was hiding real issues (see the fixes above).

## [0.1.0-alpha.0]

Published by hand directly to pub.dev (not through `release.yml`), purely
to reserve the package names — confirmed live via pub.dev's own API
(`ob_dump_reader` and `ob_dump_reader_flutter` both at `0.1.0-alpha.0`).

### dart/

#### Added

- `readObjectBoxRecords()` — walks an ObjectBox LMDB store and yields each
  record's raw FlatBuffers table bytes plus its entity id and object id. No
  FlatBuffers/schema knowledge; pair with `flatc --dart` output generated
  from `ob_dump --fbs` (see the parent [`ob-dump`](.) project).

### flutter/

#### Added

- Re-exports [`ob_dump_reader`](dart)'s full API unchanged, depending on
  `flutter_lmdb2` instead of `dart_lmdb2` so Flutter's plugin tooling
  properly bundles the native LMDB library on Android/iOS/macOS (see the
  `[Unreleased]` entry above for why that dependency was since dropped).

[Unreleased]: https://github.com/o-murphy/ob-dump/commits/main
