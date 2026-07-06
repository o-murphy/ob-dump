from collections.abc import Buffer
from os import PathLike
from typing import Callable
from dataclasses import dataclass

import lmdb

from ob_dump_reader._constants import (
    KEY_TYPE_DATA,
    KEY_TYPE_RELATION,
    RELATION_DIRECTION_FORWARD,
)
from ob_dump_reader.decode_helpers import read_uint32be

__all__ = (
    "ObRecord",
    "read_objectbox_records",
    "read_objectbox_to_many_targets",
)


@dataclass
class ObRecord:
    entity_id: int
    object_id: int
    data: Buffer


def read_objectbox_records(
    objectbox_dir: PathLike,
    on_record: Callable[[ObRecord], None],
) -> None:
    # readonly=True is enough on its own — LMDB's MVCC design lets any number
    # of readers run safely alongside one concurrent writer, in any process,
    # so no write transaction is needed just to open the store.
    with lmdb.Environment(
        str(objectbox_dir),
        map_size=512 * 1024 * 1024,
        max_dbs=4,
        readonly=True,
    ) as db:
        with db.begin(write=False) as txn:
            with txn.cursor() as cursor:
                for key, value in cursor:
                    if key and len(key) == 8 and key[0] == KEY_TYPE_DATA:
                        entity_id = key[3] // 4
                        object_id = read_uint32be(key, 4)

                        on_record(
                            ObRecord(
                                entity_id=entity_id,
                                object_id=object_id,
                                data=bytes(value),  # copy out of the mmap'd page
                            )
                        )


def read_objectbox_to_many_targets(
    objectbox_dir: PathLike, relation_id: int, source_object_id: int
) -> list[int]:
    """Resolves a `ToMany` relation's forward-direction target ids.

    Not part of the FlatBuffers table at all — a separate LMDB key range,
    12 bytes: type(0x08) + 2 reserved bytes + ((relation_id << 2) |
    direction) + source_id (u32 BE) + target_id (u32 BE), value always
    empty. `direction` 0 is the declared forward direction; 2 is an
    auto-maintained reverse index for ObjectBox's own query engine, not
    needed for a one-directional dump.
    """
    prefix = bytes(
        [
            KEY_TYPE_RELATION,
            0x00,
            0x00,
            (relation_id << 2) | RELATION_DIRECTION_FORWARD,
        ]
    ) + source_object_id.to_bytes(4, "big")

    targets: list[int] = []
    with lmdb.Environment(
        str(objectbox_dir),
        map_size=512 * 1024 * 1024,
        max_dbs=4,
        readonly=True,
    ) as db:
        with db.begin(write=False) as txn:
            with txn.cursor() as cursor:
                if cursor.set_range(prefix):
                    for key, _ in cursor:
                        if bytes(key[:8]) != prefix:
                            break
                        targets.append(read_uint32be(key, 8))
    return targets
