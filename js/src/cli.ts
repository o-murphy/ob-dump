#!/usr/bin/env node
import { readObjectboxRecords, type ObRecord } from "./index.js";

const objectboxDir = process.argv[2];
if (!objectboxDir) {
  console.error(
    "usage: ob-dump-reader <objectbox_dir>\n\n" +
      "Walks an ObjectBox LMDB store and prints each record's entity id, " +
      "object id, and raw FlatBuffers data length. Decoding the data itself " +
      "needs a flatc-generated schema — see the package README.",
  );
  process.exit(1);
}

readObjectboxRecords(objectboxDir, (record: ObRecord) => {
  console.log(
    `entity=${record.entityId} object=${record.objectId} bytes=${record.data.length}`,
  );
});
