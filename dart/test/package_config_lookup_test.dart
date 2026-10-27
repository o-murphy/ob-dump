import 'dart:io';

import 'package:ob_dump_reader/src/package_config_lookup.dart';
import 'package:path/path.dart' as p;
import 'package:test/test.dart';

void main() {
  late Directory tmp;

  setUp(() => tmp = Directory.systemTemp.createTempSync('native_lookup_'));
  tearDown(() => tmp.deleteSync(recursive: true));

  void writeConfig(
    Directory dir,
    String rootUri, {
    String packageUri = 'lib/',
  }) {
    Directory(p.join(dir.path, '.dart_tool')).createSync(recursive: true);
    File(
      p.join(dir.path, '.dart_tool', 'package_config.json'),
    ).writeAsStringSync(
      '{"configVersion":2,"packages":[{"name":"ob_dump_reader","rootUri":"$rootUri",'
      '"packageUri":"$packageUri","languageVersion":"3.0"}]}',
    );
  }

  File makeLib(Directory root) =>
      File(p.join(root.path, 'lib', 'native', 'linux', 'liblmdb.so'))
        ..createSync(recursive: true);

  String? find(Directory start) => findNativeLibInPackageConfig(
    package: 'ob_dump_reader',
    platform: 'linux',
    libName: 'liblmdb.so',
    startDir: start,
  );

  test('resolves a rootUri relative to the config file', () {
    final pkg = Directory(p.join(tmp.path, 'pkgs', 'ob_dump_reader'))
      ..createSync(recursive: true);
    final lib = makeLib(pkg);
    final app = Directory(p.join(tmp.path, 'app'))..createSync();
    writeConfig(app, '../../pkgs/ob_dump_reader');
    expect(find(app), lib.path);
  });

  test('resolves an absolute file:// rootUri', () {
    final pkg = Directory(p.join(tmp.path, 'abs'))..createSync();
    final lib = makeLib(pkg);
    writeConfig(tmp, pkg.uri.toString());
    expect(find(tmp), lib.path);
  });

  test('searches parent directories for the config', () {
    final pkg = Directory(p.join(tmp.path, 'ob_dump_reader'))..createSync();
    final lib = makeLib(pkg);
    writeConfig(tmp, '../ob_dump_reader');
    final deep = Directory(p.join(tmp.path, 'a', 'b'))
      ..createSync(recursive: true);
    expect(find(deep), lib.path);
  });

  test('returns null when the library file is missing', () {
    Directory(p.join(tmp.path, 'ob_dump_reader')).createSync();
    writeConfig(tmp, '../ob_dump_reader');
    expect(find(tmp), isNull);
  });

  test('returns null for a malformed config', () {
    Directory(p.join(tmp.path, '.dart_tool')).createSync();
    File(p.join(tmp.path, '.dart_tool', 'package_config.json'))
        .writeAsStringSync('not json');
    expect(find(tmp), isNull);
  });
}
