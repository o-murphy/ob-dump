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

```sh
build/ob_dump <path/to/data.mdb> <path/to/objectbox-model.json> [-o dump.json]
```

Prints JSON to stdout by default (`-o` writes to a file instead), shaped as:

```json
{
  "EntityName": [
    { "id": 1, "someField": "value", "...": "..." }
  ]
}
```

Entities with zero stored objects are omitted entirely.

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
```

## Scope

Reads `bool`, `int64`/`Date`, `double`, `string`, `ToOne` relations,
`Float64Vector`, and `StringVector` properties — everything actually observed
in the schema this was built against. `ToMany` relations, `Flex` properties,
and the remaining `PropertyType` codes are not implemented yet. See
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
