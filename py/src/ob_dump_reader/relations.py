from os import PathLike

import lmdb

__all__ = ("read_to_many_targets",)


def read_to_many_targets(
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
