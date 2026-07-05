# ob-dump

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

Entities with zero stored objects are omitted entirely.

The other two modes export the *schema*, not the data:

`--schema` prints a clean, minimal JSON listing of every entity/property
(id, name, type, computed vtable slot) — stripped of ObjectBox's model.json
noise. `--fbs` generates a valid FlatBuffers IDL file, so any language's
`flatc` can generate its own typed reader for the raw table bytes instead of
depending on this library — verified against a real database (see
`docs/BACKLOG.md`), but note it only replaces the per-record FlatBuffers
decode step, not LMDB access or the ObjectBox key-format parsing (see
`docs/BACKLOG.md` "Schema export" for exactly what that does and doesn't
cover).

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

## Scope

Reads all 20 non-`Flex` ObjectBox `PropertyType` codes: every fixed-width
scalar (`Bool`, `Byte`, `Short`, `Char`, `Int`, `Long`, `Float`, `Double`,
`Date`, `ToOne` relations, `DateNano`), `String`, and every vector form of
the above. `ToMany` relations and `Flex` properties are not implemented —
they're structurally different (a separate LMDB structure and a nested
FlexBuffers blob, respectively), not just another field type. See
`docs/BACKLOG.md` for the full list and the reasoning behind each design
choice.

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
