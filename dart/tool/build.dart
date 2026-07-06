import 'dart:io';
import 'dart:isolate';
import 'package:ob_dump_reader/src/build_util.dart';

Future<void> main(List<String> args) async {
  // `Platform.script` points at a `.dart_tool/pub/bin/` wrapper (or a
  // compiled snapshot) when invoked via `dart run ob_dump_reader:build`,
  // not this package's actual source directory — resolve the package root
  // via its own `package:` URI instead, which works the same way
  // regardless of how this script was invoked.
  final libUri = await Isolate.resolvePackageUri(Uri.parse('package:ob_dump_reader/'));
  if (libUri == null) {
    print('Build failed: could not resolve package:ob_dump_reader/ URI');
    exit(1);
  }
  final projectDir = File(libUri.toFilePath()).parent;
  try {
    await buildNativeLibrary(projectDir);
  } catch (e) {
    print('Build failed: $e');
    exit(1);
  }
}
