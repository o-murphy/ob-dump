// Build hook: compiles liblmdb from the vendored LMDB source (src/lmdb/) into a
// code asset, `package:ob_dump_reader/src/lmdb_bindings.dart`, which the
// `@Native` bindings in that file resolve at runtime. Same code path for
// `dart run`, `dart test`, `flutter run/build/test` and AOT release builds --
// nothing is downloaded, everything comes from the package archive.
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

const _libraryName = 'lmdb';

Future<void> main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;

    final os = input.config.code.targetOS;
    final root = input.packageRoot;

    await CBuilder.library(
      name: _libraryName,
      assetName: 'src/lmdb_bindings.dart',
      language: Language.c,
      sources: [
        root.resolve('src/lmdb/mdb.c').toFilePath(),
        root.resolve('src/lmdb/midl.c').toFilePath(),
        // MSVC DLLs export nothing by default and mdb.c has no dllexport
        // decorations: this file adds the /EXPORT directives (no-op elsewhere).
        root.resolve('src/lmdb_exports_win.c').toFilePath(),
      ],
      includes: [root.resolve('src/lmdb/').toFilePath()],
      // glibc < 2.34 keeps pthread_mutex_* in libpthread (Android's bionic and
      // the Apple libc have it in libc).
      libraries: [
        if (os == OS.linux) 'pthread',
        if (os == OS.windows) 'advapi32',
      ],
      flags: [
        // Google Play requires 64-bit native libraries to be aligned to 16 KB
        // pages (apps targeting Android 15+); older NDKs default to 4 KB.
        if (os == OS.android) '-Wl,-z,max-page-size=16384',
      ],
    ).run(input: input, output: output);

    // Fail the *build*, not the app at startup, if the library is not usable.
    await _verifyBuiltLibrary(input, os);
  });
}

/// Checks the library that was just built, so that a build which produced an
/// unusable library fails here instead of in the user's app at startup.
///
/// The required symbols are every `mdb_*` function the `@Native` bindings call
/// (read from lib/src/lmdb_bindings.dart).
///
///  * When the target is the machine doing the build, the library is actually
///    loaded and every required symbol is looked up exactly like the runtime
///    will do it (this catches a symbol that is not exported from the Windows
///    DLL, an unresolved dependency, a wrong architecture), and
///    `mdb_strerror(0)` is called.
///  * Cross-compiled targets (Android, iOS) cannot be loaded here; the names
///    are searched for in the binary instead. That is a weaker check: a symbol
///    that is present but hidden is not detected.
Future<void> _verifyBuiltLibrary(BuildInput input, OS os) async {
  final config = input.config.code;
  final fileName = os.libraryFileName(_libraryName, DynamicLoadingBundled());
  final lib = File.fromUri(input.outputDirectory.resolve(fileName));
  if (!lib.existsSync() || lib.lengthSync() == 0) {
    throw StateError(
      'ob_dump_reader build check: $fileName was not produced '
      '(expected ${lib.path}).',
    );
  }

  final bindings = File.fromUri(
    input.packageRoot.resolve('lib/src/lmdb_bindings.dart'),
  ).readAsStringSync();
  final required = {
    for (final m in RegExp(r"symbol:\s*'(mdb_\w+)'").allMatches(bindings))
      m.group(1)!,
  };
  if (required.isEmpty) {
    throw StateError(
      'ob_dump_reader build check: found no mdb_* functions in '
      'lib/src/lmdb_bindings.dart.',
    );
  }

  final List<String> missing;
  if (os == OS.current && config.targetArchitecture == Architecture.current) {
    try {
      final dl = DynamicLibrary.open(lib.path);
      missing = [
        for (final name in required)
          if (!dl.providesSymbol(name)) name,
      ];
      if (missing.isEmpty) {
        final strerror = dl
            .lookupFunction<
              Pointer<Void> Function(Int32),
              Pointer<Void> Function(int)
            >('mdb_strerror');
        if (strerror(0) == nullptr) {
          throw StateError('mdb_strerror(0) returned NULL');
        }
      }
    } on Object catch (e) {
      throw StateError(
        'ob_dump_reader build check: $fileName was built but cannot be '
        'loaded and called on this machine: $e',
      );
    }
  } else {
    final binary = latin1.decode(lib.readAsBytesSync(), allowInvalid: true);
    missing = [
      for (final name in required)
        if (!binary.contains(name)) name,
    ];
  }
  if (missing.isNotEmpty) {
    throw StateError(
      'ob_dump_reader build check: $fileName does not export '
      '${missing.length} function(s) the bindings call: '
      '${missing.take(8).join(', ')}${missing.length > 8 ? ', ...' : ''}. '
      'Check src/lmdb_exports_win.c and the visibility of the LMDB symbols.',
    );
  }
}
