// `flexbuffers.toObject` (unlike Python's `flatbuffers.flexbuffers`, which
// only exposes a low-level `Ref` with `Is*`/`As*` properties — see
// `py/src/ob_dump_reader/decode_helpers.py`'s `_flex_ref_to_native`) already
// walks a FlexBuffers root into a plain JS value, so no manual recursive
// converter is needed here.
// `flatbuffers`'s package.json has no "exports" map and its main entry
// (`flatbuffers.js`) doesn't re-export this — flexbuffers lives in its own
// sibling module, confirmed against the installed package's own .d.ts.
import { toObject } from "flatbuffers/js/flexbuffers.js";

export function uint32be(v: number): number[] {
  return [(v >>> 24) & 0xff, (v >>> 16) & 0xff, (v >>> 8) & 0xff, v & 0xff];
}

export function readUint32be(buf: Uint8Array, offset: number): number {
  return (
    buf[offset] * 0x1000000 +
    ((buf[offset + 1] << 16) | (buf[offset + 2] << 8) | buf[offset + 3])
  );
}

export function bytesToHex(b: Uint8Array): string {
  return Buffer.from(b).toString("hex");
}

export function bytesToUuidString(b: Uint8Array): string {
  const hex = bytesToHex(b);
  if (hex.length !== 32) return hex;
  return (
    `${hex.slice(0, 8)}-${hex.slice(8, 12)}-` +
    `${hex.slice(12, 16)}-${hex.slice(16, 20)}-` +
    `${hex.slice(20, 32)}`
  );
}

export function tryParseJsonString(s: string): unknown {
  try {
    return JSON.parse(s);
  } catch {
    return s;
  }
}

export function decodeFlex(b: Uint8Array): unknown {
  // `toObject` does `new DataView(buffer)` internally, which needs a real
  // ArrayBuffer spanning exactly this data — Node Buffers are frequently
  // views into a larger shared pool (byteOffset/byteLength don't cover the
  // whole underlying buffer), so `b.buffer` can't be passed as-is.
  const arrayBuffer = (
    b.byteOffset === 0 && b.byteLength === b.buffer.byteLength
      ? b.buffer
      : b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength)
  ) as ArrayBuffer;
  return toObject(arrayBuffer);
}
