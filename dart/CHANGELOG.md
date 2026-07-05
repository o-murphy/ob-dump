# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-07-05

### Added

- `readObjectBoxRecords()` — walks an ObjectBox LMDB store and yields each
  record's raw FlatBuffers table bytes plus its entity id and object id. No
  FlatBuffers/schema knowledge; pair with `flatc --dart` output generated
  from `ob_dump --fbs` (see the parent [`ob-dump`](..) project).

[Unreleased]: https://github.com/o-murphy/ob-dump/compare/dart-v0.1.0...HEAD
[0.1.0]: https://github.com/o-murphy/ob-dump/releases/tag/dart-v0.1.0
