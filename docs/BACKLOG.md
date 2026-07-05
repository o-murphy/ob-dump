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

**`UNSIGNED` flag — fixed.** ObjectBox tracks signedness via a separate
`flags` bitmask on the property (`UNSIGNED = 8192`,
`lib/src/modelinfo/enums.dart`), not via a distinct `PropertyType` code —
e.g. an unsigned `Long` and a signed `Long` are both type code 6.
`Schema::parse` now reads `flags` (`PropertyDef::isUnsigned`), and
`fb_decode.cpp` dispatches to the unsigned template instantiation
(`uint8_t`/`uint16_t`/`uint32_t`/`uint64_t`) for `Byte`/`Short`/`Int`/
`Long`(+`Date`/`Relation`/`DateNano`) and their vector forms when set —
`nlohmann::json` serializes `uint64_t` exactly, so a value above
`INT64_MAX` no longer flips sign. `--fbs` generation picks the matching
unsigned `.fbs` keyword (`ubyte`/`ushort`/`uint`/`ulong`) too, so a
`flatc`-generated reader in any language agrees on signedness, not just
width. Covered by `tests/fb_decode_test.cpp`
(`testUnsignedIntegersDecodeCorrectly`) and `tests/fbs_gen_test.cpp`. Note:
no real schema encountered so far (including ebalistyka's) actually sets
this flag — checked by scanning ebalistyka's real `objectbox-model.json`
for any `flags` value with the `8192` bit set (none found) — so this is a
correctness fix for generality, not something that changed any real output.

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
- **`ExternalPropertyType` — partially implemented.** This is a semantic
  annotation *on top of* a base `PropertyType` (the `"externalType"` field
  on a property in model.json, numeric codes from the official `objectbox`
  Dart package, `lib/src/modelinfo/enums.dart`, class
  `OBXExternalPropertyType` — starting at 100 specifically so they never
  collide with `PropertyType`'s own codes). The base type still determines
  the wire encoding; `externalType` only changes what the bytes *mean*.
  Handled: `Uuid`(102)/`Int128`(100)/`Decimal128`(103)/`Bson`(110) — all
  physically a `ByteVector` — now decode as a hex string (`Uuid`
  additionally grouped into canonical `8-4-4-4-12` form) instead of a JSON
  array of small integers (`fb_decode.cpp`'s `decodeByteVectorAsBlobString`;
  `--fbs` generation adds a `// external type: X` comment, since the `.fbs`
  field type itself doesn't change). `Json`(109)/`JavaScript`(111)/
  `JsonToNative`(112) need no extra work — their base type is `String`,
  already decoded correctly; the only possible improvement there is
  cosmetic (parsing the string content as nested JSON instead of leaving it
  as an escaped JSON string), not attempted. **Not implemented:**
  `FlexMap`(107)/`FlexVector`(108) (base type `Flex`, already out of scope
  — see above), `Int128Vector`(116)/`UuidVector`(118) (would need a
  vector-of-byte-vectors wire structure, not one of our current vector
  categories), and the Mongo-specific codes (`MongoId` and friends, rare,
  not researched). Like `UNSIGNED`, no real schema encountered so far
  (including ebalistyka's) actually sets `externalType` — checked the same
  way, found none.
- Big-endian host support — **already correct, not actually a gap** (this
  entry previously claimed otherwise; corrected after checking). FlatBuffers'
  own `ReadScalar<T>`/`EndianScalar` (`flatbuffers/base.h`) already
  byte-swaps on a big-endian host at every scalar/vtable/vector read —
  that's a core, documented FlatBuffers design guarantee, and everything we
  read goes through it (`Table::GetField`, `Vector<T>::Get`, `GetRoot`,
  ...). The only hand-rolled, non-flatbuffers byte read in this codebase is
  `lmdb_reader.cpp`'s `readUint32BE` for the LMDB key's `object_id` — built
  from individual `uint8_t` shifts, not a memory reinterpret-cast, so it's
  endian-neutral by construction regardless. Grepped this project's own
  code for any other raw `reinterpret_cast`/manual multi-byte read that
  could bypass `EndianScalar`: none found. Caveat: this is verified by
  source inspection of the exact mechanism (FlatBuffers' own endian
  handling, a stated design goal of the format) and reasoning about our own
  code, not by actually running on real big-endian hardware — none was
  available to test against.
- Windows/macOS build coverage: now CI-checked (`.github/workflows/cpp.yml`,
  `.github/workflows/dart.yml`, a 3-OS matrix each), but *only* CI-checked —
  no development or manual verification has happened on either platform,
  Linux is still where this project is actually developed
  (Flathub-first priority, matching ebalistyka's own). One known fix made
  for portability: `lmdb_reader.cpp`'s file-type check used POSIX
  `stat()`/`S_ISREG` (not available as-is on MSVC) — switched to
  `std::filesystem::is_regular_file` (C++17, already our language
  standard, portable). The Dart side inherits whatever platform coverage
  `dart_lmdb2` itself has — its own README lists Linux/Windows/macOS/
  Android/iOS.

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

    **Why `readObjectBoxRecords` also copies `lock.mdb`, not just
    `data.mdb`:** `lock.mdb` isn't part of the data at all — it's LMDB's own
    coordination file, holding (a) the mutex enforcing a single active
    write transaction, and (b) a reader table (PID + which MVCC snapshot
    each active reader holds), so a writer knows which old pages a reader
    might still reference and can't reclaim yet. Neither matters for us:
    we're always the only reader, on a disposable copy, never concurrent
    with the original app. The only reason a write-capable transaction (and
    therefore a lock file) is needed at all is `dart_lmdb2`'s own dbi-handle
    registration requirement (see above) — not anything intrinsic to
    reading. If `lock.mdb` doesn't exist in the source directory, that's
    fine as-is (`readObjectBoxRecords` only copies it `if` present); LMDB
    just creates a fresh, valid one in the temp copy on open. Copying the
    original when present is closer to "faithfully mirror the source
    directory" than a hard requirement.

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
13. **Streaming API + unsafe Dart variant + Dependabot** — done, in response
    to a large-database concern raised about item 10:
    - `LmdbReader::forEachObject`'s callback now returns `bool` (continue/
      stop) instead of `void`, so a streaming consumer can bail out early.
    - New `dumpStreaming()` (internal) / `ob_dump_stream()` (public C API):
      invokes a callback once per decoded record instead of building the
      whole database as one in-memory JSON tree first — `ob_dump()`/
      `dumpToJson()` still do that, and remain the simple default. Verified
      against the real ebalistyka database via the built `.so` directly
      (12 records, same field values as every earlier verification).
      Covered by `tests/dumper_stream_test.cpp` (a hand-built minimal
      FlatBuffers table + raw `mdb_put` fixture — all-records-in-order and
      early-stop cases).
    - Dart's `readObjectBoxRecords` already streamed by design (per-record
      callback during the cursor walk, nothing accumulated) — the actual
      cost for large databases there is the safety copy of
      `data.mdb`/`lock.mdb` before opening. Added
      `readObjectBoxRecordsUnsafe` (same signature, no copy, reads the
      given directory directly) for callers who know the source isn't in
      use by anything else and want to skip that cost.
    - The CLI's `--json` mode now uses `ob_dump_stream()` internally instead
      of `ob_dump()`, writing incrementally to stdout/`-o` file rather than
      building the whole result in memory first — memory use is O(1) per
      record, not O(total data size), with no opt-in flag needed. This is
      possible without any lookahead/buffering because LMDB sorts keys
      byte-for-byte and `entity_id` is the byte that determines order among
      object-data keys (everything before it in the key is constant) — a
      plain forward cursor walk already visits every record of one entity
      contiguously before moving to the next, so "did the entity change
      since the last record" is enough to know when to close/open the
      `"EntityName": [...]` brackets. One visible tradeoff: each record's
      fields are written as one compact JSON line (`ob_dump_stream()` hands
      records already serialized) rather than fully indented like
      `ob_dump()`'s output — still valid, readable JSON, just not
      byte-identical formatting. Verified against real data (all 10
      entities present, correct content) and an empty-database edge case
      (`{}`).
    - Added `.github/dependabot.yml` (`pub` ecosystem for `dart/`). Note:
      the C++ core's dependencies are pinned via `GIT_TAG` in the root
      `CMakeLists.txt`'s `FetchContent_Declare` calls — Dependabot has no
      CMake/FetchContent ecosystem, so those stay manually bumped.
14. **`build_runner` codegen on top of this toolkit** (future, speculative —
    not scoped or started, captured here so the idea isn't lost). The
    README "Building your own reader in another language" workflow is
    already fully scriptable: `ob_dump --schema`/`--fbs` → `flatc --dart` →
    hand-write the `entityId` dispatch. A `build_runner` `Builder` package
    could automate the whole thing for a Dart project: given
    `objectbox-model.json` as an input asset, shell out to `ob_dump --fbs`
    and `flatc --dart`, then generate a small dispatch entrypoint (e.g. a
    `switch (entityId) { case ammoId: return AmmoReader(...); ... }`) as one
    `<name>.g.dart` output — turning today's five manual steps into one
    `dart run build_runner build`. Would need: deciding whether `ob_dump`/
    `flatc` are expected as pre-installed system binaries or fetched by the
    builder itself (the latter is friendlier but reintroduces a
    build-time-network-fetch question similar to this project's own
    `FetchContent` tradeoffs — see "Design decisions" above); and a real
    Dart project to validate the generated dispatch code against, not just
    the raw `flatc` output already verified. Not needed unless someone
    other than ebalistyka's one-time migration actually wants this —
    ebalistyka's own use is a single run, where the manual workflow is
    already fine.
15. **Cross-platform CI** — done: `.github/workflows/cpp.yml` and
    `.github/workflows/dart.yml`, each a 3-OS matrix (`ubuntu-latest`,
    `macos-latest`, `windows-latest`) running the existing test suites
    (`ctest`, `dart test`). This checks the *build*, not real ObjectBox
    data — none of the real-database verification done during development
    (against ebalistyka's actual `data.mdb`) is repeated in CI, since that
    file is personal user data, not something to commit as a fixture.
    Fixed one real portability bug found while wiring this up (see
    "Explicitly out of scope" above: `lmdb_reader.cpp`'s POSIX
    `stat()`/`S_ISREG` → `std::filesystem::is_regular_file`).
    `cpp.yml` also uploads the built CLI binary + shared library + public
    header as a per-OS `actions/upload-artifact` (`ob-dump-<os>`) — glob
    patterns cover both the single-config layout (Linux/macOS, `build/`
    directly) and Windows' multi-config Visual Studio generator
    (`build/Release/`) without an OS-specific step; verified the Linux glob
    against this project's own local build output, not able to verify the
    Windows/macOS paths the same way (see caveat below). Important
    caveat: these workflows have not actually been run yet — this repo has
    no commits pushed to a remote yet (local commits are pending on the
    user's own GPG signing setup), so "the C++/Dart core builds and its
    unit tests pass on Windows and macOS" is reasoned through (portable
    libraries throughout: LMDB officially supports Windows, `dart_lmdb2`
    lists Windows/macOS support in its own README) but **not yet
    empirically confirmed** on those two platforms — only Linux has
    actually been built and tested so far. Confirm once pushed.
16. **`UNSIGNED` flag + partial `ExternalPropertyType` + big-endian doc
    correction** — done, see "Scope" and "Explicitly out of scope" above
    for full detail. Summary: integers now decode with correct signedness;
    `Uuid`/`Int128`/`Decimal128`/`Bson` `ByteVector`s decode as hex/UUID
    strings instead of int arrays; `Json`/`JavaScript` needed no change
    (already `String`); big-endian support turned out to already be
    correct (FlatBuffers' own `EndianScalar` handles it), so that entry was
    a documentation correction, not a code fix. New tests in
    `fb_decode_test.cpp`/`schema_json_test.cpp`/`fbs_gen_test.cpp`; full
    suite + real-data regression re-verified (same 4 known diffs as every
    prior verification, zero new ones).
17. **`flutter/` package (`ob_dump_reader_flutter`)** — done. `dart/`
    (`ob_dump_reader`) depends on `dart_lmdb2`, whose native library is
    fetched via `dart run dart_lmdb2:fetch_native` into the *pub cache* —
    fine for a `dart run` CLI script, but not part of a compiled
    Android/iOS/macOS app bundle a real user installs. `dart_lmdb2`'s own
    README points at a separate package, `flutter_lmdb2`, for that case — a
    genuine Flutter plugin (Android Gradle / iOS podspec / macOS) that
    bundles the native library properly and, per its own `lib/lmdb.dart`
    (`export 'package:dart_lmdb2/lmdb.dart';` — checked by downloading and
    inspecting the actual published package, not assumed), re-exports
    `dart_lmdb2`'s identical Dart API.

    Rather than duplicating `readObjectBoxRecords`'s logic into a second
    package that imports `flutter_lmdb2` instead of `dart_lmdb2`, `flutter/`
    is a near-empty wrapper: its `lib/ob_dump_reader_flutter.dart` is just
    `export 'package:ob_dump_reader/ob_dump_reader.dart';`, and its
    `pubspec.yaml` depends on both `ob_dump_reader` (path dep during
    development) and `flutter_lmdb2`. No source file here imports
    `flutter_lmdb2` directly — it only needs to be present in the resolved
    dependency graph, because Flutter's plugin-discovery tooling scans the
    *whole* graph (not just an app's direct dependencies) for packages
    declaring `flutter: plugin:` and wires up their native bundling
    regardless of depth.

    **Verified empirically, not just reasoned through:** created a scratch
    `flutter create` app, added `ob_dump_reader_flutter` as its only new
    dependency (no direct mention of `flutter_lmdb2` anywhere in the app's
    own code or pubspec), ran `flutter pub get`, and confirmed
    `.flutter-plugins-dependencies` lists `flutter_lmdb2` under `android`/
    `ios`/`macos` with `native_build: true`, and the generated
    `GeneratedPluginRegistrant.java` actually instantiates
    `com.grammatek.flutter_lmdb2.FlutterLmdb2Plugin()`. Confirms the
    transitive-plugin-discovery mechanism this design relies on actually
    works, not just that it should in theory.

    Known loose end: `flutter analyze` reports one expected warning
    (`Publishable packages can't have 'path' dependencies`) because
    `ob_dump_reader` isn't published to pub.dev yet, so `flutter/`'s
    dependency on it is a local `path:` — harmless during development
    (doesn't fail CI, `flutter analyze` exits 0 on warnings), needs
    switching to a real version constraint once `ob_dump_reader` is
    actually published. `.github/workflows/flutter.yml` added (3-OS
    matrix, `flutter pub get` + `flutter analyze`) — same "not run on a
    real remote yet" caveat as item 15 applies here too.
