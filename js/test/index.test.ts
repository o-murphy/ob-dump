import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import {
  readObjectboxRecords,
  readObjectboxToManyTargets,
  type ObRecord,
} from "../src/index.js";
import { dataKey, relationKey, schemaKey, writeFixture } from "./fixture.js";

let dir: string;

beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), "ob-dump-js-"));
});

afterEach(() => {
  rmSync(dir, { recursive: true, force: true });
});

describe("readObjectboxRecords", () => {
  it("decodes entity id and object id from data keys, skipping other key types", () => {
    writeFixture(dir, [
      [dataKey(1, 100), Buffer.from("payload-1-100")],
      [dataKey(2, 7), Buffer.from("payload-2-7")],
      [schemaKey(1), Buffer.alloc(0)],
      [relationKey(1, 0, 100, 200), Buffer.alloc(0)],
    ]);

    const records: ObRecord[] = [];
    readObjectboxRecords(dir, (record) => records.push(record));

    records.sort((a, b) => a.entityId - b.entityId);
    expect(records).toHaveLength(2);
    expect(records[0]).toMatchObject({ entityId: 1, objectId: 100 });
    expect(records[0].data.toString()).toBe("payload-1-100");
    expect(records[1]).toMatchObject({ entityId: 2, objectId: 7 });
    expect(records[1].data.toString()).toBe("payload-2-7");
  });

  it("calls onRecord zero times for an empty store", () => {
    writeFixture(dir, []);
    const records: ObRecord[] = [];
    readObjectboxRecords(dir, (record) => records.push(record));
    expect(records).toHaveLength(0);
  });
});

describe("readObjectboxToManyTargets", () => {
  it("resolves only forward-direction links for the given relation/source", () => {
    writeFixture(dir, [
      [relationKey(1, 0, 100, 200), Buffer.alloc(0)],
      [relationKey(1, 0, 100, 201), Buffer.alloc(0)],
      [relationKey(1, 2, 100, 999), Buffer.alloc(0)], // backward index, ignored
      [relationKey(2, 0, 100, 300), Buffer.alloc(0)], // different relation, ignored
      [relationKey(1, 0, 101, 400), Buffer.alloc(0)], // different source, ignored
    ]);

    const targets = readObjectboxToManyTargets(dir, 1, 100);
    expect(targets.sort()).toEqual([200, 201]);
  });

  it("returns an empty list when the source object has no links", () => {
    writeFixture(dir, [[relationKey(1, 0, 100, 200), Buffer.alloc(0)]]);
    expect(readObjectboxToManyTargets(dir, 1, 999)).toEqual([]);
  });
});
