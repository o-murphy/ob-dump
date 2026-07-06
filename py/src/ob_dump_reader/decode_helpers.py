from collections.abc import Buffer

try:
    import orjson as json
except ImportError:
    import json
from typing import Any
from flatbuffers import flexbuffers

__all__ = (
    "uint32be",
    "read_uint32be",
    "bytes_to_hex",
    "bytes_to_uuid_string",
    "try_parse_json_string",
    "decode_flex",
)


def uint32be(v: int) -> list[int]:
    return [
        (v >> 24) & 0xFF,
        (v >> 16) & 0xFF,
        (v >> 8) & 0xFF,
        v & 0xFF,
    ]


def read_uint32be(buf: Buffer, offset: int) -> int:
    return (
        (buf[offset] << 24)
        | (buf[offset + 1] << 16)
        | (buf[offset + 2] << 8)
        | buf[offset + 3]
    )


def bytes_to_hex(b: Buffer | list[int]) -> str:
    return bytes(b).hex()


def bytes_to_uuid_string(b: bytes | list[int]) -> str:
    hex_str = bytes(b).hex()
    if len(hex_str) != 32:
        return hex_str
    return (
        f"{hex_str[0:8]}-{hex_str[8:12]}-"
        f"{hex_str[12:16]}-{hex_str[16:20]}-"
        f"{hex_str[20:32]}"
    )


def try_parse_json_string(s: str) -> Any:
    try:
        return json.loads(s)
    except json.JSONDecodeError:
        return s


def _flex_ref_to_native(ref: Any) -> Any:
    # flatbuffers.flexbuffers.Ref has no `.json`/`str()` serialization of its
    # own (an earlier version of this function assumed `str(root)` gave JSON
    # — it gives a debug repr like "Ref(buf[21:], ...)", not decodable data;
    # caught by actually calling this against real FlexBuffers bytes, not
    # assumed). Is*/As* are properties, not methods.
    if ref.IsNull:
        return None
    if ref.IsBool:
        return ref.AsBool
    if ref.IsInt:
        return ref.AsInt
    if ref.IsFloat:
        return ref.AsFloat
    if ref.IsString:
        return ref.AsString
    if ref.IsBlob:
        return bytes_to_hex(ref.AsBlob)
    if ref.IsMap:
        m = ref.AsMap
        return {
            key.AsKey: _flex_ref_to_native(value)
            for key, value in zip(m.Keys, m.Values)
        }
    if ref.IsVector or ref.IsTypedVector or ref.IsFixedTypedVector:
        vec = (
            ref.AsVector
            if ref.IsVector
            else ref.AsTypedVector
            if ref.IsTypedVector
            else ref.AsFixedTypedVector
        )
        return [_flex_ref_to_native(vec[i]) for i in range(len(vec))]
    raise ValueError(f"Unsupported FlexBuffers value: {ref}")


def decode_flex(b: Buffer) -> Any:
    root = flexbuffers.GetRoot(bytes(b))
    return _flex_ref_to_native(root)
