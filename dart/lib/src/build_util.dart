import 'dart:io';
import 'package:path/path.dart' as p;

/// Compiles the vendored LMDB source (`src/lmdb/mdb.c`/`src/lmdb/midl.c`)
/// into a shared library for the current desktop platform (Linux/macOS/
/// Windows), via a plain CMake configure+build — no FetchContent, no
/// network. Output is placed at `lib/native/<platform>/<libName>`, the
/// same location `loadLmdbLibrary()` looks for it via `package:` URI
/// resolution (works when this runs ahead of a `dart run`/`flutter run`
/// JIT session).
///
/// Run via `dart run ob_dump_reader:build` (see `bin/build.dart`).
Future<void> buildNativeLibrary(Directory projectDir) async {
  final srcDir = p.join(projectDir.path, 'src', 'lmdb');
  final buildDir = Directory(p.join(projectDir.path, 'build'));
  if (!buildDir.existsSync()) buildDir.createSync();

  print('Configuring CMake...');
  var result = await Process.run(
    'cmake',
    ['-S', srcDir, '-B', buildDir.path, '-DCMAKE_BUILD_TYPE=Release'],
  );
  stdout.write(result.stdout);
  stderr.write(result.stderr);
  if (result.exitCode != 0) {
    throw ProcessException('cmake', [], 'CMake configuration failed', result.exitCode);
  }

  print('Building liblmdb...');
  result = await Process.run(
    'cmake',
    ['--build', buildDir.path, '--config', 'Release'],
  );
  stdout.write(result.stdout);
  stderr.write(result.stderr);
  if (result.exitCode != 0) {
    throw ProcessException('cmake', [], 'Build failed', result.exitCode);
  }

  final platform = Platform.operatingSystem; // 'linux', 'macos', 'windows'
  final libName = Platform.isWindows
      ? 'lmdb.dll'
      : Platform.isMacOS
          ? 'liblmdb.dylib'
          : 'liblmdb.so';

  final candidates = [
    p.join(buildDir.path, libName),
    p.join(buildDir.path, 'Release', libName),
  ];
  final builtLib = candidates.map((c) => File(c)).firstWhere(
        (f) => f.existsSync(),
        orElse: () => throw FileSystemException(
            'Could not find built library (tried ${candidates.join(", ")})'),
      );

  final targetDir = Directory(p.join(projectDir.path, 'lib', 'native', platform));
  if (!targetDir.existsSync()) targetDir.createSync(recursive: true);
  final targetLib = File(p.join(targetDir.path, libName));
  builtLib.copySync(targetLib.path);

  print('liblmdb built and copied to: ${targetLib.path}');
}
