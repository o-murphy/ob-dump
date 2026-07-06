from os import PathLike
from pathlib import Path
import shutil
import tempfile

import lmdb

__all__ = (
    "read_to_many_targets",
    "read_to_many_targets_unsafe",
)

_KEY_TYPE_RELATION: int = 0x08
_RELATION_DIRECTION_FORWARD: int = 0


def _read_to_many_targets_form_dir(
    objectbox_dir: PathLike,
    relation_id: int,
    source_object_id: int,
    /,
    safe: bool = True,
):
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


def _read_to_many_targets(
    objectbox_dir: PathLike,
    relation_id: int,
    source_object_id: int,
    /,
    copy_to_temp: bool,
):

    objectbox_path = Path(objectbox_dir)  # noqa: F821

    if copy_to_temp:
        with tempfile.TemporaryDirectory(prefix="ob_dump_reader_") as tmp:
            tmp_path = Path(tmp)
            for name in ["data.mdb", "lock.mdb"]:
                src = objectbox_path / name
                if src.exists():
                    shutil.copy(src, tmp_path / name)  # noqa: F821
            _read_to_many_targets_form_dir(
                tmp_path, relation_id, source_object_id, safe=copy_to_temp
            )
    else:
        _read_to_many_targets_form_dir(
            objectbox_path, relation_id, source_object_id, safe=copy_to_temp
        )


def read_to_many_targets(
    objectbox_dir: PathLike, relation_id: int, source_object_id: int
):
    return _read_to_many_targets(
        objectbox_dir, relation_id, source_object_id, copy_to_temp=True
    )


def read_to_many_targets_unsafe(
    objectbox_dir: PathLike, relation_id: int, source_object_id: int
):
    return _read_to_many_targets(
        objectbox_dir, relation_id, source_object_id, copy_to_temp=False
    )
