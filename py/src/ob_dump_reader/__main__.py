import argparse

import ob_dump_reader as ob


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="ob_dump_reader",
        description=(
            "Walk an ObjectBox LMDB store and print each record's entity id, "
            "object id, and raw FlatBuffers data length. Decoding the data "
            "itself needs a flatc-generated schema — see the package README."
        ),
    )
    parser.add_argument(
        "objectbox_dir",
        help="path to the ObjectBox data directory (containing data.mdb)",
    )
    args = parser.parse_args()

    def on_record(record: ob.ObRecord) -> None:
        print(
            f"entity={record.entity_id} object={record.object_id} bytes={len(record.data)}"
        )

    ob.read_objectbox_records(args.objectbox_dir, on_record)


if __name__ == "__main__":
    main()
