# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Generated from the repo root's `CHANGELOG.md` at publish time — see that
file if you're reading this in the published package and want the full
picture across the other packages in this repo too.

## [0.1.0-beta.1] - 2026-07-22

### Changed

- **`dependencies.ob_dump_reader` is now a real, checked-in `^X.Y.Z`
  constraint** (kept in lockstep with `dart/pubspec.yaml`'s `version:`,
  enforced by `.github/workflows/version-parity.yml`), with the local
  monorepo path dep moved to `dependency_overrides.ob_dump_reader`
  instead. Previously this was a bare `path: ../dart` under
  `dependencies` plus a `publish_to: none` — required because a plain
  path dependency there isn't publishable (`flutter analyze` fails on
  `invalid_dependency` without it) — which meant `publish-flutter` had to
  ephemerally rewrite `pubspec.yaml` (`pub remove`/`pub add` + strip
  `publish_to: none`) right before `pub publish`, in the release
  checkout only. `dependency_overrides` is never inspected by the
  analyzer's path-dependency check and has zero effect on what a
  consumer of the published package resolves, so nothing needs that
  rewrite anymore. See `docs/BACKLOG.md` item 25.

## [0.1.0-alpha.2] - 2026-07-06

### Changed

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

## [0.1.0-alpha.0]

### Added

- Re-exports [`ob_dump_reader`](dart)'s full API unchanged, depending on
  `flutter_lmdb2` instead of `dart_lmdb2` so Flutter's plugin tooling
  properly bundles the native LMDB library on Android/iOS/macOS (see the
  `0.1.0-alpha.2` entry above for why that dependency was since dropped).

[0.1.0-beta.1]: https://github.com/o-murphy/ob-dump/compare/v0.1.0-alpha.2...v0.1.0-beta.1
[0.1.0-alpha.2]: https://github.com/o-murphy/ob-dump/releases/tag/v0.1.0-alpha.2
[0.1.0-alpha.0]: https://pub.dev/packages/ob_dump_reader_flutter/versions/0.1.0-alpha.0
