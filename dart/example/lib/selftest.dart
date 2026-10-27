import 'dart:io';

import 'readback.dart';

/// Headless self-test used by CI: `OB_DUMP_EXAMPLE_SELFTEST=1 ob_dump_example`
/// loads the native LMDB library, writes a store, reads it back, prints a
/// marker and exits (the Flatpak build runs it inside the sandbox).
Future<void> runSelfTestIfRequested() async {
  if (Platform.environment['OB_DUMP_EXAMPLE_SELFTEST'] != '1') return;
  try {
    final records = await writeAndReadStore();
    _report('ob_dump_reader self-test ok: ${records.length} records');
    exit(0);
  } catch (e, st) {
    _report('ob_dump_reader self-test FAILED: $e\n$st', error: true);
    exit(1);
  }
}

/// Prints the result, and also writes it to `OB_DUMP_EXAMPLE_SELFTEST_OUT` when
/// set: a GUI executable (the Windows runner) has no reliable stdout.
void _report(String message, {bool error = false}) {
  (error ? stderr : stdout).writeln(message);
  final out = Platform.environment['OB_DUMP_EXAMPLE_SELFTEST_OUT'];
  if (out == null || out.isEmpty) return;
  // A failed write (e.g. the macOS App Sandbox forbids the path) must not keep
  // the process from reaching exit(): the result is on stdout/stderr anyway.
  try {
    File(out).writeAsStringSync('$message\n');
  } on Object catch (e) {
    stderr.writeln('could not write self-test result to $out: $e');
  }
}
