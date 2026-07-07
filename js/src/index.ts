import { open } from "lmdb";

import {
  KEY_TYPE_DATA,
  KEY_TYPE_RELATION,
  RELATION_DIRECTION_FORWARD,
} from "./constants.js";
import { readUint32be } from "./decodeHelpers.js";

export interface ObRecord {
  entityId: number;
  objectId: number;
  data: Buffer;
}

// readOnly/keyEncoding/getRange(start:) option names are per lmdb-js's
// published API — authored without network access to the registry in this
// sandbox, so verify against the installed package's own type defs
// (`yarn install` then check `node_modules/lmdb/index.d.ts`) before relying
// on this in production.
function openStore(objectboxDir: string) {
  return open({
    path: objectboxDir,
    encoding: "binary",
    keyEncoding: "binary",
    readOnly: true,
    mapSize: 512 * 1024 * 1024,
  });
}

export function readObjectboxRecords(
  objectboxDir: string,
  onRecord: (record: ObRecord) => void,
): void {
  const db = openStore(objectboxDir);
  try {
    for (const { key, value } of db.getRange({})) {
      const k = key as Buffer;
      if (k.length === 8 && k[0] === KEY_TYPE_DATA) {
        onRecord({
          entityId: Math.floor(k[3] / 4),
          objectId: readUint32be(k, 4),
          data: value as Buffer,
        });
      }
    }
  } finally {
    db.close();
  }
}

/**
 * Resolves a `ToMany` relation's forward-direction target ids.
 *
 * Not part of the FlatBuffers table at all — a separate LMDB key range,
 * 12 bytes: type(0x08) + 2 reserved bytes + ((relationId << 2) |
 * direction) + sourceId (u32 BE) + targetId (u32 BE), value always empty.
 * `direction` 0 is the declared forward direction; 2 is an auto-maintained
 * reverse index for ObjectBox's own query engine, not needed for a
 * one-directional dump.
 */
export function readObjectboxToManyTargets(
  objectboxDir: string,
  relationId: number,
  sourceObjectId: number,
): number[] {
  const prefix = Buffer.alloc(8);
  prefix[0] = KEY_TYPE_RELATION;
  prefix[3] = (relationId << 2) | RELATION_DIRECTION_FORWARD;
  prefix.writeUInt32BE(sourceObjectId, 4);

  const targets: number[] = [];
  const db = openStore(objectboxDir);
  try {
    for (const { key } of db.getRange({ start: prefix })) {
      const k = key as Buffer;
      if (!k.subarray(0, 8).equals(prefix)) break;
      targets.push(readUint32be(k, 8));
    }
  } finally {
    db.close();
  }
  return targets;
}
