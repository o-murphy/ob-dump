# ob_dump_reader_flutter

![GitHub License](https://img.shields.io/github/license/o-murphy/ob-dump)
![Pub Version](https://img.shields.io/pub/v/ob_dump_reader_flutter?logo=flutter)


Same API as [`ob_dump_reader`](../dart) — `readObjectBoxRecords`,
`ObRecord`, `readToManyTargets`, and the `Flex`/`ExternalPropertyType`
decode helpers — re-exported unchanged. Use **this** package (not
`ob_dump_reader` directly) from inside a Flutter app; use `ob_dump_reader`
directly for plain Dart (CLI scripts, server, tests).

## Why a separate package

This package is a real Flutter plugin (`ffiPlugin: true` in `pubspec.yaml`,
for all five platforms — Android, iOS, Linux, macOS, Windows). It compiles
the same vendored LMDB C source [`ob_dump_reader`](../dart) binds to
(`src/lmdb/`, duplicated here since these are separate published packages
with no reliable cross-package relative path) via each platform's own
native build convention — CMake on Linux/Windows/Android (through the NDK),
a CocoaPods podspec plus small forwarder `.c` files on iOS/macOS — and
Flutter's tooling bundles the resulting library into the shipped app
automatically. `ob_dump_reader` on its own only offers
`dart run ob_dump_reader:build`, which produces a library for the current
desktop machine — useful for a plain Dart script, not for an app another
user installs on a *different* device.

`ffiPlugin: true` (rather than `pluginClass:`) is used because there is no
method-channel surface at all here — confirmed against the official
`flutter create --template=plugin_ffi` scaffold, not guessed.

## Verified platforms

- **Linux**: built via a real `flutter build linux --release`, the output
  bundle copied to an isolated directory (no source project, no pub cache)
  and run directly, reading a real ObjectBox database successfully.
- **Android**: a real debug/release APK built and installed on an emulator;
  `liblmdb.so` present for all three ABIs (arm64-v8a, armeabi-v7a, x86_64),
  confirmed working via logcat output from an on-device LMDB write+read
  round trip.
- **Windows / macOS / iOS**: written to match the official
  `flutter create --template=plugin_ffi` scaffold exactly (generated that
  scaffold and diffed against it), but **not** built on real hardware — no
  Windows or macOS/Xcode toolchain available in the environment this was
  developed in. Flagged here rather than assumed working.

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
