from collections.abc import Buffer
from os import PathLike
from typing import Any, Callable, Coroutine
from dataclasses import dataclass

import lmdb
import lmdb.aio

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


async def read_objectbox_records(
    objectbox_dir: PathLike | str,
    on_record: Callable[[ObRecord], Coroutine[Any, Any, None]],
) -> None:
    # AsyncEnvironment wraps a plain (sync) Environment — it doesn't take a
    # path/map_size/etc. itself (lmdb.aio's own docstring example). Cursor
    # has no __aiter__, and its iternext() collects every remaining entry
    # into a list before returning (nothing streams past the executor
    # boundary) — stepping via first()/next() instead keeps this a true
    # per-record callback, not "load the whole database, then callback".
    env = lmdb.Environment(
        str(objectbox_dir),
        map_size=512 * 1024 * 1024,
        max_dbs=4,
        readonly=True,
    )
    aenv = lmdb.aio.wrap(env)
    async with aenv:
        async with aenv.begin(write=False) as txn:
            async with txn.cursor() as cursor:
                has_item = await cursor.first()
                while has_item:
                    key, value = cursor.item()
                    if key and len(key) == 8 and key[0] == KEY_TYPE_DATA:
                        entity_id = key[3] // 4
                        object_id = read_uint32be(key, 4)

                        await on_record(
                            ObRecord(
                                entity_id=entity_id,
                                object_id=object_id,
                                data=bytes(value),
                            )
                        )
                    has_item = await cursor.next()


async def read_objectbox_to_many_targets(
    objectbox_dir: PathLike | str, relation_id: int, source_object_id: int
) -> list[int]:
    """Async counterpart of `ob_dump_reader.read_objectbox_to_many_targets` —
    see its docstring for the key format."""
    prefix = bytes(
        [
            KEY_TYPE_RELATION,
            0x00,
            0x00,
            (relation_id << 2) | RELATION_DIRECTION_FORWARD,
        ]
    ) + source_object_id.to_bytes(4, "big")

    targets: list[int] = []
    env = lmdb.Environment(
        str(objectbox_dir),
        map_size=512 * 1024 * 1024,
        max_dbs=4,
        readonly=True,
    )
    aenv = lmdb.aio.wrap(env)
    async with aenv:
        async with aenv.begin(write=False) as txn:
            async with txn.cursor() as cursor:
                has_item = await cursor.set_range(prefix)
                while has_item:
                    key, _ = cursor.item()
                    if bytes(key[:8]) != prefix:
                        break
                    targets.append(read_uint32be(key, 8))
                    has_item = await cursor.next()
    return targets
