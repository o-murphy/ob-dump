# ob-dump — architecture & backlog

`ob-dump` reads an ObjectBox LMDB database (`data.mdb`) directly — without linking
`objectbox-c` — and dumps its contents to JSON, driven entirely by the project's
own `objectbox-model.json` schema file. No `.fbs` / `flatc` codegen is involved:
decoding is schema-driven at runtime using the generic FlatBuffers `Table` API.

Born out of the ebalistyka ObjectBox → SQL/keystore migration (GPL-3.0 project
blocked from bundling the closed-source `objectbox-c` binary on Flathub). See
that project's PoC at `tools/ob_migration_poc/` for the reference Dart
implementation this design ports from.

This repo also contains [`dart/`](../dart), a small standalone pub.dev
package (`ob_dump_reader`) for reading an ObjectBox database directly from
Dart — see phased-plan item 10 below for why that's a thin LMDB-traversal
toolkit paired with official `flatc --dart` output, not an FFI binding to
this C++ core.

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
char*       ob_dump_schema(const char* model_json);  // clean schema JSON — see below
char*       ob_dump_fbs(const char* model_json);     // .fbs IDL generation — see below
const char* ob_dump_last_error(void);
```

CLI wraps this 1:1, one explicit flag per mode (`--json`/`--schema`/`--fbs` —
kept uniform rather than leaving the data-dump mode flagless):

```
ob_dump --json   <base>.mdb objectbox-model.json [-o dump.json]
ob_dump --schema objectbox-model.json [-o schema.json]
ob_dump --fbs    objectbox-model.json [-o schema.fbs]
```

(no `-o` → output on stdout, so e.g. `ob_dump --json base.mdb model.json > dump.json` works too)

## Scope — what v1 reads

All 20 non-Flex `PropertyType` codes ObjectBox defines are implemented (full
list from the official `objectbox` Dart package, `lib/src/modelinfo/enums.dart`
— the authoritative source, not guessed):

- Scalars: `Bool`(1), `Byte`(2), `Short`(3), `Char`(4), `Int`(5), `Long`(6),
  `Float`(7), `Double`(8), `String`(9), `Date`(10), `Relation`/ToOne(11,
  int64 fk), `DateNano`(12)
- Vectors: `BoolVector`(22), `ByteVector`(23), `ShortVector`(24),
  `CharVector`(25), `IntVector`(26), `LongVector`(27), `FloatVector`(28),
  `DoubleVector`(29), `StringVector`(30), `DateVector`(31),
  `DateNanoVector`(32)

`fb_decode.cpp` implements this with two templates (`decodeScalar<T>`,
`decodeNumericVector<T>`) instead of ~20 near-duplicate functions — `Bool`/
`BoolVector` stay separate since their JSON output needs an actual
`true`/`false`, not a wire `0`/`1`; `String`/`StringVector` stay separate
since they're offset/pointer-based, not fixed-width scalars.

`Char` is decoded as a plain JSON integer (the underlying UTF-16 code unit),
not a JSON string — a lone `Char` isn't guaranteed to be a valid standalone
Unicode scalar value (it could be one half of a surrogate pair), so turning
it into a UTF-8 string would risk producing invalid output for exactly the
inputs where it matters most.

Covered by `tests/fb_decode_test.cpp`, which builds real FlatBuffers tables
with `flatbuffers::FlatBufferBuilder`'s low-level `AddElement`/`AddOffset`
API (the same primitives ObjectBox itself uses) rather than relying only on
real app data, so every type has a deterministic, from-scratch test — plus
cases for: a schema property never written to a given record (must be
omitted, not defaulted), a present-but-empty vector (must be `[]`, not
omitted), an `Unknown`/`Flex` property (must be skipped, not fail the whole
record), and a truncated buffer (must throw, never read out of bounds).

**Known gap even within these 20 types:** ObjectBox tracks signedness via a
separate `flags` bitmask on the property (`UNSIGNED = 8192`,
`lib/src/modelinfo/enums.dart`), not via a distinct `PropertyType` code —
e.g. an unsigned `Long` and a signed `Long` are both type code 6. Since
`Schema::parse` doesn't read `flags` yet, every integer scalar/vector here
is decoded with its natural **signed** interpretation (matching the common
case). A genuinely-unsigned field large enough to flip sign under a signed
read would print incorrectly. Not fixed now because it's an orthogonal
concern to "which `PropertyType` codes are implemented" (the thing this
round of work was scoped to) and no real schema encountered so far uses
`UNSIGNED` — tracked here rather than silently ignored.

## Schema export: `--schema` and `--fbs`

Two CLI modes (and matching `ob_dump_schema()`/`ob_dump_fbs()` C API
functions) export the *schema*, not the data:

- **`ob_dump --schema <model.json>`** — re-serializes the parsed schema as
  clean, minimal JSON (`{entities: [{entityId, name, properties: [{id, name,
  type, vtableSlot}]}]}`), stripped of ObjectBox's model.json noise (uids,
  indexes, retired-property arrays, flags). Useful on its own, and as the
  `entity_id -> table name/shape` lookup a standalone `.fbs` consumer needs
  (see below).
- **`ob_dump --fbs <model.json>`** — generates a valid FlatBuffers IDL
  (`.fbs`) so any language's `flatc` can produce a typed reader for the raw
  table bytes, as an alternative to depending on this library's own C ABI.

**Important limit, so this isn't oversold:** the `.fbs` (and flatc's
generated code from it) only replaces the *per-record FlatBuffers decode*
step of the pipeline. It does **not** give a consumer LMDB file access (they
still need an LMDB binding for their language — commonly available, e.g.
`py-lmdb`, the `lmdb` crate) or the 8-byte ObjectBox key-format parsing
(`entity_id`/`object_id` extraction — see "Why this exists" above; this is
ObjectBox's own convention, not part of any `.fbs`). A from-scratch consumer
in another language still has to: open/walk `data.mdb` with their own LMDB
binding, parse the key themselves per our documented format, look up which
generated table type an `entity_id` corresponds to (via `--schema`'s
output), and only then hand the raw value bytes to flatc's generated
accessor. This is a real reduction in effort — the trickiest, most
bug-prone part (vtable/type decoding) is now official generated code — but
not a drop-in replacement for depending on `ob-dump` directly.

**Design notes:**
- Field declaration order in the generated `.fbs` follows property id order
  (that's what determines each field's vtable slot). Gaps from retired
  properties are filled with anonymous `_reserved_N:ubyte (deprecated);`
  placeholders to keep slot numbering aligned — flatc still counts a
  deprecated field's declaration towards the slot index, it just generates
  no accessor for it. A retired property's original type isn't recoverable
  from model.json (only its uid is kept), so the placeholder type is
  arbitrary and irrelevant.
- `Flex` properties are emitted as `[ubyte]` (the raw, still-FlexBuffers-
  -encoded bytes) rather than skipped or deprecated — real, accessible data,
  just not decoded any further here (matches the "Explicitly out of scope"
  stance on Flex below).
- An `Unknown`/unrecognized raw type code is kept **by name** (so a human
  reading the `.fbs` can see the field existed) but marked `(deprecated)`
  since its true shape can't be safely guessed.
- Confirmed the `id` property (id 1) really is written into the FlatBuffers
  table itself, not just the LMDB key — checked empirically against a real
  `Ammo` record (`Table::CheckField(4)` true, value matches the real object
  id) before deciding to treat it as a normal field rather than a special
  case.

**Verified end-to-end against real data**, not just unit tests: built a
standalone `flatc` (disabled in this project's own build — see "FlatBuffers
decode" in Design decisions — but buildable on demand from the same fetched
source), ran it on the `.fbs` generated from ebalistyka's real
`objectbox-model.json` (`flatc --cpp`, zero errors — only cosmetic
snake_case naming-convention warnings, since our field names come straight
from Dart property names). Extracted a real `Ammo` record's raw table bytes
and read them with the officially generated code, entirely independent of
this project's own `fb_decode.cpp`: `Verify()` succeeded, and every field
checked (`id`, `name`, `dragTypeValue`, `bcG1`, `bcG7`, `useMultiBcG1`,
`ownerId`, `caliberInch`, `weightGrain`, `muzzleVelocityMps`) matched our own
decoder's output exactly.

## Explicitly out of scope for v1 (tracked here, not silently ignored)

- **`ToMany` relations** — stored in a *separate* LMDB relation-index structure
  (different key-type byte, not part of the object's FlatBuffers table at
  all). Needs its own walk once we have a real schema that uses one.
- **`Flex` properties** (`PropertyType` 13) — dynamic property stored as an
  embedded FlexBuffers blob (a different encoding from the table-based
  FlatBuffers we decode; would need its own recursive decoder, not just
  another `switch` branch). Deliberately excluded from the "cover every
  remaining type" pass — different enough in kind to warrant its own scoped
  effort rather than being bundled in.
- The newer `ExternalPropertyType` annotation layer (`int128`, `uuid`,
  `decimal128`, `flexMap`, `flexVector`, `json`, `bson`, `javaScript`) that
  can sit on top of a base property type.
- The `UNSIGNED` property flag (see "Known gap" above).
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
8. **Full `PropertyType` coverage + unit tests** — done. Added the remaining
   13 scalar/vector types (see "Scope" above) and `tests/fb_decode_test.cpp`.
   Deliberately prioritized ahead of the Dart wrapper (step 9) on request:
   the goal is to close out type coverage once and not have to revisit
   `fb_decode.cpp` per-type again later, even though ebalistyka's own real
   schema only needs the original 7. `Flex`/`ToMany` stayed out of scope by
   explicit choice — different enough in kind (a different encoding /
   a different LMDB structure, respectively) to deserve their own pass.
9. **Schema export (`--schema`, `--fbs`)** — done. See "Schema export"
   section above for the design and the real-`flatc` end-to-end
   verification. Inserted ahead of language bindings since it was a small,
   self-contained addition requested in the middle of this phase; doesn't
   change the priority of what follows.
10. **Dart: reader toolkit, not an FFI binding** — done, supersedes the
    original plan for Dart specifically (quoted below for the record).
    `dart/` is a small standalone pub.dev package (`ob_dump_reader`, MIT,
    `dart pub publish --dry-run` clean, 0 warnings) that only does LMDB
    traversal + ObjectBox key parsing (`readObjectBoxRecords()` — ported
    from `tools/ob_migration_poc`'s Dart code, minus the FlatBuffers decode
    part), depending on `dart_lmdb2` alone. Decoding is left entirely to
    official `flatc --dart` output generated from this project's own `--fbs`
    — **no FFI to ob-dump's C++ core, no vendored native sources, no
    `build_native` script needed for Dart at all.** Verified end-to-end
    against a real database: generated `schema.fbs` from the real model, ran
    `flatc --dart`, read a real `Ammo` record with `readObjectBoxRecords`,
    decoded it with the generated class — every field matched the same
    values already confirmed by three earlier, independent verifications
    (Dart PoC, this project's own C++ decoder, a C++ `flatc` consumer).
    Other languages can follow the identical pattern (their own LMDB
    binding + `flatc --<lang>` + this project's `--schema`/`--fbs`) with no
    C++ FFI needed either, as long as *a* LMDB binding exists for that
    language — which is common (LMDB is a popular embedded store).
    ob-dump's own C ABI remains valuable independently: as a
    zero-additional-dependency CLI/library for anyone who doesn't want to
    write any code at all (just wants a JSON dump), and for languages
    without a usable LMDB binding, where FFI to the C core would still be
    the fallback.

    <details><summary>Original plan (superseded for Dart, kept for context)</summary>

    Thin wrappers per target language calling the C ABI directly, starting
    with Dart (`dart:ffi`) since that's what unblocks ebalistyka's actual
    migration. Likely needs a pub.dev package that vendors this repo's C/C++
    sources and builds them via a `build_native`-style script (same shape as
    `dart_lmdb2`/`dart_bclibc`), rather than depending on a prebuilt binary.
    Python (`ctypes`/`cffi`) etc. can follow the same pattern later. Each
    wrapper is expected to be small since all logic lives in the core.

    </details>
11. **Alternate output formats** (future) — MessagePack/CBOR writer, or a
    direct-to-SQLite writer (via the `sqlite3` C amalgamation) as an
    alternative to JSON, selected by an output-format parameter — useful for
    projects (like ebalistyka) whose actual migration target is SQL, skipping
    the JSON intermediate entirely.
12. **`OB_DUMP_SOURCE_BUFFER` input mode** (future) — LMDB's API only opens
    real files; a pure in-memory buffer input can't be handed to
    `mdb_env_open` directly. On Linux, back it with `memfd_create` + one
    `write()` so LMDB still mmaps a real fd without touching disk — the
    closest practical approximation to zero-copy for this input mode.
