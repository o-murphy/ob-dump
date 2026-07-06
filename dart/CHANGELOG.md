# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `readToManyTargets()`/`readToManyTargetsUnsafe()` — resolves ObjectBox
  `ToMany` relation links directly from LMDB (not part of the FlatBuffers
  table `flatc --dart` output can decode).
- `decodeFlex()`, `bytesToHex()`, `bytesToUuidString()`,
  `tryParseJsonString()` — decode helpers for `Flex` fields and the
  `Uuid`/`Int128`/`Decimal128`/`Bson`/`Json`/`JavaScript`/`JsonToNative`
  `ExternalPropertyType` annotations, which `flatc --dart` output only
  gives you as raw bytes/strings. Mirrors `ob-dump`'s own C++ decode
  exactly.
- New dependency: `flat_buffers` (for `decodeFlex`'s FlexBuffers reader —
  the same official runtime `flatc --dart` output already depends on, not
  an extra native/FFI dependency).

## [0.1.0-alpha.0] - 2026-07-05

### Added

- `readObjectBoxRecords()` — walks an ObjectBox LMDB store and yields each
  record's raw FlatBuffers table bytes plus its entity id and object id. No
  FlatBuffers/schema knowledge; pair with `flatc --dart` output generated
  from `ob_dump --fbs` (see the parent [`ob-dump`](..) project).

[Unreleased]: https://github.com/o-murphy/ob-dump/compare/v0.1.0-alpha.0...HEAD
[0.1.0-alpha.0]: https://github.com/o-murphy/ob-dump/releases/tag/v0.1.0-alpha.0
