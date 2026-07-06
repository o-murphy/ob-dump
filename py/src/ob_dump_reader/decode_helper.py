from collections.abc import Buffer
import json
from typing import Any
from flatbuffers import flexbuffers


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


def decode_flex(b: Buffer) -> Any:
    safe_bytes = bytes(b)
    root = flexbuffers.GetRoot(safe_bytes)
    json_str = root.json if hasattr(root, "json") else str(root)
    return json.loads(json_str)
