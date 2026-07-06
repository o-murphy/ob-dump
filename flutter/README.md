# ob_dump_reader_flutter

![GitHub License](https://img.shields.io/github/license/o-murphy/ob-dump)
![Pub Version](https://img.shields.io/pub/v/ob_dump_reader_flutter?logo=flutter)


Same API as [`ob_dump_reader`](../dart) — `readObjectBoxRecords`,
`readObjectBoxRecordsUnsafe`, `ObRecord` — re-exported unchanged. Use
**this** package (not `ob_dump_reader` directly) from inside a Flutter app;
use `ob_dump_reader` directly for plain Dart (CLI scripts, server, tests).

## Why a separate package

`ob_dump_reader` depends on [`dart_lmdb2`](https://pub.dev/packages/dart_lmdb2),
which loads its native LMDB library via a `dart run dart_lmdb2:fetch_native`
step that downloads a binary into the package's own directory in your pub
cache. That's fine for a `dart run` script on a desktop — it is **not** part
of a compiled Android/iOS app bundle a real user installs.

[`flutter_lmdb2`](https://pub.dev/packages/flutter_lmdb2) is a genuine
Flutter plugin (Android Gradle / iOS podspec / macOS) that bundles the same
native library properly for a shipped app, and re-exports `dart_lmdb2`'s
identical API (its own `lib/lmdb.dart` is literally
`export 'package:dart_lmdb2/lmdb.dart';` — confirmed by inspecting the
published package, not assumed).

This package's only job is adding `flutter_lmdb2` to the resolved dependency
graph so Flutter's plugin-discovery tooling (which scans the *whole* graph,
not just direct dependencies) notices it and wires up native bundling for
whatever app depends on this package — transitively, without that app's own
`pubspec.yaml` needing to mention `flutter_lmdb2` at all. No source code
here needs to import `flutter_lmdb2` directly for that to work; it just
needs to be present as a dependency.

## Usage

Identical to [`ob_dump_reader`](../dart/README.md) — see that package's
README for the full workflow (`ob_dump --schema`/`--fbs` + `flatc --dart`).
Just import this package instead:

```dart
import 'package:ob_dump_reader_flutter/ob_dump_reader_flutter.dart';
```

## Integrity & Licensing

`ob-dump` was developed as an independent implementation for reading data stored in the ObjectBox format. It adheres to a "Clean Room Design" approach regarding binary software:
- **No Reverse Engineering:** We have performed no decompilation, disassembly, or any other analysis of the closed-source `objectbox-c` binary.
- **Open Specification:** Data parsing is based exclusively on public formats (LMDB and FlatBuffers) and the open-source code of the official schema generator ([`objectbox_generator`](https://github.com/objectbox/objectbox-dart/tree/main/generator), licensed under Apache 2.0).
- **Model-Driven:** The decoding process is driven by the user-provided `objectbox-model.json` file, which is an open, user-accessible schema definition.

This approach ensures full licensing integrity: `ob-dump` is an independent software project that contains no proprietary or misappropriated code, making it suitable for integration into projects with any licensing requirements.

## License

MIT — see [`LICENSE`](LICENSE).
