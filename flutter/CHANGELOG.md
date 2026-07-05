# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-07-06

### Added

- Re-exports [`ob_dump_reader`](../dart)'s full API unchanged, depending on
  `flutter_lmdb2` instead of `dart_lmdb2` so Flutter's plugin tooling
  properly bundles the native LMDB library on Android/iOS/macOS.

[Unreleased]: https://github.com/o-murphy/ob-dump/compare/flutter-v0.1.0...HEAD
[0.1.0]: https://github.com/o-murphy/ob-dump/releases/tag/flutter-v0.1.0
