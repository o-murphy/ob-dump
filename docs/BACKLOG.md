# ob-dump — architecture & backlog

`ob-dump` reads an ObjectBox LMDB database (`data.mdb`) directly — without linking
`objectbox-c` — and dumps its contents to JSON, driven entirely by the project's
own `objectbox-model.json` schema file. No `.fbs` / `flatc` codegen is involved:
decoding is schema-driven at runtime using the generic FlatBuffers `Table` API.

Born out of a real-world app's ObjectBox → SQL/keystore migration (a GPL-3.0
project blocked from bundling the closed-source `objectbox-c` binary on
Flathub). Ported from that project's own Dart migration PoC, which first
worked out the LMDB key format and vtable-slot decoding this design builds on.

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
leave retired ids — confirmed via a real-world app's model: e.g. entity `Ammo`
has ids `[1,2,6,7,8,9,10,18,20,22,29,...]`, with `retiredPropertyUids` present
in the model file), a reader must treat "slot beyond this record's vtable
size" as "field absent", not an error — this is normal forward/backward
compatibility, not corruption.

## Design decisions

| Decision | Choice | Why |
|---|---|---|
| Language | Pure C public ABI; implementation may use C++ where a dependency requires it (FlatBuffers' generic `Table`/`Verifier` API and nlohmann/json are C++ header-only libraries) | Public surface must be trivially bindable via CFFI from any language (Dart `dart:ffi`, Python `ctypes`/`cffi`, Go `cgo`, JVM JNI, Swift...). C++-ness is fully internal, hidden behind `extern "C"`. |
| Build | CMake + `FetchContent` | No network needed at *use* time, only at first configure; each dependency vendors/builds from source (Flathub/GPL-safe — no prebuilt binaries). |
| LMDB | Vendor `mdb.c`/`midl.c` from `LMDB/lmdb` upstream, compiled ourselves (no CMake shipped upstream) | Same trusted, source-buildable pattern as `dart_bclibc` (the ballistics engine binding) already uses in the app this project was born out of — `dart/` and `flutter/` (below) vendor and build the same LMDB source themselves too, rather than depending on a third-party binding package. |
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
no real schema encountered so far (including the real-world app database
this project was verified against) actually sets this flag — checked by
scanning its real `objectbox-model.json` for any `flags` value with the
`8192` bit set (none found) — so this is a correctness fix for
generality, not something that changed any real output.

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
source), ran it on the `.fbs` generated from a real-world app's real
`objectbox-model.json` (`flatc --cpp`, zero errors — only cosmetic
snake_case naming-convention warnings, since our field names come straight
from Dart property names). Extracted a real `Ammo` record's raw table bytes
and read them with the officially generated code, entirely independent of
this project's own `fb_decode.cpp`: `Verify()` succeeded, and every field
checked (`id`, `name`, `dragTypeValue`, `bcG1`, `bcG7`, `useMultiBcG1`,
`ownerId`, `caliberInch`, `weightGrain`, `muzzleVelocityMps`) matched our own
decoder's output exactly.

## Explicitly out of scope for v1 (tracked here, not silently ignored)

- ~~`ToMany` relations~~ — **done.** Wire format confirmed empirically
  first, then implemented against it. Previously blocked on "no real
  schema encountered that uses one" — resolved by building one: a scratch
  Dart-native project
  (`dart pub add objectbox`, `dart pub add --dev build_runner
  objectbox_generator:any`, the real `objectbox-c` native lib via
  `objectbox-dart`'s own `install.sh`), two entities (`Author`, `Book`)
  with `Book.authors`/`Book.editors` as two separate `ToMany<Author>`
  fields (to get two different relation ids to cross-check against, not
  just one), populated with real data, then cursor-walked the resulting
  `data.mdb` raw (via `dart_lmdb2` directly — every key + value length,
  no filtering), not just decoded through our own existing key-type
  assumptions.

  **Confirmed key format**, cross-checked against two independent
  relation ids (`authors` = relation id 1, `editors` = relation id 2) so
  it isn't a one-data-point coincidence:

  ```
  [type:1 = 0x08][0x00][0x00][(relation_id << 2) | direction:1][source_id:u32 BE][target_id:u32 BE]
  ```

  12 bytes total, value always **empty** (0 bytes) — the key alone *is*
  the link; presence means the relation exists, nothing else to decode.
  `direction`: `0` = forward, in the declared direction (owning entity's
  id → target entity's id, e.g. `Book#1 → Author#1`); `2` = backward, an
  automatically-maintained reverse index (target's id → owning entity's
  id, e.g. `Author#1 → Book#1`) that a plain forward relation-id walk
  would otherwise miss for "which books reference this author"-style
  queries. Both directions observed for both test relations
  (`authors`: byte4 `0x04`/`0x06` = `1<<2 | 0`/`1<<2 | 2`; `editors`:
  `0x08`/`0x0a` = `2<<2 | 0`/`2<<2 | 2`) — the shift-by-2 (leaving 2 low
  bits for direction) mirrors the same convention this project already
  relies on for `entity_id*4` in the 8-byte object-data key.

  This key type (`0x08`) is distinct from the `0x00` (schema) and `0x18`
  (object data) types already documented above, and from the `0x20`
  ("index") byte this doc previously guessed at — `0x20` is unverified by
  this investigation (no `@Index()` property was tested) and may refer to
  something else entirely (a property index, not a relation).

  **Implemented** as a separate lookup, not folded into `decodeObject`:
  `EntityDef` gained a `relations` list (`RelationDef{id, name,
  targetEntityId}`, parsed from model.json's `"relations"` array —
  `schema.cpp`), and `LmdbReader::toManyTargets(relationId,
  sourceObjectId)` does the `MDB_SET_RANGE` cursor lookup described above
  (forward direction only — the reverse index exists for ObjectBox's own
  query engine, not needed for a one-directional dump) and returns the
  target ids as a plain `vector<uint32_t>`. `dumper.cpp`'s `dumpToJson`/
  `dumpStreaming` call it once per relation per record, after
  `decodeObject`, and add the result as `obj[relationName] = [...]` — a
  plain array of target ids, the same "just the fk, no eager nested
  object" shape already used for `ToOne` (`PropertyType::Relation`).
  Chosen over recursively embedding the full target objects: avoids
  duplicate/circular-reference concerns for free, and matches how a
  consumer would already have to look up `ToOne` targets by id anyway.
  Also surfaced in `--schema`'s JSON output (`schema_json.cpp`) as a
  `"relations": [{"id", "name", "targetEntityId"}]` array on the owning
  entity, omitted when empty (same convention as `"unsigned"`/
  `"externalType"`) — **not** added to `--fbs` generation, since a
  `flatc`-generated reader in another language would need this same
  separate LMDB lookup itself; there's nothing about it representable in
  the FlatBuffers table shape `.fbs` describes.

  Covered by `tests/schema_test.cpp` (relation parsing, including entities
  with none), `tests/schema_json_test.cpp` (`--schema` output), and
  `tests/dumper_stream_test.cpp` (a real hand-built LMDB fixture with both
  forward and backward relation-link keys, confirming the decoded output
  only picks up the forward ones). Re-verified against the real-world app
  database: its schema declares zero `ToMany` relations (checked all
  entities' `"relations"` arrays — all empty), so this is a true no-op
  there, and the existing `--json` output is unchanged (still the same
  18 records across 10 entities as every prior run).

  **Dart side too** — `dart/ob_dump_reader` gained `readToManyTargets()`/
  `readToManyTargetsUnsafe()`, the same `MDB_SET_RANGE`-based lookup as the
  C++ side (`dart_lmdb2`'s `CursorOp.setRange`, confirmed available before
  committing to this — see `lib/src/relations.dart`), since `flatc --dart`
  output has no accessor for a relation at all. Prompted by checking
  whether the Dart package had parity with every C++-side decode
  capability — it didn't, for `ToMany` and also for `Flex`/
  `ExternalPropertyType` (see below), not just relations. Covered by
  `test/relations_test.dart` (same forward/backward-link fixture shape as
  the C++ test).

  **Also added while checking that parity — `Flex`/`ExternalPropertyType`
  decode helpers for Dart**: `flatc --dart` output only knows the base
  FlatBuffers field type, so a `Flex` field comes through as raw
  `List<int>`/`Uint8List`, and an `ExternalPropertyType` field as its
  plain base value (bytes for `Uuid`/`Int128`/`Decimal128`/`Bson`, a
  string for `Json`/`JavaScript`/`JsonToNative`) — exactly the gap this
  package's C++ counterpart closes with real decoding. Added
  `decodeFlex()` (wraps the *official* `flat_buffers` pub package's own
  FlexBuffers reader, `package:flat_buffers/flex_buffers.dart` — same
  Dart runtime `flatc --dart` output already depends on, so this added
  zero new *kind* of dependency, just promoted an already-present
  `dev_dependency` to a real one) plus `bytesToHex()`/
  `bytesToUuidString()`/`tryParseJsonString()`, each mirroring the C++
  decoder's exact logic (canonical UUID grouping, JSON-parse-with-string-
  fallback for `JavaScript`). Covered by `test/decode_helpers_test.dart`,
  using `flat_buffers`'s own `Builder.buildFromObject()` to build real
  FlexBuffers fixtures rather than hand-rolling bytes.
- ~~`Flex` properties~~ — **done, see phased-plan item 20 below.**
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
  `JsonToNative`(112) — done: their base type is `String`, already decoded
  correctly, and the string content is now also parsed as JSON
  (`decodeStringAsJson` in `fb_decode.cpp`) instead of being left as one
  extra layer of escaped-string JSON. Falls back to the raw string on
  parse failure (covered by a test: a `JavaScript` field holding an actual
  JS function expression, not valid JSON), since `JavaScript` content isn't
  guaranteed to be valid JSON the way `Json`/`JsonToNative` are expected to
  be. `FlexMap`(107)/`FlexVector`(108)
  need no extra work either now that `Flex` itself decodes (item 20 below):
  they're just a semantic promise that the root value is specifically a map
  or vector, which the generic `Flex` decoder already handles regardless.
  **Deliberately not implemented — `Int128Vector`(116)/`UuidVector`(118):
  the wire format is genuinely undocumented, not just unimplemented here.**
  Checked the authoritative source (`objectbox.h` from the official
  `objectbox` Dart package, `~/.pub-cache/hosted/pub.dev/objectbox-5.3.x`):
  every other `OBXExternalPropertyType` value carries an explicit
  `Representing type: X` doc comment (`Json` → `String`, `Bson` →
  `ByteVector`, `MongoBinary` → `ByteVector`, `MongoRegex` → "string vector
  with 2 elements", etc.) — `Int128Vector`, `UuidVector`, and
  `MongoIdVector`(124) are the only three that say just "a vector (array)
  of X values" with **no `Representing type:` line at all**. That's not
  an oversight in one place — checked all three, all missing it the same
  way, unlike every neighboring code. Our own earlier guess here ("would
  need a vector-of-byte-vectors wire structure") was exactly that — a
  guess, not something confirmed against the spec. Plausible actual
  encodings: a flat `ByteVector` of N×16 concatenated bytes (matching how
  scalar `Uuid`/`Int128` already reuse `ByteVector`), a genuine
  vector-of-offsets-to-`ByteVector` structure, or simply not yet
  implemented in `objectbox-c` itself the way `Json`/`Bson` are explicitly
  flagged "Placeholder (not yet used)" (just without that same label).
  Implementing against a guess risks silently decoding wrong values
  instead of failing loudly — worse than leaving it out. Left unimplemented
  until a real schema using this exists to verify against; the
  Mongo-specific codes (`MongoId` and friends) are equally out of scope,
  same reasoning, lower priority (Mongo interop isn't a target use case
  here at all). Like `UNSIGNED`, no real schema encountered so far
  (including the real-world app database this project was verified
  against) actually sets `externalType` — checked the same way, found none.
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
  (Flathub-first priority, matching the app it was born out of). One known fix made
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
   matching the shape the reference Dart migration PoC this project ports
   from already produces, so outputs can be diffed 1:1 against it during
   verification.
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
   `fb_decode.cpp` per-type again later, even though the real-world app's
   own real schema only needs the original 7. `Flex`/`ToMany` stayed out of scope by
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
    with Dart (`dart:ffi`) since that's what unblocks the app this project
    was born out of. Likely needs a pub.dev package that vendors this repo's C/C++
    sources and builds them via a `build_native`-style script (same shape as
    `dart_lmdb2`/`dart_bclibc`), rather than depending on a prebuilt binary.
    Python (`ctypes`/`cffi`) etc. can follow the same pattern later. Each
    wrapper is expected to be small since all logic lives in the core.

    </details>
11. **Alternate output formats** (future) — MessagePack/CBOR writer, or a
    direct-to-SQLite writer (via the `sqlite3` C amalgamation) as an
    alternative to JSON, selected by an output-format parameter — useful for
    projects (like the app this was born out of) whose actual migration
    target is SQL, skipping the JSON intermediate entirely.
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
      against the real-world app database via the built `.so` directly
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
    other than this project's own one-time migration use case actually
    wants this — that use is a single run, where the manual workflow is
    already fine.
15. **Cross-platform CI** — done: `.github/workflows/cpp.yml` and
    `.github/workflows/dart.yml`, each a 3-OS matrix (`ubuntu-latest`,
    `macos-latest`, `windows-latest`) running the existing test suites
    (`ctest`, `dart test`). This checks the *build*, not real ObjectBox
    data — none of the real-database verification done during development
    (against the real-world app's actual `data.mdb`) is repeated in CI,
    since that file is personal user data, not something to commit as a
    fixture.
    Fixed one real portability bug found while wiring this up (see
    "Explicitly out of scope" above: `lmdb_reader.cpp`'s POSIX
    `stat()`/`S_ISREG` → `std::filesystem::is_regular_file`).
    `cpp.yml` also uploads the built CLI binary + shared library + public
    header as a per-OS `actions/upload-artifact` (`ob-dump-<os>`) — glob
    patterns cover both the single-config layout (Linux/macOS, `build/`
    directly) and Windows' multi-config Visual Studio generator
    (`build/Release/`) without an OS-specific step; verified the Linux glob
    against this project's own local build output, and now also confirmed
    on Windows and macOS the same way this whole project verifies anything
    else — not just reasoned through: pushed to `origin`/`gitlab` and
    watched real runs (`gh run list --workflow=release.yml`), including a
    real, non-dry-run release matrix (`gh run view 28755549996`) with
    every leg green — `ubuntu-latest`, `ubuntu-24.04-arm`,
    `windows-latest`, `windows-11-arm`, and the `macos-latest` universal
    build. Both artifact-glob variants (single-config `build/` vs.
    Windows' multi-config `build/Release/`) are therefore empirically
    confirmed, not just portability-reasoned.
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

    Loose end, now resolved by the real 0.1.0-alpha.0 release (see item
    18): `flutter analyze` used to report one expected warning
    (`Publishable packages can't have 'path' dependencies`) while
    `ob_dump_reader` wasn't on pub.dev yet — harmless during development
    (doesn't fail CI, `flutter analyze` exits 0 on warnings), and only ever
    meant to be swapped to a real version constraint at actual-publish
    time, not permanently in the committed file (see the `TODO(release)`
    comments in `flutter/pubspec.yaml` itself). `.github/workflows/flutter.yml`
    (3-OS matrix, `flutter pub get` + `flutter analyze`) has since actually
    run — repeatedly, on real pushes — same as item 15's build/test
    matrix.
18. **Release process: keeping `dart/` and `flutter/` versions in lockstep**
    — done, and now exercised by a real release. Since `flutter/` is purely
    a re-export of `dart/`, there's no reason for their version numbers to
    ever diverge — so the rule is: **always bump both `pubspec.yaml`s
    together, to the same version, in the same commit**, and tag both
    (`dart-vX.Y.Z`/`flutter-vX.Y.Z` — matches the tag URLs already written
    into both `CHANGELOG.md`s' link references). Added
    `.github/workflows/version-parity.yml`: a single cheap job (no OS
    matrix needed) that greps both `pubspec.yaml`s' `version:` line and
    fails the build with an explicit `::error::` message the moment they
    differ — verified locally (`0.1.0` == `0.1.0` today) before adding the
    CI check. This turns "remember to keep them in sync" from a purely
    manual discipline into something CI actually enforces.

    **`0.1.0-alpha.0` shipped for real** — confirmed via pub.dev's own API
    (`curl https://pub.dev/api/packages/ob_dump_reader` /
    `.../ob_dump_reader_flutter`, not assumed): both packages are live at
    `0.1.0-alpha.0`, and the published `ob_dump_reader_flutter` pubspec
    shows `ob_dump_reader: ^0.1.0-alpha.0` — a real version constraint, not
    the path dep. The publish itself was done by hand (the user ran the
    actual `pub publish` locally), not via `release.yml`'s
    `publish-dart`/`publish-flutter` jobs — separately, `release.yml` has
    also been run for real (`gh run view 28755549996`, all jobs green,
    including `bump-versions`/`create-release`/`publish-dart`/
    `publish-flutter`), so the automated path is verified to work
    end-to-end too; the two just haven't been the same run yet. Either way,
    the pattern that matters is unchanged: `flutter/pubspec.yaml`'s
    checked-in `path: ../dart` + `publish_to: none` are permanent (for
    local dev), and the swap to a real pub.dev constraint only ever exists
    ephemerally at publish time (see the `TODO(release)` comments in that
    file) — never committed back, whether that swap is done by the CI job
    or by hand.
19. **`.github/workflows/release.yml`** — done: a single `v*` tag push now
    drives the entire release (based on this project's own prior
    `a7p-dart` release workflow pattern, adapted for a monorepo with a C++
    core plus two lockstep Dart packages instead of one plain Dart
    package). Jobs, in dependency order:
    - `build-cpp` — 5-leg **native** matrix (no cross-compilation/QEMU):
      `ubuntu-latest`(x64)/`ubuntu-24.04-arm`(arm64)/
      `windows-latest`(x64)/`windows-11-arm`(arm64) — all confirmed GA (not
      preview) via `actions/runner-images`' own README, not guessed — plus
      a `macos-latest` **universal** binary (`CMAKE_OSX_ARCHITECTURES=
      "arm64;x86_64"`, same pattern `dart_lmdb2`'s own CMakeLists.txt
      already uses) instead of two separate macOS legs. Builds, tests
      (natively — real ARM64 hosts, no emulation), packages the CLI +
      shared lib + header into `ob-dump-<platform>.tar.gz`, uploads as a
      build artifact.
    - `test-dart` / `test-flutter` — same checks as the regular `dart.yml`/
      `flutter.yml` CI, run again here as a release gate.
    - `bump-versions` (needs all three above) — strips the tag's `v`
      prefix, writes that version into *both* `dart/pubspec.yaml` and
      `flutter/pubspec.yaml` (lockstep, one commit), pushes to `main`. Same
      "tag points at the pre-bump commit, later jobs must checkout `main`
      by name, not the default ref" pattern as the `a7p-dart` reference.
    - `create-release` — downloads every `build-cpp` leg's archive,
      extracts that version's section from `dart/CHANGELOG.md` for the
      release body (found a real bug while testing this locally before
      trusting it in CI: the original awk pattern only stopped at the next
      `## [` heading, so on the *most recent* version — nothing bounds it
      from below — the CHANGELOG's own trailing `[x.y.z]: https://...`
      link-reference lines leaked into the extracted notes; fixed by also
      stopping at a line matching `^\[.+\]:`), creates the GitHub Release
      with all platform archives attached.
    - `publish-dart` — checks out `main` (post-bump), `dart pub publish`.
      Uses pub.dev's OIDC trusted-publishing (`permissions: id-token:
      write`), same as this project's own `a7p-dart` release workflow —
      **requires the user to configure trusted publishing for
      `ob_dump_reader` on pub.dev first**, not something this file can set
      up by itself.
    - `publish-flutter` (needs `publish-dart`) — **gated behind a GitHub
      Environment (`pub-publish-flutter`) requiring manual approval**,
      specifically to compensate for pub.dev's indexing lag: this job's
      pubspec depends on the `ob_dump_reader` version `publish-dart` just
      published, and resolving that immediately could hit pub.dev before
      its index has caught up. **Requires the user to add a "required
      reviewers" protection rule to that environment in this repo's
      Settings > Environments** — a workflow file cannot configure that
      itself. Before installing dependencies, ephemerally rewrites (never
      committed — `dart/pubspec.yaml`'s path dep stays as-is in the repo
      for local dev) the checkout's own `flutter/pubspec.yaml` dependency
      from the local `path: ../dart` to the just-published version, using
      `flutter pub remove ob_dump_reader && flutter pub add
      "ob_dump_reader@^$VERSION"` — proper pub tooling editing the
      pubspec structurally, not a text-substitution `sed`, per explicit
      request. (Caught the correct flag syntax by checking `flutter pub
      add --help` rather than assuming: it's `package@constraint`, not
      `package:constraint`.)

    **Two more manual prerequisites**, beyond the environment/trusted-
    publishing setup above: a `## [x.y.z]` section must already exist in
    `dart/CHANGELOG.md` before pushing the tag (`create-release` fails
    loudly, before creating anything, if it doesn't — verified locally by
    simulating both the "entry exists" and "entry missing" cases against a
    scratch copy of the real CHANGELOG); and this workflow, like items 15
    and 17, has **not actually been run for real** (see below — it *has*
    been dry-run tested against real CI, which caught several real bugs).

    **Real CI bugs this surfaced, on the *existing* `cpp.yml`/`flutter.yml`
    workflows** (checked via `gh run view --log-failed`, not guessed):
    - `tests/dumper_stream_test.cpp` used POSIX `mkdtemp()` for its temp
      fixture directory — undeclared on Windows (MSVC: `error C3861`) and
      unreliable via `<cstdlib>` on macOS's libc++ (`error: use of
      undeclared identifier`). Replaced with a small portable
      `std::filesystem`-based temp-dir helper (random suffix + retry loop —
      `std::filesystem` has no built-in "unique temp dir", unlike Python's
      `tempfile.mkdtemp`).
    - `flutter analyze`'s "path dependencies aren't publishable" issue
      isn't just a warning — it actually fails the command (exit 1),
      contradicting what an earlier BACKLOG entry claimed after checking
      locally. That local check was itself wrong: `flutter analyze | tail
      -5; echo $?` reports `tail`'s exit code, not `flutter analyze`'s
      (classic pipeline exit-status masking). Fixed by adding
      `publish_to: none` to `flutter/pubspec.yaml` for now (exactly what
      the tool's own message suggests) — `release.yml`'s real-publish step
      strips it back out ephemerally, alongside the path-dep swap (see
      below), since `publish_to: none` would otherwise block `pub publish`
      too and `pub remove`/`pub add` only manage dependencies, not that
      top-level field. Verified the removal with a plain `sed` test against
      the real file before trusting it in CI.
    - A genuine Windows-only linker collision in the existing `cpp.yml`
      (`LINK : fatal error LNK1104: cannot open file '...\ob_dump.exp'`):
      the `ob_dump` shared-library target and the `ob_dump_cli` executable
      (`OUTPUT_NAME ob_dump`) both resolve to the same base name in the
      same output directory. Invisible on Linux/macOS (`libob_dump.so`/
      `.dylib` vs. plain `ob_dump` are already distinct filenames), but on
      Windows a DLL's import-library side-effect (`ob_dump.lib`/`.exp`)
      collides with the same pair CMake/MSVC provisions by default for
      *any* target sharing that name — including a plain .exe. Fixed by
      setting `ARCHIVE_OUTPUT_NAME` (not `OUTPUT_NAME`) specifically for
      `ob_dump_cli`, so only that incidental archive-output naming changes
      — the actual `ob_dump`/`ob_dump.exe` binary path stays exactly what's
      already documented everywhere else. Rebuilt and re-ran the full test
      suite locally after this change to confirm no Linux regression (a
      real Windows CI run is still the only way to confirm the actual fix,
      not yet done).

    **`workflow_dispatch` dry-run support — added, then removed.** Originally
    added so the whole pipeline shape could be validated without pushing a
    real tag: a manual "Run workflow" took a `tag` input (simulated, didn't
    need to exist) and a `dry_run` boolean (defaulting to `true`), with
    `IS_DRY_RUN` gating every side-effecting step individually (version-bump
    commit/push, GitHub release creation, both `pub publish --force` calls).
    Removed by request once the pipeline had been dry-run-validated enough
    (see below) to trust for real — `release.yml` is now triggered solely by
    `push: tags: v*`, with no `workflow_dispatch` input and no `IS_DRY_RUN`
    conditionals left anywhere; every step in every job always runs for
    real. Re-validated with `actionlint` after removal — clean, same as
    before.

    **Validated so far, concretely, not just by review:**
    `actionlint` (a real GitHub-Actions-specific linter, downloaded via
    `gh release download` since it wasn't preinstalled — not just generic
    YAML syntax checking) passes clean on every workflow in this repo,
    including this one; it caught two real bugs before this was ever run —
    `needs.bump-versions.outputs.*` referenced from `publish-flutter`
    despite `bump-versions` only being a *transitive* dependency there (via
    `publish-dart`) — `needs.<job>.outputs` is only visible to a job's
    *direct* `needs:` entries, so `bump-versions` had to be added to
    `publish-flutter`'s `needs:` list explicitly even though the ordering
    was already correct either way; and a YAML "nested mappings not
    allowed" parse error from an unquoted colon inside a `run:`/`name:`
    string value (`"Dry run: nothing was..."`, `"...publish_to: none)"`) —
    fixed by quoting those particular values. Also locally simulated (on a
    scratch copy of the real files, not the actual pubspec/changelog) the
    version-strip, the lockstep `sed` bump, the changelog-extraction awk
    pattern against both a matching and a missing version section, and the
    `publish_to: none` removal — all before trusting any of them inside
    the actual workflow.
20. **`Flex` properties — done.** Recursive `flexbuffers::Reference ->
    nlohmann::json` converter in `fb_decode.cpp` (`flexToJson`, ~90 lines,
    matching the size estimate from when this was still "explicitly out of
    scope" — see the struck-through entry above). No new dependency:
    `flexbuffers.h` is the same header-only `google/flatbuffers` include
    already used for everything else. Maps/vectors/scalars/strings/blobs
    all handled; blobs get the same "opaque bytes -> hex string" treatment
    as `ExternalPropertyType`'s `Uuid`/`Int128`/etc. (factored `toHex()` out
    to a shared helper so both use the same code). Bounds-checked two ways,
    not one: the outer `flatbuffers::Verifier` only covers the `ByteVector`
    the Flex bytes live in — the FlexBuffers structure *inside* those bytes
    gets its own independent check via `flexbuffers::VerifyBuffer()` before
    `flexToJson` ever touches it. Covered by
    `tests/fb_decode_test.cpp`: a map root, a bare vector root (FlexBuffers
    values don't have to be maps), and a truncated/corrupted FlexBuffers
    blob correctly throwing despite passing the *outer* Verifier fine.
    `--fbs` generation is unchanged on purpose (still emits `[ubyte]` for
    `Flex` — a `flatc`-generated reader in another language has no way to
    know about our own `flexToJson`, so it still gets raw bytes and would
    need its own FlexBuffers decode to go further); doc comments updated to
    make that split explicit rather than implying `Flex` is unhandled
    everywhere.

    **A real, non-cosmetic bug surfaced while re-implementing this** (the
    first attempt was lost to what looks like an uncommitted-changes revert
    between turns — re-checked with `git status`/`git diff --stat` before
    redoing anything, rather than assuming): linking failed with undefined
    references to `flatbuffers::ClassicLocale::instance_`.
    `flatbuffers/base.h` auto-enables the locale-independent
    `strtod_l`/`strtoull_l` code path on Unix unconditionally (a plain
    `#if defined(__unix__)`, not something our CMake setup controls), but
    `ClassicLocale`'s static member is only *defined* in `util.cpp` — part
    of the compiled `flatbuffers`/`flatc` library this project deliberately
    never builds (see "FlatBuffers decode" in Design decisions: header-only
    only, specifically to avoid pulling in `idl_parser.cpp`/`reflection.cpp`
    and everything else that comes with `.fbs`/`flatc` support we don't
    want). Fixed by forcing `FLATBUFFERS_LOCALE_INDEPENDENT=0` via
    `target_compile_definitions` on `ob_dump_core` — falls back to plain
    `strtod`/`strtoull`, which is fine here since nothing in this project
    calls `setlocale()` (so "current" and "classic" C locale are the same
    thing regardless). Re-verified against the real-world app database
    after the fix: same 4 known diffs as every prior verification, zero
    new ones.
21. **`scripts/ci/` — reusable, locally-runnable CI logic — done.** The same
    build/test/package/changelog-extraction bash had drifted into two
    (`cpp.yml`/`dart.yml`/`flutter.yml` vs. `release.yml`'s equivalent
    jobs) or three copies each, and — since it only ever ran inside a
    workflow step — every bug in it (missing `dart_lmdb2:fetch_native`,
    the Windows import-lib collision, the changelog-extraction edge cases)
    was only discoverable by pushing and reading a run's log. Pulled the
    common logic into five standalone, `chmod +x`'d scripts instead:
    `build-cpp.sh` (configure + build + `ctest`, taking `OB_DUMP_VERSION`
    from the environment and extra CMake flags as `"$@"` — covers both
    plain CI and release's per-arch matrix, e.g. macOS's
    `CMAKE_OSX_ARCHITECTURES`), `package-cpp.sh` (the OS-conditional
    binary-locating + tarball step, artifact name as `$1`),
    `test-dart.sh`/`test-flutter.sh` (pub get + analyze [+ test/
    `fetch_native`]), and `extract-changelog.sh` (version-section lookup
    with dot-escaping and `[Unreleased]` fallback — deliberately scoped to
    just the text extraction, not the git-tag "Full Changelog" footer or
    dry-run pass/fail gating, since those are release-specific
    orchestration rather than something worth testing standalone). Every
    script was actually run locally against this repo (not just read) —
    `build-cpp.sh` built and passed all 5 ctest cases, `package-cpp.sh`
    produced a tarball with the expected 3 files, `extract-changelog.sh`
    correctly extracted the real `0.1.0-alpha.0` section from
    `dart/CHANGELOG.md` and correctly fell back to (empty)
    `[Unreleased]` for a made-up version. All four workflow files were
    rewired to call these scripts instead of inlining the bash, then
    re-validated with both `python3 -c "import yaml..."` and a freshly
    downloaded `actionlint` — clean on all of them.
22. **`dart/` and `flutter/` drop `dart_lmdb2`/`flutter_lmdb2` for a
    self-vendored `dart:ffi` binding — done, supersedes item 17.** Two
    concrete problems with the `dart_lmdb2`/`flutter_lmdb2` pair, both
    confirmed empirically (build a scratch Flutter app, run
    `flutter build linux --release`, copy only the output bundle to an
    isolated directory with no source project/pub cache, run it directly):
    `dart_lmdb2`'s own native-library path resolution
    (`Isolate.resolvePackageUriSync()`) throws `UnsupportedError` outright
    in any compiled AOT release build — breaks every compiled Flutter
    Linux/Windows release depending on it, regardless of how `liblmdb.so`
    was obtained; and `flutter_lmdb2` has no Linux or Windows support at all
    as published (both commented out in its own `pubspec.yaml`).

    Fix: both packages now vendor the same LMDB source the C++ core does
    (`src/lmdb/mdb.c`/`midl.c`/`lmdb.h`/`midl.h`, duplicated between
    `dart/` and `flutter/` since they're separately-published pub.dev
    packages with no reliable cross-package relative path) and bind to it
    directly via a small hand-written `dart:ffi` layer
    (`dart/lib/src/lmdb_bindings.dart`) instead of depending on a
    third-party binding package — the same "vendor and build ourselves"
    philosophy the "LMDB" row in Design decisions already uses for the
    C++ core.
    - `dart/`'s own loader (`lib/src/lmdb_native_library.dart`) tries
      `package:` URI resolution first (unchanged `dart run`/`flutter run`
      behaviour), then falls back per-platform to wherever each platform's
      real native-bundling convention places the library — confirmed
      against the official `flutter create --template=plugin_ffi` scaffold
      (generated it and read its own loading code), not guessed. Notably:
      macOS/iOS load a bare `<PodName>.framework/<PodName>` reference
      resolved by dyld's bundle-embedded search paths, *not*
      `DynamicLibrary.process()` — an earlier draft of this loader assumed
      the latter and was wrong, caught by reading that same reference
      scaffold before shipping it.
    - `flutter/` is now a real Flutter plugin (`ffiPlugin: true` — no
      method-channel surface at all, confirmed against the same official
      scaffold rather than using `pluginClass:`) for all five platforms.
      CMake (Linux/Windows, and Android via the NDK) all `add_subdirectory`
      one shared `flutter/src/CMakeLists.txt`; iOS/macOS use a CocoaPods
      podspec plus tiny forwarder `.c` files (`Classes/mdb.c` including
      `../../src/lmdb/mdb.c` — podspec globs can't cross `../`, this is the
      same trick the official scaffold's own reference example uses).
    - Opening the LMDB environment itself `MDB_RDONLY` (not just the
      transaction) — pointed out during review — turned out to remove the
      *entire* copy-to-temp/`readObjectBoxRecordsUnsafe` distinction from
      item 13, not just work around `dart_lmdb2`'s dbi-handle requirement:
      LMDB's MVCC design already lets any number of readers run safely
      alongside one concurrent writer, in any process, so there was never
      a need for a write-capable environment at all once we controlled the
      binding ourselves. `readObjectBoxRecords`/`readToManyTargets` now
      read the original files directly, in place; `readObjectBoxRecordsUnsafe`/
      `readToManyTargetsUnsafe` are gone (no distinction left to make).
    - **Verified empirically, not just built:** Linux — real
      `flutter build linux --release`, isolated-bundle run, real ObjectBox
      data (18 records, matches every prior verification). Android — real
      debug/release APK on a real emulator (Pixel 9, Android 15), all
      three ABIs present, on-device write+read round trip confirmed via
      logcat. Windows/macOS/iOS — written to match the official
      `plugin_ffi` scaffold exactly (diffed against a freshly generated
      copy), **not** built on real hardware — no Windows or macOS/Xcode
      toolchain in this environment; flagged as such in both packages'
      READMEs/CHANGELOGs rather than assumed working.
    - **LMDB version pinning, three independent copies, no git
      submodule:** the C++ core's `FetchContent`, `dart/src/lmdb/`, and
      `flutter/src/lmdb/` all need the same LMDB release. A submodule was
      considered and rejected: it'd still be three separate
      submodule entries (pub.dev packages can't share content outside
      their own directory), gains nothing over a version-pin file for
      keeping them in sync, and adds a real failure mode of its own — an
      uninitialized submodule silently publishes an *empty* directory to
      pub.dev. Instead: `LMDB_VERSION` at the repo root pins one tag as a
      single bare line (must be a release tag like `LMDB_0.9.31` — upstream
      uses OpenLDAP release-engineering branch names like `mdb.master3`/
      `mdb.RE/0.9` for moving development lines, confirmed via
      `git ls-remote`, never a fixed point); the C++ core's
      `CMakeLists.txt` reads it via `file(STRINGS ... LIMIT_COUNT 1)`
      (plain `file(STRINGS)` without `LIMIT_COUNT` extracts printable-ASCII
      *runs*, not lines — a stray em dash in an earlier, comment-laden
      draft of this file fragmented it mid-line and leaked a text fragment
      through as "the version", caught by actually running the configure
      step, not assumed). `scripts/ci/verify-lmdb-vendor.sh` checks all
      three stay in sync: a fast default mode reads
      `MDB_VERSION_{MAJOR,MINOR,PATCH}` straight out of each vendored
      `lmdb.h` (LMDB's own header embeds its release version — no separate
      per-copy version file needed) and compares against `LMDB_VERSION`;
      `--full` additionally clones the pinned tag fresh and diffs it
      byte-for-byte against both vendored copies, to catch a hand-edited
      vendored file that macro comparison alone can't see. Wired into a
      new `.github/workflows/lmdb-vendor.yml`, `push`/`pull_request`, using
      `--full` since CI always has network. Both modes run and pass
      locally against the real pinned tag.
    - **Offline/Flatpak-safe by construction, not by exception:** grepped
      every native build file this item touches
      (`flutter/{linux,windows,src}/CMakeLists.txt`,
      `flutter/android/build.gradle`, `flutter/{ios,macos}/*.podspec`,
      `dart/src/lmdb/CMakeLists.txt`, `dart/lib/src/build_util.dart`) for
      `FetchContent`/`curl`/`wget`/`git clone`/`git_repository`/bare
      `http(s)://` — zero matches. The vendored LMDB source travels with
      each package's own pub.dev tarball, the same as any other file in
      it; no `flutpak` `foreign_deps.json` entry is needed for
      `ob_dump_reader`/`ob_dump_reader_flutter` at all, unlike the C++
      core's own LMDB `FetchContent` (still a live network fetch at CMake
      configure time — out of scope for this item, tracked separately if
      it ever needs the same treatment).
    - CI (`scripts/ci/test-dart.sh`) updated to match: the stale
      `dart run dart_lmdb2:fetch_native` step (see item 21 — CI already
      called this out as a script this project depends on) is replaced
      with `dart run ob_dump_reader:build`, this package's own native
      build step. `.github/dependabot.yml`'s comments and one stale
      `dart_lmdb2` precedent-citation in the "LMDB" Design-decisions row
      (see above) updated to match; no functional Dependabot config change
      since neither package had a real pub.dev LMDB dependency to track in
      the first place once this landed.
23. **`py/` package (`ob-dump-reader`) — introduced, then brought up to the
    same bar as `dart/`/`flutter/`.** Same pattern as item 10 (Dart): a
    standalone PyPI package doing LMDB traversal + ObjectBox key parsing via
    [`py-lmdb`](https://github.com/jnwatson/py-lmdb) (a mature, actively
    maintained binding — no reason to vendor LMDB the way `dart/` does, see
    item 22), with decoding left to official `flatc --python` output. Also
    ships an `ob_dump_reader.aio` module mirroring the same API as
    `async`/`await` coroutines, built on py-lmdb's own executor-based async
    wrapper.

    This package existed before it had ever been exercised end-to-end —
    a full pass (triggered by "put the Python package in order") found and
    fixed several real, previously-undiscovered bugs, each confirmed by
    actually running the code, not by reading it:
    - **`decode_flex()` crashed on every real call.** Assumed
      `flatbuffers.flexbuffers.Ref` had a `.json` property; it doesn't
      (`hasattr` was always `False`), so it fell back to `str(root)` — a
      debug repr like `"Ref(buf[21:], parent_width=1, ...)"`, not JSON —
      and `json.loads()` on that raised immediately. Fixed with a proper
      recursive `Ref` → native-Python-object converter (also had to
      discover, empirically, that `Is*`/`As*` are properties on this API,
      not methods — `ref.IsMap()` raises `TypeError: 'bool' object is not
      callable`), matching the same map/vector/scalar/blob handling as the
      C++ core's `flexToJson` and the Dart package's `decodeFlex`.
    - **`ob_dump_reader.aio` couldn't be imported at all** —
      `from ob_dump_reader.decode_helpers import K_KEY_TYPE_DATA, ...`
      referenced a name that had never existed in that module. Root cause:
      the constant was defined once in `__init__.py` as `KEY_TYPE_DATA`
      (no `K_` prefix) and never actually shared — fixed by extracting
      `KEY_TYPE_DATA`/`KEY_TYPE_RELATION`/relation-direction constants into
      one `_constants.py` both modules import, so this can't drift again.
      Also fixed a real typo along the way: `RELATION_DIRECTION_FFORWARD`
      → `RELATION_DIRECTION_FORWARD`.
    - **`ob_dump_reader.aio` misused `lmdb.aio`'s actual API**, even past
      the import fix: `AsyncEnvironment(path, map_size=..., ...)` isn't how
      one is constructed — reading `lmdb.aio`'s own source showed it wraps
      an already-open `lmdb.Environment` via `lmdb.aio.wrap(env)` instead.
      Separately, `AsyncCursor` has no `__aiter__`, so the original
      `async for key, value in cursor` was never callable; its `iternext()`
      alternative also fully materializes every remaining entry into a
      list in one executor call, which would mean loading the *entire rest
      of the database* into memory before the first callback fires —
      replaced with an explicit `first()`/`next()`/`item()` step loop
      instead (verified with a real fixture that this continues from
      `set_range()`'s position rather than resetting, same as the sync
      `lmdb.Cursor`).
    - **`read_objectbox_to_many_targets()` was a no-op stub**, sync and
      async both — opened an environment and did nothing else, silently
      returning `None` despite being part of the public API (`__all__`
      included it). Implemented for real against the same 12-byte
      relation-key format documented in item "ToMany relations" above
      (`Cursor.set_range()` to the relation/source-id prefix, walking
      forward while the prefix still matches, forward direction only).
    - `__main__.py` hardcoded a developer's own local database path
      (`/home/murphy/.local/share/...`) — replaced with a real
      `argparse`-based CLI, plus a `[project.scripts]` entry point
      (`ob-dump-reader`).
    - Dead, commented-out "write transaction to initialize the root
      handle" code removed — the same `dart_lmdb2`-era misconception item
      22 already resolved for Dart, independently present here too:
      `readonly=True` alone is sufficient, LMDB's MVCC design is exactly
      why.
    - Added `tests/` (15 tests, `pytest`) — the package had none at all
      despite `pytest` already being a declared dev dependency and
      `testpaths = ["tests"]` already configured; every fix above is now
      covered by a fixture-based test that would have caught it.
    - Removed unused `protovalidate`/`pydantic` dev dependencies (zero
      references anywhere in `src/`) — pulled in 16 transitive packages
      for nothing; `uv lock` after removal dropped from 650 to under 100
      lines.
    - `.github/workflows/pytest.yml` filled in (previously an empty stub)
      and `scripts/ci/test-python.sh` added, matching the
      `test-dart.sh`/`test-flutter.sh` pattern from item 21. Separately,
      `release.yml`'s own `test-python` job was found using
      `subosito/flutter-action@v2` (copy-pasted from `test-flutter` without
      swapping the toolchain-setup step) instead of `astral-sh/setup-uv`;
      fixed, and wired into the same gate every other build/test leg feeds:
      a new `tests-passed` job (`needs: [build-cpp, test-dart, test-flutter,
      test-python]`) that `bump-versions` alone depends on, rather than
      every downstream job repeating the full leg list — added on request,
      once adding `test-python` directly to `bump-versions`' own `needs:`
      made that repetition obvious. `publish-pypi`'s checkout was missing
      `fetch-depth: 0` — `setuptools_scm` derives the package version from
      the git tag via `git describe`, which a shallow checkout can leave
      unreachable, silently falling back to `pyproject.toml`'s
      `fallback_version` (`0.0.0.dev0`) instead of the real release
      version.
    - `.github/dependabot.yml`'s newly-added `py/` entry used
      `package-ecosystem: "pip"`, which doesn't understand `uv.lock` (only
      `requirements.txt`/`Pipfile`/plain `pyproject.toml`) — confirmed via
      GitHub's own Dependabot changelog that `"uv"` got native ecosystem
      support (understanding `pyproject.toml` + `uv.lock` directly) in
      March 2025; switched to that.
24. **One root `CHANGELOG.md`, generated per-package copies — done.**
    `create-release`'s "Extract release notes" step only ever read
    `dart/CHANGELOG.md` — fine while `dart/` was the only package with real
    content, but a real gap once `flutter/CHANGELOG.md` grew its own
    distinct entries and `py/CHANGELOG.md` was added (item 23): neither
    ever fed into the combined GitHub Release notes, and the C++ core had
    no changelog anywhere at all. Considered adding a root file *alongside*
    the three per-package ones first, but pub.dev requires dart/ and
    flutter/ to each ship their own physical `CHANGELOG.md` to publish at
    all — a hand-maintained root file plus three hand-maintained package
    files is exactly the "which copy is the real one" drift this is meant
    to fix, not a fix.

    Landed on: the repo root's `CHANGELOG.md` is the *only* hand-edited
    copy — version headings, each containing per-package `### dart/`/
    `### flutter/`/`### py/` subsections (only the packages that actually
    changed that release). `dart/CHANGELOG.md`/`flutter/CHANGELOG.md`/
    `py/CHANGELOG.md` are **not committed at all** (removed from git) —
    `scripts/ci/sync_changelogs.py` generates them fresh, ephemerally, as a
    `release.yml` step immediately before each package's own publish step
    (`publish-dart`/`publish-flutter`/ not `publish-pypi`, since PyPI has no
    equivalent hard requirement) — same "never written back to the repo"
    pattern `publish-flutter` already uses for its own ephemeral
    `pubspec.yaml` ref-swap. Deliberately **not** added to `dart/.gitignore`/
    `flutter/.gitignore` despite being generated: `dart pub publish`/
    `flutter pub publish` decide package contents by git tracking status,
    and explicitly exclude gitignored files even if force-staged
    (`git add -f` still triggers a "checked in while gitignored" warning
    and gets dropped from the archive — confirmed empirically after a real
    `publish-dart` run failed exactly this way, `Package validation found
    ... Please add a CHANGELOG.md`, despite the generation step itself
    having succeeded) — a plain untracked-but-not-ignored file is what
    actually gets included, no commit needed either. `create-release`'s
    extraction step now reads the root file directly instead of looping
    over three.

    **Two more real, previously-undiscovered bugs surfaced while doing
    this** (both confirmed, not assumed):
    - Every "`0.1.0-alpha.1`" reference across `dart/CHANGELOG.md`,
      `flutter/CHANGELOG.md`, and this doc's own item 18 was wrong — the
      version actually published by hand to pub.dev (to reserve the
      package names, see item 18) is `0.1.0-alpha.0`, confirmed via
      pub.dev's own API (`curl .../api/packages/ob_dump_reader` →
      `"latest": {"version": "0.1.0-alpha.0"}`, same for
      `ob_dump_reader_flutter`) — there is no `0.1.0-alpha.1` and never
      was. Fixed everywhere, including this document. Since that publish
      was manual (never through a git tag), it also has no GitHub tag to
      link to — `sync_changelogs.py` special-cases it to link to its
      pub.dev version page instead of a 404ing `releases/tag/` URL.
    - `publish-pypi` had **no `working-directory: py` at all** (unlike
      `publish-dart`/`publish-flutter`, which both set one) — every
      `uv sync`/`uv build`/`uv publish` step would have run from the repo
      root, which has no `pyproject.toml`, and failed immediately. Never
      caught because this job has never actually run (no tag has been
      pushed since it was added). Confirmed by running `uv sync --dev`
      from the repo root locally: `error: No pyproject.toml found in
      current directory or any parent directory` — uv only searches
      upward, never into subdirectories. Fixed by adding the same
      `defaults: run: working-directory: py` the other two publish jobs
      already had.
25. **`flutter/pubspec.yaml` switched from a plain `path:` dep to
    `dependency_overrides` — done, avoids CI patching it.** Previously,
    `flutter/pubspec.yaml`'s committed `dependencies.ob_dump_reader` was a
    bare `path: ../dart`, plus a `publish_to: none` needed because a plain
    path dependency there isn't publishable (`flutter analyze` fails,
    exit 1, on `invalid_dependency`). `publish-flutter` worked around both
    ephemerally, right before `pub publish`, in the checkout only: `flutter
    pub remove ob_dump_reader && flutter pub add "ob_dump_reader@^$VERSION"`
    then a `sed` to drop `publish_to: none` — never committed back, so
    every other CI job (`test-flutter.sh`, `flutter analyze` on PRs) still
    saw the raw path dep, and there was a whole ephemeral-rewrite step to
    trust at release time.

    Restructured instead: `dependencies.ob_dump_reader` is now a real,
    checked-in `^X.Y.Z` constraint (kept in lockstep with
    `dart/pubspec.yaml`'s `version:`, same as `flutter/pubspec.yaml`'s own
    `version:` already was — see item 18), and the local path dep moved to
    a checked-in `dependency_overrides.ob_dump_reader: {path: ../dart}`.
    `bump-versions` now bumps that dependency line alongside both
    `version:` fields (one more `sed`, same lockstep pattern), and
    `version-parity.yml` gained a second check enforcing it never drifts.
    `publish-flutter` no longer touches `pubspec.yaml` at all — no `pub
    remove`/`pub add`, no `publish_to: none` (removed from the file
    entirely, nothing left to strip).

    This relies on two things read directly from source rather than run
    locally (no Flutter/Dart SDK available in the environment this was
    done in, so **not empirically confirmed the way item 19's real CI runs
    were** — flagging that explicitly rather than claiming otherwise):
    - The analyzer's `invalid_dependency` check
      (`pkg/analyzer/lib/src/pubspec/validators/dependency_validator.dart`
      in `dart-lang/sdk`) only walks `dependencies`/`dev_dependencies`;
      `dependency_overrides` is never passed to `validatePathEntries`. So
      moving the path dep there, with a real constraint under
      `dependencies`, should make `isPublishablePackage` stay true without
      tripping the path-dependency error — i.e. `publish_to: none` is no
      longer needed for `flutter analyze` to pass.
    - `dart-lang/pub`'s own `DependencyOverrideValidator`
      (`lib/src/validator/dependency_override.dart`) only ever adds a
      *hint* when a non-dev dependency is overridden — not a warning or
      error — so it doesn't block `pub publish` or need `--force` to get
      past, and `dependency_overrides` is never transitive (only applies
      when the declaring package is the resolution root), so it has zero
      effect on anyone installing the published `ob_dump_reader_flutter`.

    If a real `flutter analyze`/`flutter pub publish --dry-run` run ever
    contradicts either point above, that's the first thing to check.
26. **`release.yml` has no `publish-js` job yet — intentional, not an
    oversight.** `js/` (`ob-dump-reader` on npm) was added to the repo
    with no corresponding publish job. npm's trusted-publisher OIDC flow
    needs the package to already exist on the registry with a trusted
    publisher configured in its settings — unlike PyPI's "pending
    publisher" feature, there's no way to pre-authorize a name that's
    never been published — so CI can't be the one to do the very first
    `npm publish` for `ob-dump-reader`. That has to happen by hand, once,
    from a maintainer's own npm login, before a `publish-js` job has
    anything to attach trusted publishing to.

    Once that manual first publish has happened and npmjs.com's package
    settings has a trusted publisher pointing at this repo's `release.yml`,
    add a job matching the one `o-murphy/a7p`'s own `release.yml` already
    uses for the same situation (its `a7p-js` npm package):
    ```yaml
    publish-js:
      needs: [bump-versions, create-release]
      runs-on: ubuntu-latest
      environment: npmjs
      permissions:
        id-token: write # npm OIDC trusted publishing (provenance)
      defaults:
        run:
          working-directory: js
      steps:
        - uses: actions/checkout@v7
          with:
            ref: main
        - uses: actions/setup-node@v7
          with:
            node-version: "24"
            registry-url: https://registry.npmjs.org/
        - run: yarn install --frozen-lockfile
        - run: yarn build
        - name: Use OIDC auth
          run: npm config delete //registry.npmjs.org/:_authToken
        - name: Publish to npm
          run: |
            VERSION="${{ needs.bump-versions.outputs.version }}"
            if [ "${{ needs.bump-versions.outputs.prerelease }}" = "true" ]; then
              DIST_TAG=$(echo "$VERSION" | grep -oE '(alpha|beta|rc|pre)' | head -1)
              npm publish --provenance --tag "${DIST_TAG:-prerelease}"
            else
              npm publish --provenance
            fi
    ```
    The `npm config delete //registry.npmjs.org/:_authToken` step matters:
    `actions/setup-node`'s `registry-url` input writes a placeholder
    `NODE_AUTH_TOKEN`-based auth entry to `.npmrc` that `npm publish` would
    otherwise try first; deleting it is what makes `npm publish` fall back
    to the OIDC trusted-publisher exchange instead (per `a7p`'s own working
    setup, not guessed). Same `environment:` gate pattern as
    `pub-publish-dart`/`pub-publish-flutter`/`pypi` above — configure
    protection rules for it in this repo's Settings > Environments once
    the environment exists.

## Integrity & Licensing

`ob-dump` was developed as an independent implementation for reading data stored in the ObjectBox format. It adheres to a "Clean Room Design" approach regarding binary software:
- **Purpose-Limited:** Built solely to support data recovery and migration to another database, for projects whose own license is incompatible with `objectbox-c`'s (a closed-source binary — see "Why this exists" above). Not intended as, and not pursued as, a competing product to ObjectBox itself — no write support, no query engine, no ongoing-database use case, strictly a one-time read-only export path out of an existing store.
- **No Reverse Engineering:** We have performed no decompilation, disassembly, or any other analysis of the closed-source `objectbox-c` binary.
- **Open Specification:** Data parsing is based exclusively on public formats (LMDB and FlatBuffers) and the open-source code of the official schema generator ([`objectbox_generator`](https://github.com/objectbox/objectbox-dart/tree/main/generator), licensed under Apache 2.0).
- **Model-Driven:** The decoding process is driven by the user-provided `objectbox-model.json` file, which is an open, user-accessible schema definition.

This approach ensures full licensing integrity: `ob-dump` is an independent software project that contains no proprietary or misappropriated code, making it suitable for integration into projects with any licensing requirements.

## License

MIT — see [`LICENSE`](LICENSE).
