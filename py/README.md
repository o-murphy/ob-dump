# ob-dump-reader

![GitHub License](https://img.shields.io/github/license/o-murphy/ob-dump)
![PyPI Version](https://img.shields.io/pypi/v/ob_dump_reader?logo=pypi)

Minimal ObjectBox LMDB reader toolkit for Python. Its core job: walk a
`data.mdb` file and hand you each stored object's raw FlatBuffers table
bytes, plus its entity id and object id — no per-field FlatBuffers or
schema knowledge for that part, just [`lmdb`](https://pypi.org/project/lmdb/)
(the [py-lmdb](https://github.com/jnwatson/py-lmdb) bindings). It also
covers the handful of things `flatc --python` output can't: `ToMany`
relations (a separate LMDB structure, not a table field) and `Flex`/
`ExternalPropertyType` fields (`flatc` only knows the base FlatBuffers
type, not ObjectBox's semantic annotation on top) — see "Beyond `flatc`"
below.

Decoding those raw bytes into typed objects is left to the official
`flatc --python` compiler — not this package, and not [`ob-dump`](..)'s own
C++ core either. This is a deliberate toolkit split, not a missing feature:
see the parent project's [`docs/BACKLOG.md`](../docs/BACKLOG.md#schema-export---schema-and---fbs)
for the reasoning.

## Workflow

```sh
# 1. Generate a schema JSON (entityId -> table name/shape) and a .fbs from
#    your ObjectBox model, using the ob_dump CLI (see ../README.md):
ob_dump --schema objectbox-model.json -o schema.json
ob_dump --fbs    objectbox-model.json -o schema.fbs

# 2. Generate typed Python classes with the *official* FlatBuffers compiler.
flatc --python -o models/ schema.fbs
```

```python
import ob_dump_reader as ob
from models.Ammo import Ammo  # from flatc --python, see step 2 above

AMMO_ENTITY_ID = 1  # from schema.json


def on_record(record: ob.ObRecord) -> None:
    if record.entity_id == AMMO_ENTITY_ID:
        ammo = Ammo.GetRootAs(record.data)  # flatc-generated class decodes the bytes
        print(f"{ammo.Name()}: {ammo.BcG1()}")
        # ... insert into whatever your new database is.


ob.read_objectbox_records("/path/to/objectbox/dir", on_record)
```

## Async

`ob_dump_reader.aio` mirrors the same API as `async`/`await` coroutines
(built on [py-lmdb's own `lmdb.aio`](https://py-lmdb.readthedocs.io/en/latest/)
executor-based wrapper — LMDB itself has no native async I/O, so this
dispatches the same blocking calls to a thread executor rather than
avoiding them):

```python
import ob_dump_reader.aio as ob


async def on_record(record: ob.ObRecord) -> None:
    ...


await ob.read_objectbox_records("/path/to/objectbox/dir", on_record)
```

## Reads are in place, no copy

`read_objectbox_records`/`read_objectbox_to_many_targets` read
`data.mdb`/`lock.mdb` directly, with no temporary copy step — they open
the LMDB environment itself `readonly=True` (matching `ob-dump`'s own C++
`LmdbReader`), so they never need a write-capable handle. This is exactly
what LMDB's MVCC design is for: any number of readers can safely run
alongside one concurrent writer, in any process, with zero risk to the
original data — even while a live ObjectBox process has the same store
open.

## Beyond `flatc`: ToMany relations and Flex/ExternalPropertyType fields

`ob_dump --schema` lists each property's `externalType` and each entity's
`relations` (id, name, target entity) when present — use that to know
which of your fields need one of these:

```python
import ob_dump_reader as ob
from ob_dump_reader.decode_helpers import (
    decode_flex,
    bytes_to_hex,
    bytes_to_uuid_string,
    try_parse_json_string,
)

# ToMany: not part of the FlatBuffers table at all, so flatc has no
# accessor for it — relation_id/source_object_id come from `ob_dump --schema`
# and the record you're looking at (record.object_id).
author_ids = ob.read_objectbox_to_many_targets(db_dir, relation_id, record.object_id)

# Flex: flatc gives you the raw bytes (a bytes/bytearray field) —
# decode with decode_flex.
value = decode_flex(ammo.SomeFlexField())

# ExternalPropertyType: flatc gives you the base type's plain value
# (a byte blob for Uuid/Int128/Decimal128/Bson, a string for
# Json/JavaScript/JsonToNative) — decode with the matching helper.
uuid = bytes_to_uuid_string(ammo.SomeUuidField())
blob = bytes_to_hex(ammo.SomeBsonField())
parsed = try_parse_json_string(ammo.SomeJsonField())
```

Every one of these mirrors `ob-dump`'s own C++ decode (`src/fb_decode.cpp`)
exactly — same hex/UUID formatting, same JSON-parse-with-string-fallback
for `JavaScript`, same forward-only relation direction (see
`docs/BACKLOG.md` "ToMany relations" for why: the backward direction is an
auto-maintained index for ObjectBox's own query engine, not needed for a
one-directional dump).

## Why this shape

The alternative — an FFI wrapper around `ob-dump`'s C++ core — would mean
building/vendoring native C++ from a PyPI package for every platform. Not
needed here: [`py-lmdb`](https://github.com/jnwatson/py-lmdb) already gives
a mature, actively-maintained Python binding to LMDB, and `flatc --python`
already gives an officially generated, correct decoder. This package is
only the small piece connecting the two — LMDB traversal and the
ObjectBox key format (`docs/BACKLOG.md` in the parent project) — and stays
that small on purpose.

## Integrity & Licensing

`ob-dump` was developed as an independent implementation for reading data stored in the ObjectBox format. It adheres to a "Clean Room Design" approach regarding binary software:
- **Purpose-Limited:** Built solely to support data recovery and migration to another database, for projects whose own license is incompatible with `objectbox-c`'s (a closed-source binary — see "Why this exists" in the parent project's [`docs/BACKLOG.md`](../docs/BACKLOG.md)). Not intended as, and not pursued as, a competing product to ObjectBox itself — no write support, no query engine, no ongoing-database use case, strictly a one-time read-only export path out of an existing store.
- **No Reverse Engineering:** We have performed no decompilation, disassembly, or any other analysis of the closed-source `objectbox-c` binary.
- **Open Specification:** Data parsing is based exclusively on public formats (LMDB and FlatBuffers) and the open-source code of the official schema generator ([`objectbox_generator`](https://github.com/objectbox/objectbox-dart/tree/main/generator), licensed under Apache 2.0).
- **Model-Driven:** The decoding process is driven by the user-provided `objectbox-model.json` file, which is an open, user-accessible schema definition.

This approach ensures full licensing integrity: `ob-dump` is an independent software project that contains no proprietary or misappropriated code, making it suitable for integration into projects with any licensing requirements.

## License

MIT — see [`LICENSE`](LICENSE).
