import asyncio

import ob_dump_reader.aio as ob_aio

from _fixture import data_key, relation_key, schema_key, write_fixture


def test_extracts_data_records_and_ignores_non_data_keys(tmp_path):
    d = str(tmp_path)
    write_fixture(
        d,
        {
            schema_key(1): b"",
            data_key(1, 7): b"hello",
            relation_key(1, 0, 7, 9): b"",
        },
    )

    records = []

    async def on_record(r):
        records.append(r)

    asyncio.run(ob_aio.read_objectbox_records(d, on_record))

    assert [(r.entity_id, r.object_id, r.data) for r in records] == [(1, 7, b"hello")]


def test_resolves_forward_direction_to_many_links_ignoring_backward_ones(tmp_path):
    d = str(tmp_path)
    write_fixture(
        d,
        {
            relation_key(1, 0, 7, 9): b"",
            relation_key(1, 0, 7, 11): b"",
            relation_key(1, 2, 9, 7): b"",
        },
    )

    targets = asyncio.run(
        ob_aio.read_objectbox_to_many_targets(d, relation_id=1, source_object_id=7)
    )

    assert sorted(targets) == [9, 11]


def test_returns_empty_list_when_the_source_object_has_no_links(tmp_path):
    d = str(tmp_path)
    write_fixture(d, {relation_key(1, 0, 7, 9): b""})

    targets = asyncio.run(
        ob_aio.read_objectbox_to_many_targets(d, relation_id=1, source_object_id=42)
    )

    assert targets == []
