/// Re-exports `ob_dump_reader`'s entire API unchanged — see that package's
/// docs for [readObjectBoxRecords], [readObjectBoxRecordsUnsafe],
/// [ObRecord], [readToManyTargets] (`ToMany` relations), and the
/// `Flex`/`ExternalPropertyType` decode helpers ([decodeFlex],
/// [bytesToHex], [bytesToUuidString], [tryParseJsonString]).
///
/// The only reason this package exists (instead of just depending on
/// `ob_dump_reader` directly from a Flutter app) is to pull `flutter_lmdb2`
/// into the resolved dependency graph: Flutter's plugin-discovery tooling
/// scans the *whole* graph (not just direct dependencies) for packages
/// declaring a `flutter: plugin:` section, and wires up their native-library
/// bundling (Android Gradle / iOS podspec / macOS) accordingly. `dart_lmdb2`
/// itself (which `ob_dump_reader` actually imports and uses) has no such
/// section — its own `fetch_native` step downloads a binary into the pub
/// cache, which is fine for a plain `dart run` CLI script on desktop, but
/// isn't part of a shipped Android/iOS app bundle. `flutter_lmdb2` re-exports
/// `dart_lmdb2`'s identical API (see its own `lib/lmdb.dart`) and depends on
/// it internally — it exists purely to add that bundling. No source code
/// here needs to reference it directly for this to work.
library ob_dump_reader_flutter;

export 'package:ob_dump_reader/ob_dump_reader.dart';
