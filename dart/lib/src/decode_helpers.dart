/// Decode helpers for the ObjectBox `ExternalPropertyType`/`Flex` cases
/// `flatc --dart`'s own generated accessors can't handle on their own —
/// `flatc` only knows about the base FlatBuffers field type, not
/// ObjectBox's semantic annotation on top of it (see ob-dump's
/// `docs/BACKLOG.md` "ExternalPropertyType"). You already know, from your
/// own model, which of your fields need one of these — `ob_dump --schema`
/// lists each property's `externalType` when present.
///
/// These mirror ob-dump's own C++ decode (`src/fb_decode.cpp`) exactly, so
/// output matches whichever tool you use on the same database.
library;

import 'dart:convert';
import 'dart:typed_data';

import 'package:flat_buffers/flex_buffers.dart' as flex;

/// Hex-encodes raw bytes — for `Int128`/`Decimal128`/`Bson` `externalType`
/// fields (all physically a `ByteVector`, representing an opaque blob
/// rather than a list of small integers). For `Uuid`, use
/// [bytesToUuidString] instead (same hex encoding, grouped into canonical
/// form).
String bytesToHex(List<int> bytes) {
  final buffer = StringBuffer();
  for (final b in bytes) {
    buffer.write((b & 0xFF).toRadixString(16).padLeft(2, '0'));
  }
  return buffer.toString();
}

/// Hex-encodes and groups a 16-byte `Uuid` `externalType` field into the
/// canonical `8-4-4-4-12` form. Falls back to plain hex for anything other
/// than exactly 16 bytes — a malformed/non-UUID field shouldn't throw just
/// because of its `externalType` annotation (same reasoning as the C++
/// decoder's `hexAsUuid`).
String bytesToUuidString(List<int> bytes) {
  final hex = bytesToHex(bytes);
  if (hex.length != 32) return hex;
  return '${hex.substring(0, 8)}-${hex.substring(8, 12)}-'
      '${hex.substring(12, 16)}-${hex.substring(16, 20)}-'
      '${hex.substring(20, 32)}';
}

/// Parses a `Json`/`JsonToNative`/`JavaScript` `externalType` field's string
/// content as JSON, returning the parsed value (`Map`/`List`/`num`/`String`/
/// `bool`/`null`). Falls back to the original string on parse failure, since
/// `JavaScript` source isn't guaranteed to be valid JSON the way `Json`/
/// `JsonToNative` are expected to be (same reasoning as the C++ decoder's
/// `decodeStringAsJson`).
dynamic tryParseJsonString(String s) {
  try {
    return jsonDecode(s);
  } on FormatException {
    return s;
  }
}

/// Decodes a `Flex` field's raw bytes (a `flatc`-generated reader gets these
/// undecoded — FlexBuffers isn't representable in `.fbs`/FlatBuffers table
/// schema at all) into the equivalent Dart value (`Map`/`List`/`num`/
/// `String`/`bool`/`null`), recursively. Uses the official `flat_buffers`
/// package's own FlexBuffers reader (`package:flat_buffers/flex_buffers.dart`
/// — the same Dart runtime `flatc --dart` output already depends on, not an
/// extra native/FFI dependency), the same way ob-dump's C++ core uses
/// upstream `google/flatbuffers`' header-only `flexbuffers.h`.
dynamic decodeFlex(Uint8List bytes) {
  // `Reference.fromBuffer` takes a `ByteBuffer` covering exactly these
  // bytes — `bytes.buffer` alone isn't safe if `bytes` is a view with a
  // nonzero offset into a larger backing buffer (e.g. straight off an
  // [ObRecord]'s FlatBuffers table), so copy to guarantee that.
  final ref = flex.Reference.fromBuffer(Uint8List.fromList(bytes).buffer);
  return jsonDecode(ref.json);
}
