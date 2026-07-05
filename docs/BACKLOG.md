# ob-dump — architecture & backlog

`ob-dump` reads an ObjectBox LMDB database (`data.mdb`) directly — without linking
`objectbox-c` — and dumps its contents to JSON, driven entirely by the project's
own `objectbox-model.json` schema file. No `.fbs` / `flatc` codegen is involved:
decoding is schema-driven at runtime using the generic FlatBuffers `Table` API.

Born out of the ebalistyka ObjectBox → SQL/keystore migration (GPL-3.0 project
blocked from bundling the closed-source `objectbox-c` binary on Flathub). See
that project's PoC at `tools/ob_migration_poc/` for the reference Dart
implementation this design ports from.

## Why this exists (vs. linking objectbox-c)

`objectbox-c` is a closed-source prebuilt binary. Its FlatBuffers encoding
convention is undocumented but reverse-engineerable and stable:

- **LMDB key format** (8 bytes, root/unnamed DB only): `[type:1][0x00][0x00][entity_id*4:1][object_id:u32 BE]`.
  `type`: `0x00` = schema entry, `0x18` = object data, `0x20` = index. ObjectBox
  never uses named sub-databases.
- **FlatBuffers vtable slot per property**: `slot = 4 + (property_id - 1) * 2`.
  This is **not** ObjectBox-specific — it's the generic, documented FlatBuffers
  table format (`docs/source/internals.md` in upstream `google/flatbuffers`):
  vtable[0]/[1] are header (size-of-vtable, size-of-object), then one
  `voffset_t` per declared field, in declaration order. ObjectBox simply binds
  each property's permanent numeric `id` (from `objectbox-model.json`) as that
  field's declaration index (`id - 1`) when it writes the buffer.
- Checked against `objectbox/flatbuffers` (their GitHub fork): it's a vanilla
  mirror of `google/flatbuffers` (only diff vs upstream is a CI workflow file).
  No ObjectBox-specific vtable logic lives there — the key format is purely an
  LMDB storage-engine convention with no public source.

Because property ids are **permanent and can have gaps** (deleted properties
leave retired ids — confirmed via ebalistyka's real model: e.g. entity `Ammo`
has ids `[1,2,6,7,8,9,10,18,20,22,29,...]`, with `retiredPropertyUids` present
in the model file), a reader must treat "slot beyond this record's vtable
size" as "field absent", not an error — this is normal forward/backward
compatibility, not corruption.

## Design decisions

| Decision | Choice | Why |
|---|---|---|
| Language | Pure C public ABI; implementation may use C++ where a dependency requires it (FlatBuffers' generic `Table`/`Verifier` API and nlohmann/json are C++ header-only libraries) | Public surface must be trivially bindable via CFFI from any language (Dart `dart:ffi`, Python `ctypes`/`cffi`, Go `cgo`, JVM JNI, Swift...). C++-ness is fully internal, hidden behind `extern "C"`. |
| Build | CMake + `FetchContent` | No network needed at *use* time, only at first configure; each dependency vendors/builds from source (Flathub/GPL-safe — no prebuilt binaries). |
| LMDB | Vendor `mdb.c`/`midl.c` from `LMDB/lmdb` upstream, compiled ourselves (no CMake shipped upstream) | Same trusted, source-buildable pattern as `dart_lmdb2`/`dart_bclibc` already used in ebalistyka. |
| FlatBuffers decode | Official `flatbuffers::Table` + `flatbuffers::Verifier` (header-only C++), driven by slot numbers computed at runtime from `objectbox-model.json` — **no `.fbs` schema, no `flatc` codegen** | Gives us battle-tested bounds-checked reads for free instead of hand-rolling and re-testing our own overflow checks in C. Still zero external schema files — same "dynamic" approach as the Dart PoC, just backed by the official reader instead of hand-rolled offset math. |
| JSON | `nlohmann/json` (header-only) | Well-tested, ergonomic C++ JSON construction; avoids hand-rolled string escaping bugs. |
| Copy strategy | Zero-copy | LMDB's `mdb_get`/cursor API returns pointers directly into the mmap'd file within a read txn — never copied. FlatBuffers' `Table` API reads fields directly off that same pointer. The **only** allocation in the whole pipeline is the final output JSON string. |
| Export format | JSON (default) | Primary use case is manual data recovery from an abandoned ObjectBox db — human-readability matters more than wire efficiency at this data volume (hundreds/thousands of records). int64 fields are emitted as JSON strings to sidestep IEEE-754-double precision loss in string-to-number JSON parsers (e.g. JS); doubles are used as-is. Alternative binary formats (MessagePack/CBOR) or a direct-to-SQLite writer are plausible future output modes — see backlog below — since the core (LMDB walk + decode) is decoupled from serialization. |

## Public API (draft)

```c
// include/ob_dump.h
typedef enum ObDumpSourceKind {
    OB_DUMP_SOURCE_PATH   = 0, // path to a data.mdb file; opened directly by LMDB (true zero-copy mmap)
    OB_DUMP_SOURCE_BUFFER = 1, // in-memory buffer holding a full data.mdb image (see backlog: memfd-backed on Linux)
} ObDumpSourceKind;

typedef struct ObDumpSource {
    ObDumpSourceKind kind;
    union {
        const char* path;
        struct { const uint8_t* data; size_t size; } buffer;
    } as;
} ObDumpSource;

// model_json: full *contents* of objectbox-model.json (not a path) — small, so
// always passed as an in-memory string to keep the FFI surface simple.
//
// Returns a malloc'd, NUL-terminated JSON string on success, or NULL on error
// (call ob_dump_last_error() for a message). Caller MUST release the result
// with ob_dump_free() — never free()/delete directly (allocator may differ
// across the FFI boundary in some target languages/build configs).
char*       ob_dump(const ObDumpSource* source, const char* model_json);
void        ob_dump_free(char* json);
const char* ob_dump_last_error(void);
```

CLI wraps this 1:1:

```
ob_dump <base>.mdb objectbox-model.json [-o dump.json]
```//
(no `-o` → JSON on stdout, so `ob_dump base.mdb model.json > dump.json` works too)

## Scope — what v1 reads

Property types implemented (matches everything actually observed in
ebalistyka's real schema — confirmed via a scan of every entity in
`objectbox-model.json`, only these 7 types appear):

- `bool` (1), `int64`/`Date` (6), `double` (8), `string` (9),
  `relation`/ToOne (11, stored as int64 fk), `Float64Vector` (29), `StringVector` (30)

(`fb_decode.cpp` handles this with 6 `switch` branches, not 7 — `Long` and
`Relation` share one branch, since both are a plain `int64` on the wire.)

## Explicitly out of scope for v1 (tracked here, not silently ignored)

- **`ToMany` relations** — stored in a *separate* LMDB relation-index structure
  (different key-type byte, not part of the object's FlatBuffers table at
  all). Needs its own walk once we have a real schema that uses one.
- **`Flex` properties** — dynamic property stored as an embedded FlexBuffers
  blob (a different encoding from the table-based FlatBuffers we decode).
- Remaining scalar/vector `PropertyType` codes ObjectBox defines but that don't
  appear in ebalistyka's schema (`int`/`short`/`byte`/`char`/`float32` and
  their vector forms, `ByteVector`, `StringIdVector`). Add as encountered —
  the slot-lookup mechanism is generic, only the per-type reader is missing.
- Big-endian host support (FlatBuffers is always little-endian on disk;
  reading on a BE host needs an explicit byteswap we haven't added).
- Windows/macOS build coverage (Linux-first, matching ebalistyka's own
  Flathub-first priority).

## Phased plan

1. **Scaffolding** (this commit) — CMake + FetchContent wiring for
   lmdb/flatbuffers/nlohmann-json, public header, empty CLI, this doc.
2. **Schema loader** — parse `objectbox-model.json` into an in-memory
   `entity_id → {name, [{property_id, name, type}]}` table.
3. **LMDB walk** — open read-only txn on the root/unnamed db, cursor-walk,
   classify keys by type byte, group object records by `entity_id`.
4. **FlatBuffers decode** — `flatbuffers::Table::GetField<T>`/`GetPointer` per
   property, wrapped in `flatbuffers::Verifier` bounds checks; skip
   out-of-range slots as "absent" (handles retired-property gaps correctly).
5. **JSON emission** — assemble `{EntityName: [ {..fields.., "id": objId}, ... ]}`,
   matching the shape `tools/ob_migration_poc/bin/export_json.dart` already
   produces in ebalistyka, so outputs can be diffed 1:1 against the proven
   Dart reference during verification.
6. **CLI** — argument parsing, file I/O, wire to `ob_dump()`.
7. **Verification** — run against a real `data.mdb` + `objectbox-model.json`
   pair and diff the JSON against the Dart PoC's output. Done: 10/11 entities
   matched exactly (`ConvertorsState` had 0 records on both sides). The 4
   remaining per-field diffs are **not bugs** — they're the Dart PoC treating
   "value equals the type's zero/empty default" as equivalent to "field
   absent" (`v == 0 ? null : v` for longs, `v.isEmpty ? null : v` for
   strings, `count == 0 ? null : ...` for vectors, and always defaulting
   bools instead of checking presence at all). `ob_dump` instead does a
   genuine vtable-presence check (`Table::CheckField`) uniformly across
   every type, which is the semantically correct FlatBuffers behavior —
   e.g. a real ToOne relation id of `0` (`ammoId`, `sightId`) is now
   reported as `0` rather than incorrectly nulled out. This is the reason
   the real `flatbuffers::Table` API was worth depending on in the first
   place; the Dart PoC was never fully correct here, it just went
   unnoticed until a byte-exact diff.
8. **Language bindings** (future) — thin wrappers per target language calling
   the C ABI directly: Dart (`dart:ffi`), Python (`ctypes`/`cffi`), etc.
   Each wrapper is expected to be small since all logic lives in the core.
9. **Alternate output formats** (future) — MessagePack/CBOR writer, or a
   direct-to-SQLite writer (via the `sqlite3` C amalgamation) as an
   alternative to JSON, selected by an output-format parameter — useful for
   projects (like ebalistyka) whose actual migration target is SQL, skipping
   the JSON intermediate entirely.
10. **`OB_DUMP_SOURCE_BUFFER` input mode** (future) — LMDB's API only opens
    real files; a pure in-memory buffer input can't be handed to
    `mdb_env_open` directly. On Linux, back it with `memfd_create` + one
    `write()` so LMDB still mmaps a real fd without touching disk — the
    closest practical approximation to zero-copy for this input mode.
