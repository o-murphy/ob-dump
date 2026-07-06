from collections.abc import Buffer
from os import PathLike
from typing import Callable
from dataclasses import dataclass

import lmdb

from ob_dump_reader.decode_helpers import read_uint32be

__all__ = (
    "read_ob_records",
    "read_ob_to_many_targets",
    "read_objectbox_records",
    "read_objectbox_to_many_targets",
)


KEY_TYPE_DATA: int = 0x18
KEY_TYPE_RELATION: int = 0x08
RELATION_DIRECTION_FFORWARD: int = 0


@dataclass
class ObRecord:
    entity_id: int
    object_id: int
    data: Buffer


def read_objectbox_records(
    objectbox_dir: PathLike,
    on_record: Callable[[ObRecord], None],
) -> None:
    # 1. Opening the LMDB environment
    with lmdb.Environment(
        str(objectbox_dir),
        map_size=512 * 1024 * 1024,
        max_dbs=4,
        readonly=True,  # set to False for initial write transaction
    ) as db:
        # # 2. Write transaction to initialize the root handle (as required by LMDB/Objectbox)
        # if not db.readonly:
        #     with db.begin(write=True) as txn:
        #         pass

        # 3. Read transaction with cursor iteration and filtering
        with db.begin(write=False) as txn:
            with txn.cursor() as cursor:
                # We transfer the iteration on the pair (key, value)
                for key, value in cursor:
                    if key and len(key) == 8 and key[0] == KEY_TYPE_DATA:
                        entity_id = key[3] // 4
                        object_id = read_uint32be(key, 4)

                        on_record(
                            ObRecord(
                                entity_id=entity_id,
                                object_id=object_id,
                                data=bytes(value),  # Copy the buffer to safe bytes
                            )
                        )


def read_objectbox_to_many_targets(
    objectbox_dir: PathLike, relation_id: int, source_object_id: int
):
    # 1. Opening the LMDB environment
    with lmdb.Environment(
        str(objectbox_dir),
        map_size=512 * 1024 * 1024,
        max_dbs=4,
        readonly=True,  # set to False for initial write transaction
    ) as db:
        # # 2. Write transaction to initialize the root handle (as required by LMDB/Objectbox)
        # if not db.readonly:
        #     with db.begin(write=True) as txn:
        #         pass
        pass


# Aliases
read_ob_records = read_objectbox_records
read_ob_to_many_targets = read_objectbox_to_many_targets
