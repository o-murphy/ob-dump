# ob-dump

![GitHub License](https://img.shields.io/github/license/o-murphy/ob-dump)
![GitHub Release](https://img.shields.io/github/v/release/o-murphy/ob-dump)
![Dart Pub Version](https://img.shields.io/pub/v/ob_dump_reader?logo=dart)
![Flutter Pub Version](https://img.shields.io/pub/v/ob_dump_reader_flutter?logo=flutter)
![PyPI Version](https://img.shields.io/pypi/v/ob_dump_reader?logo=pypi)


Reads an [ObjectBox](https://objectbox.io/) LMDB store (`data.mdb`) directly
and dumps its contents to JSON — **without linking `objectbox-c`**. Decoding
is driven entirely, at runtime, by the project's own `objectbox-model.json`
schema file: no `.fbs` schema and no `flatc` codegen involved.

Public interface is a plain C ABI (`ob_dump()`/`ob_dump_free()`), meant to be
embedded via CFFI bindings from any language (Dart, Python, Go, JVM, ...).

See [`docs/BACKLOG.md`](docs/BACKLOG.md) for the full design rationale
(why not `objectbox-c`, why not `.fbs`/`flatc`, why this dependency set),
current scope, and the verification results against a real ObjectBox
database.

## Table of contents

- [Building](#building)
- [CLI](#cli)
- [C API](#c-api)
- [Dart](#dart)
- [Python](#python)
- [Building your own reader in another language](#building-your-own-reader-in-another-language)
- [Scope](#scope)
- [Integrity & Licensing](#integrity--licensing)
- [License](#license)

## Building

```sh
cmake -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Dependencies (`lmdb`, `flatbuffers`, `nlohmann/json`) are fetched from source
via CMake `FetchContent` on first configure — no prebuilt binaries, no
network access needed afterwards. See `docs/BACKLOG.md` for why (Flathub/GPL
source-availability requirements).

## CLI

Three modes, each behind an explicit flag:

```sh
build/ob_dump --json   <path/to/data.mdb> <path/to/objectbox-model.json> [-o dump.json]
build/ob_dump --schema <path/to/objectbox-model.json> [-o schema.json]
build/ob_dump --fbs    <path/to/objectbox-model.json> [-o schema.fbs]
```

`--json` dumps the actual data, shaped as:

```json
{
  "EntityName": [
    { "id": 1, "someField": "value", "...": "..." }
  ]
}
```

Entities with zero stored objects are omitted entirely. `--json` streams its
output (memory use is O(1) per record, not O(total database size)) — see
`docs/BACKLOG.md` phased-plan item 13 for why that's possible without any
buffering/lookahead.

The other two modes export the *schema*, not the data:

`--schema` prints a clean, minimal JSON listing of every entity/property
(id, name, type, computed vtable slot) and any `ToMany` relations it
declares (id, name, target entity) — stripped of ObjectBox's model.json
noise. `--fbs` generates a valid FlatBuffers IDL file, so any language's
`flatc` can generate its own typed reader for the raw table bytes instead of
depending on this library — verified against a real database (see
`docs/BACKLOG.md`), but note it only replaces the per-record FlatBuffers
decode step, not LMDB access, the ObjectBox key-format parsing, or `ToMany`
relations (not representable in a FlatBuffers table at all — a `flatc`
consumer needs its own relation-key lookup, same as this library's own;
see `docs/BACKLOG.md` "Schema export" for exactly what that does and
doesn't cover).

## C API

```c
#include <ob_dump.h>

ObDumpSource source = { .kind = OB_DUMP_SOURCE_PATH, .as.path = "/path/to/data.mdb" };
char* json = ob_dump(&source, model_json_contents);
if (json == NULL) {
    fprintf(stderr, "error: %s\n", ob_dump_last_error());
} else {
    // use json ...
    ob_dump_free(json); // never plain free()/delete — see ob_dump.h
}

// Schema-only exports — same NULL/ob_dump_last_error()/ob_dump_free() contract:
char* schema = ob_dump_schema(model_json_contents);
char* fbs    = ob_dump_fbs(model_json_contents);
```

For large databases, `ob_dump()` isn't ideal — it builds the entire result
in memory before returning it. `ob_dump_stream()` invokes a callback once
per decoded record instead:

```c
int on_record(const char* entity_name, int64_t object_id,
             const char* fields_json, void* user_data) {
    printf("%s#%lld: %s\n", entity_name, (long long)object_id, fields_json);
    return 0; // non-zero stops iteration early (not an error)
}

int rc = ob_dump_stream(&source, model_json_contents, on_record, NULL);
if (rc != 0) fprintf(stderr, "error: %s\n", ob_dump_last_error());
```

## Dart

[`dart/`](dart) is a separate, standalone pub.dev package
([`ob_dump_reader`](dart/README.md)) for reading an ObjectBox database
directly from Dart — deliberately *not* an FFI binding to this C++ core. See
its own README and `docs/BACKLOG.md` (phased-plan item 10) for why.

**Using it from a Flutter app** (not a plain Dart script/CLI)? Depend on
[`flutter/`](flutter) (`ob_dump_reader_flutter`) instead — same API,
re-exported unchanged, but it's a real Flutter plugin (`ffiPlugin: true`)
that compiles and bundles the native LMDB library for you on every platform
(Android/iOS/Linux/macOS/Windows). `ob_dump_reader`'s own
`dart run ob_dump_reader:build` only produces a library for the current
desktop machine, which isn't part of a shipped mobile app. See
`flutter/README.md` and `docs/BACKLOG.md` (phased-plan items 17 and 22).

## Python

[`py/`](py) is a separate, standalone PyPI package
([`ob-dump-reader`](py/README.md)) for reading an ObjectBox database
directly from Python, via [`py-lmdb`](https://github.com/jnwatson/py-lmdb)
— same pattern as `dart/`: no FFI binding to this C++ core, just an LMDB
binding for the language plus this project's own key-format/`ToMany`
handling. Also ships an `async`/`await` variant (`ob_dump_reader.aio`,
built on py-lmdb's own executor-based async wrapper). See its own README
for the full workflow.

## Building your own reader in another language

[`dart/`](dart) and [`py/`](py) (see above) are worked examples of a
pattern that doesn't need any FFI binding to this C++ core at all, as long
as *some* LMDB binding exists for your language (common — LMDB is a
popular embedded store):

1. **Get an LMDB binding for your language.** `dart/` (see above) vendors
   and builds LMDB itself rather than depending on a third-party binding
   package — see `docs/BACKLOG.md` item 22 for why (a real AOT-compiled-build
   bug found in the binding package it used to depend on). `py/` uses
   [`py-lmdb`](https://github.com/jnwatson/py-lmdb) directly — mature and
   actively maintained, so no equivalent reason to vendor there. For other
   languages: the `lmdb` crate for Rust, `lmdbjava` for the JVM.
2. **Generate the schema artifacts from this project:**
   ```sh
   ob_dump --schema objectbox-model.json -o schema.json  # entityId -> name/shape
   ob_dump --fbs    objectbox-model.json -o schema.fbs   # FlatBuffers IDL
   ```
3. **Generate a typed decoder with the official FlatBuffers compiler:**
   `flatc --<your-language> schema.fbs` (not `flatcc` — that's a separate,
   C-only implementation with no backend for most languages). This gives
   you correct, officially-generated code for the one genuinely fiddly part
   (FlatBuffers vtable/type decoding) — see `docs/BACKLOG.md` "Schema
   export" for exactly what this does and doesn't replace.
4. **Implement the LMDB walk + ObjectBox key parsing** — the only part
   that's actually yours to write, and it's small: open `data.mdb`
   read-only, cursor-walk the root/unnamed database (ObjectBox never uses
   named sub-databases), and for each entry whose 8-byte key's first byte
   is `0x18` (object data — `0x00` is a schema entry, `0x20` an index
   entry), the value is one record's raw FlatBuffers table bytes, and:
   - `entity_id = key[3] / 4`
   - `object_id = key[4..7]` as a big-endian uint32
   `dart/lib/ob_dump_reader.dart` and `py/src/ob_dump_reader/__init__.py`
   are both complete, working reference implementations of exactly this
   (each well under 100 lines) — port whichever's closer to your target
   language rather than starting from scratch.
5. Dispatch on `entity_id` (via `schema.json`) to pick the right
   `flatc`-generated type, decode, and do whatever you need with the result
   (insert into a new database, etc).

## Scope

Reads all 21 ObjectBox `PropertyType` codes: every fixed-width scalar
(`Bool`, `Byte`, `Short`, `Char`, `Int`, `Long`, `Float`, `Double`, `Date`,
`ToOne` relations, `DateNano`), `String`, every vector form of the above —
with correct signedness (the `UNSIGNED` property flag is honored, not just
the type code) — and `Flex` (a dynamic, embedded FlexBuffers value, decoded
recursively into the equivalent JSON shape). `ToMany` relations are also
resolved: they live in a separate LMDB relation-index structure, not a
table field, so `--json` adds them as a plain array of target ids under
the relation's name after decoding the record itself (not a nested/eager
object fetch — the same "just the fk" shape already used for `ToOne`).
The `ExternalPropertyType` annotation layer (`Uuid`, `Int128`,
etc.) is partially handled: `Uuid`/`Int128`/`Decimal128`/`Bson` decode as
hex/UUID strings instead of raw byte arrays, and `Json`/`JavaScript`/
`JsonToNative` decode as parsed JSON instead of an escaped JSON string.
See `docs/BACKLOG.md` for the full list and the reasoning behind each
design choice.

## Integrity & Licensing

`ob-dump` was developed as an independent implementation for reading data stored in the ObjectBox format. It adheres to a "Clean Room Design" approach regarding binary software:
- **Purpose-Limited:** Built solely to support data recovery and migration to another database, for projects whose own license is incompatible with `objectbox-c`'s (a closed-source binary — see "Why this exists" in `docs/BACKLOG.md`). Not intended as, and not pursued as, a competing product to ObjectBox itself — no write support, no query engine, no ongoing-database use case, strictly a one-time read-only export path out of an existing store.
- **No Reverse Engineering:** We have performed no decompilation, disassembly, or any other analysis of the closed-source `objectbox-c` binary.
- **Open Specification:** Data parsing is based exclusively on public formats (LMDB and FlatBuffers) and the open-source code of the official schema generator ([`objectbox_generator`](https://github.com/objectbox/objectbox-dart/tree/main/generator), licensed under Apache 2.0).
- **Model-Driven:** The decoding process is driven by the user-provided `objectbox-model.json` file, which is an open, user-accessible schema definition.

This approach ensures full licensing integrity: `ob-dump` is an independent software project that contains no proprietary or misappropriated code, making it suitable for integration into projects with any licensing requirements.

## License

MIT (see [`LICENSE`](LICENSE)) — chosen for maximum portability: permissive
and combinable into any project, open or closed source, without copyleft
obligations. This is also compatible with every dependency `ob-dump` pulls in:

| Dependency | License |
|---|---|
| [`LMDB`](https://github.com/LMDB/lmdb) | OpenLDAP Public License (permissive, BSD-style) |
| [`flatbuffers`](https://github.com/google/flatbuffers) | Apache License 2.0 |
| [`nlohmann/json`](https://github.com/nlohmann/json) | MIT |

None of these are copyleft, so `ob-dump` (and anything embedding it) is free
to be licensed however its own project requires.
