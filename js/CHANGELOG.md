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
- `postinstall` script (`js/scripts/postinstall.cjs`) rebuilds `lmdb` from
  source with `LMDB_DATA_V1=true`, required to read real ObjectBox
  databases at all: `lmdb`'s prebuilt binaries default to LMDB's newer
  data format v2, while ObjectBox (like this repo's own `dart/`/`py/`
  packages) writes the legacy data format v1 — opening a real ObjectBox
  `data.mdb` with a v2-expecting build segfaults the whole Node process
  outright, not a catchable exception (confirmed against a real
  ObjectBox-produced database, not assumed). Requires a C/C++ build
  toolchain on install, same as this repo's own `dart/` package already
  needs for its vendored-LMDB build step.
- `lmdb`'s own `noSubdir` file-vs-directory auto-detection guesses from
  whether the path contains a "." — wrong for ObjectBox data directories
  with a dot in their name (e.g. a reverse-DNS app-data directory like
  `com.example.app/`); `openStore()` now always passes `noSubdir: false`
  explicitly, confirmed against a real such directory (without it, `open()`
  tried to open the directory itself as the data file and failed with
  `EISDIR`).

[Unreleased]: https://github.com/o-murphy/ob-dump/commits/main/js
