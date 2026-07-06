/// Locates and opens the `liblmdb` shared library built by this package
/// (`dart run ob_dump_reader:build`, or bundled by `ob_dump_reader_flutter`'s
/// native platform build).
///
/// Deliberately does *not* repeat `dart_lmdb2`'s own mistake here: relying
/// solely on `Isolate.resolvePackageUriSync()` (a `package:` URI resolver
/// backed by `.dart_tool/package_config.json`, a dev-time artifact) throws
/// `UnsupportedError` outright in a compiled AOT release build — confirmed
/// empirically against a real `flutter build linux --release` output run in
/// isolation (no source project, no pub cache). This resolver tries that
/// first (unchanged behaviour for `dart run`/`flutter run`), then falls back
/// to further strategies per platform, matching where each platform's own
/// native-library bundling convention actually places it — verified against
/// the official `flutter create --template=plugin_ffi` scaffold (generated
/// it and read its own library-loading code), not guessed.
library;

import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

import 'package:path/path.dart' as p;

/// The name CocoaPods gives the compiled framework on iOS/macOS is the
/// *podspec's* name — `ob_dump_reader_flutter`, the Flutter wrapper
/// package's own name, not this package's (`ob_dump_reader`) or the CMake
/// target's (`lmdb`) — since every source file the podspec declares
/// (`Classes/mdb.c`, `Classes/midl.c`) compiles into one framework binary
/// named after the pod itself. Hardcoded here (rather than passed in) since
/// this loader has to work standalone too (this package's own
/// `dart run ob_dump_reader:build` desktop path, which never touches a
/// framework at all) — see the platform-by-platform strategy below.
const String _iosMacosFrameworkName = 'ob_dump_reader_flutter';

DynamicLibrary loadLmdbLibrary() {
  final libName = _libName();
  final platform = Platform.operatingSystem; // 'linux', 'macos', 'windows', 'android', 'ios'

  // 1. `package:` URI resolution — works in JIT mode (`dart run`/`flutter
  //    run`), backed by .dart_tool/package_config.json. Not applicable to
  //    iOS at all (no JIT there, ever), skipped for that platform.
  if (!Platform.isIOS) {
    try {
      final uri = Isolate.resolvePackageUriSync(
          Uri.parse('package:ob_dump_reader/native/$platform/$libName'));
      if (uri != null) {
        final path = uri.toFilePath();
        if (File(path).existsSync()) return DynamicLibrary.open(path);
      }
    } on UnsupportedError {
      // AOT/release build — expected, fall through to the next strategy.
    }
  }

  // 2. iOS/macOS via the `ob_dump_reader_flutter` CocoaPods framework: a
  // bare `<Framework>.framework/<Framework>` reference, resolved by dyld
  // through the app bundle's own embedded search paths (Xcode wires this up
  // automatically when the framework is linked in) — not an absolute path
  // computed here, same as the official FFI-plugin template's own
  // `lib/<name>.dart` does it.
  if (Platform.isMacOS || Platform.isIOS) {
    try {
      return DynamicLibrary.open(
          '$_iosMacosFrameworkName.framework/$_iosMacosFrameworkName');
    } catch (_) {
      // Not running inside the Flutter plugin's framework bundle — for iOS
      // there's no other option (see FileSystemException below); macOS
      // falls through to the desktop executable-relative strategy, for the
      // plain-Dart-via-dart-run-ob_dump_reader:build case.
      if (Platform.isIOS) rethrow;
    }
  }

  // 3. Android: bundled via jniLibs, loadable by bare name through the
  //    system's standard shared-library search path — no absolute path
  //    needed (or resolvable) the way desktop builds provide one.
  if (Platform.isAndroid) {
    return DynamicLibrary.open(libName);
  }

  // 4. Executable-relative — where Flutter's own native-library bundling
  //    places plugin libraries on Linux/Windows in a compiled release build
  //    (build/linux/x64/release/bundle/lib/*.so and equivalents), or where
  //    a plain `dart run ob_dump_reader:build` + `dart compile exe` desktop
  //    workflow would place a manually-copied library next to the binary.
  final exeDir = File(Platform.resolvedExecutable).parent.path;
  final candidates = <String>[
    p.join(exeDir, 'lib', libName),
    p.join(exeDir, libName),
  ];
  for (final candidate in candidates) {
    if (File(candidate).existsSync()) return DynamicLibrary.open(candidate);
  }

  throw FileSystemException(
      'Could not locate $libName (tried package: URI and ${candidates.join(", ")}). '
      'Run `dart run ob_dump_reader:build` for a plain Dart project, or make sure '
      'ob_dump_reader_flutter is a dependency for a Flutter app.');
}

String _libName() {
  if (Platform.isWindows) return 'lmdb.dll';
  if (Platform.isMacOS) return 'liblmdb.dylib';
  return 'liblmdb.so';
}
