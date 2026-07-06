from collections.abc import Buffer
from os import PathLike
from pathlib import Path
from typing import Callable
from dataclasses import dataclass
import tempfile
import shutil
import lmdb
from ob_dump_reader.decode_helper import read_uint32be

__all__ = (
    "read_ob_records",
    "read_objectbox_records",
    "read_ob_records_unsafe",
    "read_objectbox_records_unsafe",
)


@dataclass
class ObRecord:
    entity_id: int
    object_id: int
    data: Buffer


_KEY_TYPE_DATA: int = 0x18


def _read_objectbox_records_from_dir(
    objectbox_dir: PathLike, on_record: Callable[[ObRecord], None], /, safe: bool = True
) -> None:
    # 1. Opening the LMDB environment
    with lmdb.Environment(
        str(objectbox_dir),
        map_size=512 * 1024 * 1024,
        max_dbs=4,
        readonly=safe,  # set to False for initial write transaction
    ) as db:
        # 2. Write transaction to initialize the root handle (as required by LMDB/Objectbox)
        if not safe:
            with db.begin(write=True) as txn:
                pass

        # 3. Read transaction with cursor iteration and filtering
        with db.begin(write=False) as txn:
            with txn.cursor() as cursor:
                # We transfer the iteration on the pair (key, value)
                for key, value in cursor:
                    if key and len(key) == 8 and key[0] == _KEY_TYPE_DATA:
                        entity_id = key[3] // 4
                        object_id = read_uint32be(key, 4)

                        on_record(
                            ObRecord(
                                entity_id=entity_id,
                                object_id=object_id,
                                data=bytes(value),  # Copy the buffer to safe bytes
                            )
                        )


def _read_objectbox_records(
    objectbox_dir: PathLike,
    on_record: Callable[[ObRecord], None],
    /,
    copy_to_temp: bool,
) -> None:
    objectbox_path = Path(objectbox_dir)

    if copy_to_temp:
        with tempfile.TemporaryDirectory(prefix="ob_dump_reader_") as tmp:
            tmp_path = Path(tmp)
            for name in ["data.mdb", "lock.mdb"]:
                src = objectbox_path / name
                if src.exists():
                    shutil.copy(src, tmp_path / name)
            _read_objectbox_records_from_dir(tmp_path, on_record, safe=copy_to_temp)
    else:
        _read_objectbox_records_from_dir(objectbox_path, on_record, safe=copy_to_temp)


def read_objectbox_records(
    objectbox_dir: str, on_record: Callable[[ObRecord], None]
) -> None:
    _read_objectbox_records(objectbox_dir, on_record, copy_to_temp=True)


def read_objectbox_records_unsafe(
    objectbox_dir: str, on_record: Callable[[ObRecord], None]
) -> None:
    _read_objectbox_records(objectbox_dir, on_record, copy_to_temp=False)


# Aliases
read_ob_records = read_objectbox_records
read_ob_records_unsafe = read_objectbox_records_unsafe
