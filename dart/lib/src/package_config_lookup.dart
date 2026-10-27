import 'dart:convert';
import 'dart:io';

import 'package:path/path.dart' as p;

/// Last-resort lookup of `lib/native/<platform>/<libName>` inside [package],
/// by reading `.dart_tool/package_config.json` directly (searching upward
/// from [startDir], default the current directory).
///
/// `Isolate.resolvePackageUriSync` throws `UnsupportedError` under
/// `flutter test` (Flutter 3.47+), so the `package:` URI strategy cannot find
/// a library that `dart run <package>:<build tool>` copied into the package.
/// Reads local files only, so it is safe for offline builds.
String? findNativeLibInPackageConfig({
  required String package,
  required String platform,
  required String libName,
  Directory? startDir,
}) {
  var dir = startDir ?? Directory.current;
  while (true) {
    final config = File(p.join(dir.path, '.dart_tool', 'package_config.json'));
    if (config.existsSync()) {
      final path = _libFromConfig(config, package, platform, libName);
      if (path != null) return path;
    }
    final parent = dir.parent;
    if (parent.path == dir.path) return null;
    dir = parent;
  }
}

String? _libFromConfig(
  File config,
  String package,
  String platform,
  String libName,
) {
  try {
    final json = jsonDecode(config.readAsStringSync()) as Map<String, dynamic>;
    for (final pkg in (json['packages'] as List).cast<Map<String, dynamic>>()) {
      if (pkg['name'] != package) continue;
      var root = pkg['rootUri'] as String;
      if (!root.endsWith('/')) root = '$root/';
      final libDir = config.uri
          .resolve(root)
          .resolve((pkg['packageUri'] as String?) ?? 'lib/');
      final path = p.join(libDir.toFilePath(), 'native', platform, libName);
      return File(path).existsSync() ? path : null;
    }
  } on Object {
    // Unreadable or malformed config — not an error here, just not found.
  }
  return null;
}
