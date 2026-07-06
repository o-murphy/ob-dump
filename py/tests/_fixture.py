"""Shared LMDB fixture-writing helper for tests — writes raw entries via
plain lmdb.Environment, independent of the reader code under test."""

import lmdb


def write_fixture(objectbox_dir: str, entries: dict[bytes, bytes]) -> None:
    env = lmdb.Environment(
        objectbox_dir, map_size=10 * 1024 * 1024, max_dbs=4, readonly=False
    )
    with env.begin(write=True) as txn:
        for key, value in entries.items():
            txn.put(key, value)
    env.close()


def data_key(entity_id: int, object_id: int) -> bytes:
    return bytes([0x18, 0x00, 0x00, entity_id * 4]) + object_id.to_bytes(4, "big")


def relation_key(
    relation_id: int, direction: int, source_id: int, target_id: int
) -> bytes:
    return (
        bytes([0x08, 0x00, 0x00, (relation_id << 2) | direction])
        + source_id.to_bytes(4, "big")
        + target_id.to_bytes(4, "big")
    )


def schema_key(entity_id: int) -> bytes:
    return bytes([0x00, 0x00, 0x00, entity_id * 4]) + (0).to_bytes(4, "big")
