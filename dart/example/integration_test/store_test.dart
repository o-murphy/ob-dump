// Runs on a real device/emulator (CI: Android emulator, iOS simulator): the
// library the build hook compiled has to be loadable and work there, not just
// be present in the APK.
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:ob_dump_example/readback.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  test(
    'the native LMDB library loads and reads a store on the device',
    () async {
      expect(await writeAndReadStore(), hasLength(3));
    },
  );
}
