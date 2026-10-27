import 'package:flutter_test/flutter_test.dart';
import 'package:ob_dump_example/readback.dart';

void main() {
  // `flutter test` is where the build hook has to hand the native library over.
  test(
    'the native LMDB library loads and reads a store under flutter test',
    () async {
      expect(await writeAndReadStore(), hasLength(3));
    },
  );
}
