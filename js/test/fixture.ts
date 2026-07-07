// Shared LMDB fixture-writing helper for tests — uses `lmdb` directly,
// independent of the reader code under test (mirrors py/tests/_fixture.py
// and dart/test/test_fixture.dart).
import { open } from "lmdb";

export function writeFixture(
  objectboxDir: string,
  entries: [Buffer, Buffer][],
): void {
  const db = open({
    path: objectboxDir,
    encoding: "binary",
    keyEncoding: "binary",
    mapSize: 10 * 1024 * 1024,
    // See src/index.ts's openStore for why this can't be left to lmdb-js's
    // own path-based guess.
    noSubdir: false,
  });
  for (const [key, value] of entries) {
    db.putSync(key, value);
  }
  db.close();
}

export function dataKey(entityId: number, objectId: number): Buffer {
  const key = Buffer.alloc(8);
  key[0] = 0x18; // type: object data
  key[3] = entityId * 4;
  key.writeUInt32BE(objectId, 4);
  return key;
}

export function relationKey(
  relationId: number,
  direction: number,
  sourceId: number,
  targetId: number,
): Buffer {
  const key = Buffer.alloc(12);
  key[0] = 0x08; // type: relation link
  key[3] = (relationId << 2) | direction;
  key.writeUInt32BE(sourceId, 4);
  key.writeUInt32BE(targetId, 8);
  return key;
}

export function schemaKey(entityId: number): Buffer {
  const key = Buffer.alloc(8);
  key[0] = 0x00; // type: schema entry
  key[7] = entityId;
  return key;
}
