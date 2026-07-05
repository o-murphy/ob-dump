# ob_dump_reader

Minimal ObjectBox LMDB reader toolkit for Dart. This package does exactly
one thing: walk a `data.mdb` file and hand you each stored object's raw
FlatBuffers table bytes, plus its entity id and object id. It has **no
FlatBuffers or schema knowledge at all** — its only dependency is
[`dart_lmdb2`](https://pub.dev/packages/dart_lmdb2).

Decoding those raw bytes into typed objects is left to the official
`flatc --dart` compiler — not this package, and not [`ob-dump`](..)'s own
C++ core either. This is a deliberate toolkit split, not a missing feature:
see the parent project's [`docs/BACKLOG.md`](../docs/BACKLOG.md#schema-export---schema-and---fbs)
for the reasoning.

**Using this from a Flutter app?** Depend on
[`../flutter`](../flutter) (`ob_dump_reader_flutter`) instead of this
package directly — same API, but it pulls in `flutter_lmdb2` so Flutter's
plugin tooling bundles the native LMDB library into your Android/iOS/macOS
app. This package's own `dart_lmdb2` dependency only fetches a binary into
your pub cache, which works for a `dart run` script but isn't part of a
shipped mobile app.

## Workflow

```sh
# 1. Generate a schema JSON (entityId -> table name/shape) and a .fbs from
#    your ObjectBox model, using the ob_dump CLI (see ../README.md):
ob_dump --schema objectbox-model.json -o schema.json
ob_dump --fbs    objectbox-model.json -o schema.fbs

# 2. Generate typed Dart classes with the *official* FlatBuffers compiler.
#    (This is `flatc`, not `flatcc` — flatcc is a separate, C-only
#    implementation with no Dart backend.) flatc always names its output
#    `<input>_generated.dart`; rename to the `.g.dart` suffix Dart tooling
#    conventionally uses for generated code (e.g. so a `*.g.dart` entry in
#    .gitignore or an analyzer exclusion rule already covers it).
flatc --dart -o lib/generated schema.fbs
mv lib/generated/schema_generated.dart lib/generated/schema.g.dart
```

```dart
import 'package:ob_dump_reader/ob_dump_reader.dart';
import 'generated/schema.g.dart'; // from flatc --dart, see step 2 above

const ammoEntityId = 1; // from schema.json

Future<void> main() async {
  // Walks data.mdb, calling this callback once per stored ObjectBox object
  // with its raw (still-undecoded) FlatBuffers bytes — see [ObRecord].
  await readObjectBoxRecords('/path/to/objectbox/dir', (record) {
    if (record.entityId == ammoEntityId) {
      final ammo = Ammo(record.data); // flatc-generated class decodes the bytes
      print('${ammo.name}: ${ammo.bcG1}');
      // ... insert into whatever your new database is.
    }
  });
}
```

## Copy-free reads (large databases)

`readObjectBoxRecords` works on a temporary copy of `data.mdb`/`lock.mdb` —
needed because opening an ObjectBox store requires one write-capable LMDB
transaction (nothing is actually written, just an internal handle
registration), and doing that against a live database risks colliding with
a running ObjectBox process. That copy costs disk I/O/space proportional to
the database size. (`lock.mdb` itself is just LMDB's coordination file, not
data — see `docs/BACKLOG.md` phased-plan item 10 for why we copy it anyway.)

If you know the source isn't in use by anything else and skipping that cost
matters (a large database), use `readObjectBoxRecordsUnsafe` instead — same
signature, no copy, reads `objectboxDir` directly. See its doc comment for
the exact risk before reaching for it.

## Why this shape

The alternative — an FFI wrapper around `ob-dump`'s C++ core — would mean
vendoring/building native C++ from a pub.dev package for every platform.
None of that is needed here: `dart_lmdb2` already gives pure-Dart-callable
LMDB access, and `flatc --dart` already gives an officially generated,
correct decoder. This package is only the small piece connecting the two —
LMDB traversal and the ObjectBox key format
(`docs/BACKLOG.md` in the parent project) — and stays that small on purpose.

## Verified end-to-end

Confirmed against a real ObjectBox database, independent of every other
verification already done in this repo (the Dart PoC this was ported from,
the C++ decoder, and a C++ `flatc`-generated consumer): generated `schema.fbs`
from the real model, ran `flatc --dart` on it, read a real record with
`readObjectBoxRecords`, decoded it with the generated `Ammo` class — every
field matched exactly.

## License

MIT — see [`LICENSE`](LICENSE).
