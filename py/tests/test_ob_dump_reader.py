import ob_dump_reader as ob

from _fixture import data_key, relation_key, schema_key, write_fixture


def test_extracts_data_records_and_ignores_non_data_keys(tmp_path):
    d = str(tmp_path)
    write_fixture(
        d,
        {
            schema_key(1): b"",  # schema entry — must be ignored
            data_key(1, 7): b"hello",
            data_key(1, 8): b"world",
            relation_key(1, 0, 7, 9): b"",  # relation entry — must be ignored
        },
    )

    records = []
    ob.read_objectbox_records(d, records.append)

    records.sort(key=lambda r: r.object_id)
    assert [(r.entity_id, r.object_id, r.data) for r in records] == [
        (1, 7, b"hello"),
        (1, 8, b"world"),
    ]


def test_reads_the_directory_directly_in_place_no_copy(tmp_path):
    d = str(tmp_path)
    write_fixture(d, {data_key(1, 1): b"x"})

    before = sorted(p.name for p in tmp_path.iterdir())
    ob.read_objectbox_records(d, lambda _r: None)
    after = sorted(p.name for p in tmp_path.iterdir())

    assert before == after


def test_resolves_forward_direction_to_many_links_ignoring_backward_ones(tmp_path):
    d = str(tmp_path)
    write_fixture(
        d,
        {
            relation_key(1, 0, 7, 9): b"",
            relation_key(1, 0, 7, 11): b"",
            relation_key(1, 2, 9, 7): b"",  # backward index — must be ignored
            relation_key(2, 0, 7, 99): b"",  # different relation id — must be ignored
        },
    )

    targets = ob.read_objectbox_to_many_targets(d, relation_id=1, source_object_id=7)

    assert sorted(targets) == [9, 11]


def test_returns_empty_list_when_the_source_object_has_no_links(tmp_path):
    d = str(tmp_path)
    write_fixture(d, {relation_key(1, 0, 7, 9): b""})

    targets = ob.read_objectbox_to_many_targets(d, relation_id=1, source_object_id=42)

    assert targets == []
