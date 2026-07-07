# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Generated from the repo root's `CHANGELOG.md` at publish time — see that
file if you're reading this in the published package and want the full
picture across the other packages in this repo too.

## [Unreleased]

### Added

- Initial `ob-dump-reader` npm package — `readObjectboxRecords()`/
  `readObjectboxToManyTargets()` walk an ObjectBox LMDB store directly via
  [`lmdb`](https://www.npmjs.com/package/lmdb) (no FFI binding to this
  repo's C++ core), same pattern as `dart/`/`py/`. Pair with `flatc --ts`
  output generated from `ob_dump --fbs` to decode each record's raw
  FlatBuffers bytes.
- `decodeHelpers` (`decodeFlex()`, `bytesToHex()`, `bytesToUuidString()`,
  `tryParseJsonString()`) — same `Flex`/`ExternalPropertyType` decode
  helpers as `dart/`/`py/`, mirroring `ob-dump`'s own C++ decode exactly.
- `ob-dump-reader` CLI (`js/src/cli.ts`) — prints each record's entity id,
  object id, and raw FlatBuffers data length.

[Unreleased]: https://github.com/o-murphy/ob-dump/commits/main/js
