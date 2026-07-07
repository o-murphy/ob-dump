# ob-dump-reader (JS/TS)

![GitHub License](https://img.shields.io/github/license/o-murphy/ob-dump)

Minimal ObjectBox LMDB reader toolkit for Node.js/TypeScript. Its core job:
walk a `data.mdb` file and hand you each stored object's raw FlatBuffers
table bytes, plus its entity id and object id — no per-field FlatBuffers or
schema knowledge for that part, just [`lmdb`](https://www.npmjs.com/package/lmdb)
(a native LMDB binding). It also covers the handful of things
`flatc --ts`/`flatc --js` output can't: `ToMany` relations (a separate LMDB
structure, not a table field) and `Flex`/`ExternalPropertyType` fields
(`flatc` only knows the base FlatBuffers type, not ObjectBox's semantic
annotation on top) — see "Beyond `flatc`" below.

Decoding those raw bytes into typed objects is left to the official
`flatc --ts` compiler — not this package, and not [`ob-dump`](..)'s own C++
core either. This is a deliberate toolkit split, not a missing feature: see
the parent project's [`docs/BACKLOG.md`](../docs/BACKLOG.md#schema-export---schema-and---fbs)
for the reasoning.

## Requires a C/C++ build toolchain

`lmdb`'s prebuilt binaries default to LMDB's newer data format v2, but
ObjectBox (like this repo's own `dart/`/`py/` packages) writes the legacy
data format v1 — opening a real ObjectBox `data.mdb` with a v2-expecting
build is undefined behavior, confirmed empirically: it segfaults the whole
Node process outright, not a catchable exception. This package's
`postinstall` script (`scripts/postinstall.cjs`) rebuilds `lmdb` from
source with `LMDB_DATA_V1=true` automatically on every install, so a
Python3/make/C-compiler toolchain (or MSVC on Windows) is required —
already the norm for this repo's `dart/` package too, which needs a C
compiler for its own vendored-LMDB build step.

## Workflow

```sh
# 1. Generate a schema JSON (entityId -> table name/shape) and a .fbs from
#    your ObjectBox model, using the ob_dump CLI (see ../README.md):
ob_dump --schema objectbox-model.json -o schema.json
ob_dump --fbs    objectbox-model.json -o schema.fbs

# 2. Generate typed TS classes with the *official* FlatBuffers compiler.
flatc --ts -o models/ schema.fbs
```

```ts
import { readObjectboxRecords, type ObRecord } from "ob-dump-reader";
import { ByteBuffer } from "flatbuffers";
import { Ammo } from "./models/ammo.js"; // from flatc --ts, see step 2 above

const AMMO_ENTITY_ID = 1; // from schema.json

readObjectboxRecords("/path/to/objectbox/dir", (record: ObRecord) => {
  if (record.entityId === AMMO_ENTITY_ID) {
    const ammo = Ammo.getRootAsAmmo(new ByteBuffer(record.data));
    console.log(`${ammo.name()}: ${ammo.bcG1()}`);
    // ... insert into whatever your new database is.
  }
});
```

## Reads are in place, no copy

`readObjectboxRecords`/`readObjectboxToManyTargets` read `data.mdb`/
`lock.mdb` directly, with no temporary copy step — they open the LMDB
environment itself read-only (matching `ob-dump`'s own C++ `LmdbReader`), so
they never need a write-capable handle. This is exactly what LMDB's MVCC
design is for: any number of readers can safely run alongside one concurrent
writer, in any process, with zero risk to the original data — even while a
live ObjectBox process has the same store open.

## Beyond `flatc`: ToMany relations and Flex/ExternalPropertyType fields

`ob_dump --schema` lists each property's `externalType` and each entity's
`relations` (id, name, target entity) when present — use that to know which
of your fields need one of these:

```ts
import { readObjectboxToManyTargets } from "ob-dump-reader";
import {
  decodeFlex,
  bytesToHex,
  bytesToUuidString,
  tryParseJsonString,
} from "ob-dump-reader/dist/decodeHelpers.js";

// ToMany: not part of the FlatBuffers table at all, so flatc has no
// accessor for it — relationId/sourceObjectId come from `ob_dump --schema`
// and the record you're looking at (record.objectId).
const authorIds = readObjectboxToManyTargets(dbDir, relationId, record.objectId);

// Flex: flatc gives you the raw bytes (a Uint8Array field) — decode with
// decodeFlex.
const value = decodeFlex(ammo.someFlexFieldArray());

// ExternalPropertyType: flatc gives you the base type's plain value (a byte
// blob for Uuid/Int128/Decimal128/Bson, a string for
// Json/JavaScript/JsonToNative) — decode with the matching helper.
const uuid = bytesToUuidString(ammo.someUuidFieldArray());
const blob = bytesToHex(ammo.someBsonFieldArray());
const parsed = tryParseJsonString(ammo.someJsonField());
```

Every one of these mirrors `ob-dump`'s own C++ decode (`src/fb_decode.cpp`)
exactly — same hex/UUID formatting, same JSON-parse-with-string-fallback for
`JavaScript`, same forward-only relation direction (see `docs/BACKLOG.md`
"ToMany relations" for why: the backward direction is an auto-maintained
index for ObjectBox's own query engine, not needed for a one-directional
dump).

## CLI

```sh
yarn ob-dump-reader /path/to/objectbox/dir
```

Prints each record's entity id, object id, and raw FlatBuffers data length.
Decoding the data itself needs a `flatc`-generated schema — see "Workflow"
above.

## Why this shape

The alternative — an FFI/native-addon wrapper around `ob-dump`'s C++ core —
would mean building/vendoring native C++ from an npm package for every
platform. Not needed here: [`lmdb`](https://www.npmjs.com/package/lmdb)
already gives a mature, actively-maintained native LMDB binding, and
`flatc --ts` already gives an officially generated, correct decoder. This
package is only the small piece connecting the two — LMDB traversal and the
ObjectBox key format (`docs/BACKLOG.md` in the parent project) — and stays
that small on purpose.

## Development

```sh
yarn install
yarn build
yarn test
```

## Integrity & Licensing

`ob-dump` was developed as an independent implementation for reading data stored in the ObjectBox format. It adheres to a "Clean Room Design" approach regarding binary software:
- **Purpose-Limited:** Built solely to support data recovery and migration to another database, for projects whose own license is incompatible with `objectbox-c`'s (a closed-source binary — see "Why this exists" in the parent project's [`docs/BACKLOG.md`](../docs/BACKLOG.md)). Not intended as, and not pursued as, a competing product to ObjectBox itself — no write support, no query engine, no ongoing-database use case, strictly a one-time read-only export path out of an existing store.
- **No Reverse Engineering:** We have performed no decompilation, disassembly, or any other analysis of the closed-source `objectbox-c` binary.
- **Open Specification:** Data parsing is based exclusively on public formats (LMDB and FlatBuffers) and the open-source code of the official schema generator ([`objectbox_generator`](https://github.com/objectbox/objectbox-dart/tree/main/generator), licensed under Apache 2.0).
- **Model-Driven:** The decoding process is driven by the user-provided `objectbox-model.json` file, which is an open, user-accessible schema definition.

This approach ensures full licensing integrity: `ob-dump` is an independent software project that contains no proprietary or misappropriated code, making it suitable for integration into projects with any licensing requirements.

## License

MIT — see [`LICENSE`](LICENSE).
