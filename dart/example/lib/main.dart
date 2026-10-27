import 'package:flutter/widgets.dart';

import 'readback.dart';
import 'selftest.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await runSelfTestIfRequested();

  final records = await writeAndReadStore();
  runApp(
    Directionality(
      textDirection: TextDirection.ltr,
      child: Center(
        child: Text('ob_dump_reader read ${records.length} records'),
      ),
    ),
  );
}
