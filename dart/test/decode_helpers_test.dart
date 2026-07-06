import 'package:flat_buffers/flex_buffers.dart' as flex;
import 'package:ob_dump_reader/ob_dump_reader.dart';
import 'package:test/test.dart';

void main() {
  test('bytesToHex encodes raw bytes as lowercase hex', () {
    expect(bytesToHex([0xde, 0xad, 0xbe, 0xef]), 'deadbeef');
    expect(bytesToHex([]), '');
  });

  test('bytesToUuidString groups 16 bytes into canonical form', () {
    final uuidBytes = [
      0x55, 0x0e, 0x84, 0x00, 0xe2, 0x9b, 0x41, 0xd4,
      0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00,
    ];
    expect(bytesToUuidString(uuidBytes), '550e8400-e29b-41d4-a716-446655440000');
  });

  test('bytesToUuidString falls back to plain hex for non-16-byte input', () {
    expect(bytesToUuidString([0xde, 0xad]), 'dead');
  });

  test('tryParseJsonString parses valid JSON', () {
    expect(tryParseJsonString('{"a":1,"b":[true,null]}'), {
      'a': 1,
      'b': [true, null],
    });
    expect(tryParseJsonString('[1,2,3]'), [1, 2, 3]);
  });

  test('tryParseJsonString falls back to the original string on invalid JSON', () {
    const jsSource = 'function() { return 1; }';
    expect(tryParseJsonString(jsSource), jsSource);
  });

  test('decodeFlex decodes a FlexBuffers map to a Dart Map', () {
    final buffer = flex.Builder.buildFromObject({'a': 1, 'b': true, 'c': null});
    final bytes = buffer.asUint8List();

    expect(decodeFlex(bytes), {'a': 1, 'b': true, 'c': null});
  });

  test('decodeFlex decodes a FlexBuffers vector root to a Dart List', () {
    final buffer = flex.Builder.buildFromObject([1, 2, 'three']);
    final bytes = buffer.asUint8List();

    expect(decodeFlex(bytes), [1, 2, 'three']);
  });
}
