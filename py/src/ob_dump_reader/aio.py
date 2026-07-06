from collections.abc import Buffer  # або просто bytes
from os import PathLike
from typing import Any, Callable, Coroutine
from dataclasses import dataclass

import lmdb.aio

from ob_dump_reader.decode_helpers import K_KEY_TYPE_DATA, read_uint32be

__all__ = (
    "read_ob_records",
    "read_objectbox_records",
)


@dataclass
class ObRecord:
    entity_id: int
    object_id: int
    data: Buffer


async def read_objectbox_records(
    objectbox_dir: PathLike | str,
    on_record: Callable[[ObRecord], Coroutine[Any, Any, None]],
) -> None:
    async with lmdb.aio.AsyncEnvironment(
        str(objectbox_dir),
        map_size=512 * 1024 * 1024,
        max_dbs=4,
        readonly=True,
    ) as db:
        async with db.begin(write=False) as txn:
            async with txn.cursor() as cursor:
                async for key, value in cursor:
                    if key and len(key) == 8 and key[0] == K_KEY_TYPE_DATA:
                        entity_id = key[3] // 4
                        object_id = read_uint32be(key, 4)

                        # Додаємо await, оскільки коллбек тепер асинхронний
                        await on_record(
                            ObRecord(
                                entity_id=entity_id,
                                object_id=object_id,
                                data=bytes(value),
                            )
                        )


async def read_objectbox_to_many_targets(
    objectbox_dir: PathLike, relation_id: int, source_object_id: int
):
    async with lmdb.aio.AsyncEnvironment(
        str(objectbox_dir),
        map_size=512 * 1024 * 1024,
        max_dbs=4,
        readonly=True,
    ) as db:
        pass


# Aliases
read_ob_records = read_objectbox_records
read_ob_to_many_targets = read_objectbox_to_many_targets
