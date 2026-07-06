# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Generated from the repo root's `CHANGELOG.md` at publish time — see that
file if you're reading this in the published package and want the full
picture across the other packages in this repo too.

Version numbers are derived from git tags via `setuptools_scm`, not hand-bumped in this file.

## [0.1.0-alpha.2] - 2026-07-06

### Fixed

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

### Changed

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

### Added

- `tests/` — this package had no tests directory at all despite `pytest`
  already being declared as a dev dependency and `testpaths = ["tests"]`
  already configured. 15 tests across `test_ob_dump_reader.py`,
  `test_aio.py`, and `test_decode_helpers.py`, using a shared
  `tests/_fixture.py` LMDB-writing helper (mirrors `dart/test/test_fixture.dart`).
- `[project.scripts]` entry point: `ob-dump-reader = "ob_dump_reader.__main__:main"`.

### Removed

- Unused `protovalidate`/`pydantic` dev dependencies — not referenced
  anywhere in `src/` or the new `tests/`; pulled in 16 transitive packages
  for nothing.
- The blanket `__init__.py` exclusion from the `[tool.ruff]` config —
  `__init__.py` here holds real logic (not just re-exports), so excluding
  it from linting was hiding real issues (see the fixes above).

[0.1.0-alpha.2]: https://github.com/o-murphy/ob-dump/releases/tag/v0.1.0-alpha.2
