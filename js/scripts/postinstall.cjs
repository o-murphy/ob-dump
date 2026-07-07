#!/usr/bin/env node
// ObjectBox (and this repo's own dart/py packages) use LMDB's legacy data
// format v1; lmdb-js's prebuilt binaries default to the newer data format
// v2 instead — opening a v1-format file with a v2-expecting build is
// undefined behavior, confirmed empirically: a real ObjectBox `data.mdb`
// segfaults the whole process outright (not a catchable JS exception), not
// just a wrong-result bug. Force a from-source rebuild with the legacy v1
// data format on every install so this package actually works against
// real ObjectBox databases out of the box, instead of leaving that as a
// manual step nobody would think to look for until production crashes.
"use strict";

const { spawnSync } = require("node:child_process");

const result = spawnSync(
  process.platform === "win32" ? "npm.cmd" : "npm",
  ["rebuild", "lmdb", "--build-from-source"],
  {
    stdio: "inherit",
    env: { ...process.env, LMDB_DATA_V1: "true" },
    // Required on Windows: npm.cmd is a shell script wrapper, not a real
    // executable — spawnSync fails with EINVAL trying to run it directly
    // without a shell (confirmed via a real windows-latest CI run, not
    // assumed). Harmless on POSIX, where npm is a real executable either way.
    shell: true,
  },
);

if (result.error) {
  console.error(
    "ob-dump-reader: failed to rebuild lmdb with the legacy LMDB data " +
      "format (needed to read real ObjectBox databases) — a C/C++ build " +
      "toolchain (python3, make, a C compiler) is required. See js/README.md.",
  );
  console.error(result.error);
}
process.exit(result.status ?? 1);
