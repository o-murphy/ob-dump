import { encode } from "flatbuffers/js/flexbuffers.js";
import { describe, expect, it } from "vitest";

import {
  bytesToHex,
  bytesToUuidString,
  decodeFlex,
  readUint32be,
  tryParseJsonString,
  uint32be,
} from "../src/decodeHelpers.js";

describe("readUint32be / uint32be", () => {
  it("round-trips a big-endian uint32", () => {
    const value = 0x01020304;
    expect(readUint32be(Buffer.from(uint32be(value)), 0)).toBe(value);
  });

  it("reads at a non-zero offset", () => {
    const buf = Buffer.concat([Buffer.from([0xff]), Buffer.from(uint32be(42))]);
    expect(readUint32be(buf, 1)).toBe(42);
  });
});

describe("bytesToHex", () => {
  it("hex-encodes raw bytes", () => {
    expect(bytesToHex(Buffer.from([0xde, 0xad, 0xbe, 0xef]))).toBe("deadbeef");
  });
});

describe("bytesToUuidString", () => {
  it("formats 16 bytes as a canonical UUID", () => {
    const bytes = Buffer.from(
      "0123456789abcdef0123456789abcdef",
      "hex",
    ).subarray(0, 16);
    expect(bytesToUuidString(bytes)).toBe(
      "01234567-89ab-cdef-0123-456789abcdef",
    );
  });

  it("falls back to plain hex for non-16-byte input", () => {
    expect(bytesToUuidString(Buffer.from([1, 2, 3]))).toBe("010203");
  });
});

describe("tryParseJsonString", () => {
  it("parses valid JSON", () => {
    expect(tryParseJsonString('{"a":1}')).toEqual({ a: 1 });
  });

  it("returns the original string when parsing fails", () => {
    expect(tryParseJsonString("not json")).toBe("not json");
  });
});

describe("decodeFlex", () => {
  it("decodes a FlexBuffers map/vector/scalar mix into a plain JS value", () => {
    const bytes = encode({ a: 1, b: ["x", "y"], c: true });
    expect(decodeFlex(bytes)).toEqual({ a: 1, b: ["x", "y"], c: true });
  });

  it("decodes correctly when the bytes are a view into a larger pooled buffer", () => {
    // Node's small Buffer.from/allocations are frequently slices of a
    // shared underlying ArrayBuffer (byteOffset != 0) — the exact case
    // `decodeFlex`'s ArrayBuffer conversion has to handle correctly.
    const encoded = Buffer.from(encode({ n: 42 }));
    const padded = Buffer.concat([Buffer.from([0xff, 0xff, 0xff]), encoded]);
    const view = padded.subarray(3);
    expect(view.byteOffset).toBeGreaterThan(0);
    expect(decodeFlex(view)).toEqual({ n: 42 });
  });
});
