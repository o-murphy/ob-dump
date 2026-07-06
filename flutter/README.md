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

## Known limitation: crashes in a compiled release build on Linux/Windows

**Confirmed empirically** (built a scratch Flutter Linux app, ran
`flutter build linux --release`, copied only the resulting bundle to an
isolated directory with no source project or pub cache — the same shape a
distributed app install has — and ran it): `dart_lmdb2`'s own native-library
path resolution (`LMDBNative._resolveLibraryPath()`) uses
`Isolate.resolvePackageUriSync()`, which only works in JIT mode (`dart run`/
`flutter run`) — a compiled AOT release build throws
`Unsupported operation: Isolate.resolvePackageUriSync` immediately on the
first LMDB open. This affects **every** compiled Flutter Linux/Windows
release build depending on `dart_lmdb2` (directly or via this package),
regardless of how `liblmdb.so` itself was obtained.

This is not something `ob_dump_reader_flutter` (or `ob_dump_reader`) can fix
from its position as a plain API consumer of `dart_lmdb2` — the fix belongs
either upstream in [`dart_lmdb2`](https://github.com/grammatek/dart_lmdb2)
or in whatever's building the final app. `dart run`/`flutter run` (JIT) is
unaffected — everything already verified in
[`ob_dump_reader`](../dart/README.md) via `dart run` remains accurate.

Platform notes, by code inspection (only Linux was empirically built and
run — see above; the rest is reasoning about `dart_lmdb2`'s source, not
independently verified the same way):
- **iOS**: unaffected — `_openLibrary()` returns `DynamicLibrary.process()`
  unconditionally for `Platform.isIOS`, never reaching the buggy
  `_resolveLibraryPath()` code path at all.
- **macOS**: *likely* unaffected when used as a Flutter plugin — same
  `DynamicLibrary.process()` shortcut, but gated on a runtime
  `_isStaticallyLinked()` symbol-lookup check rather than being
  unconditional like iOS.
- **Android**: *no such bypass exists in the source* — `_openLibrary()`
  takes the same `_resolveLibraryPath()` path unconditionally as Linux/
  Windows do, which suggests a compiled release APK may hit the identical
  crash. Not empirically tested (would need an actual Android build/run);
  flagged here rather than assumed fine.

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
