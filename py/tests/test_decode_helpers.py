from flatbuffers import flexbuffers

from ob_dump_reader.decode_helpers import (
    bytes_to_hex,
    bytes_to_uuid_string,
    decode_flex,
    read_uint32be,
    try_parse_json_string,
    uint32be,
)


def test_uint32be_and_read_uint32be_roundtrip():
    encoded = bytes(uint32be(0x01020304))
    assert encoded == b"\x01\x02\x03\x04"
    assert read_uint32be(encoded, 0) == 0x01020304


def test_bytes_to_hex_encodes_raw_bytes_as_lowercase_hex():
    assert bytes_to_hex(b"\xde\xad\xbe\xef") == "deadbeef"


def test_bytes_to_uuid_string_groups_16_bytes_into_canonical_form():
    raw = bytes.fromhex("0123456789abcdef0123456789abcdef")
    assert bytes_to_uuid_string(raw) == "01234567-89ab-cdef-0123-456789abcdef"


def test_bytes_to_uuid_string_falls_back_to_plain_hex_for_non_16_byte_input():
    raw = b"\x01\x02\x03"
    assert bytes_to_uuid_string(raw) == "010203"


def test_try_parse_json_string_parses_valid_json():
    assert try_parse_json_string('{"a": 1}') == {"a": 1}


def test_try_parse_json_string_falls_back_to_the_original_string_on_invalid_json():
    assert try_parse_json_string("not json") == "not json"


def test_decode_flex_decodes_a_flexbuffers_map_to_a_python_dict():
    encoded = flexbuffers.Dumps(
        {"a": 1, "b": [1, 2, 3], "c": "hi", "d": True, "e": None, "f": b"\x01\x02"}
    )
    assert decode_flex(bytes(encoded)) == {
        "a": 1,
        "b": [1, 2, 3],
        "c": "hi",
        "d": True,
        "e": None,
        "f": "0102",
    }


def test_decode_flex_decodes_a_flexbuffers_vector_root_to_a_python_list():
    encoded = flexbuffers.Dumps([1, "two", 3.5, [4, 5]])
    assert decode_flex(bytes(encoded)) == [1, "two", 3.5, [4, 5]]
