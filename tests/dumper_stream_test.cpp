// Builds a small synthetic LMDB store (raw mdb_put calls, ObjectBox's exact
// key format) and exercises dumpStreaming(): correct per-record callback
// data, and early-stop when the callback returns false.
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <sstream>
#include <vector>

#include <lmdb.h>

#include "internal/dumper.hpp"

namespace {

// mkdtemp() is POSIX/BSD-only (missing on Windows, and unreliable via
// <cstdlib> on macOS's libc++ — both broke CI). std::filesystem has no
// built-in "make a unique temp dir", so build one: portable across every
// platform this project targets.
std::filesystem::path makeTempDir(const std::string& prefix) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned> dist(0, 0xFFFFFFu);
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::ostringstream name;
        name << prefix << std::hex << dist(gen);
        auto path = std::filesystem::temp_directory_path() / name.str();
        std::error_code ec;
        if (std::filesystem::create_directory(path, ec)) {
            return path;
        }
    }
    throw std::runtime_error("failed to create a unique temp directory");
}

std::array<uint8_t, 8> dataKey(int entityId, uint32_t objectId) {
    std::array<uint8_t, 8> key{};
    key[0] = 0x18;  // type: object data
    key[3] = static_cast<uint8_t>(entityId * 4);
    key[4] = static_cast<uint8_t>(objectId >> 24);
    key[5] = static_cast<uint8_t>(objectId >> 16);
    key[6] = static_cast<uint8_t>(objectId >> 8);
    key[7] = static_cast<uint8_t>(objectId);
    return key;
}

// ToMany relation link key — see docs/BACKLOG.md "Explicitly out of scope"
// -> ToMany for how this 12-byte format (type 0x08, empty value) was
// determined against a real ObjectBox-Dart project.
std::array<uint8_t, 12> relationKey(int relationId, int direction, uint32_t sourceId, uint32_t targetId) {
    std::array<uint8_t, 12> key{};
    key[0] = 0x08;  // type: relation link
    key[3] = static_cast<uint8_t>((relationId << 2) | direction);
    key[4] = static_cast<uint8_t>(sourceId >> 24);
    key[5] = static_cast<uint8_t>(sourceId >> 16);
    key[6] = static_cast<uint8_t>(sourceId >> 8);
    key[7] = static_cast<uint8_t>(sourceId);
    key[8] = static_cast<uint8_t>(targetId >> 24);
    key[9] = static_cast<uint8_t>(targetId >> 16);
    key[10] = static_cast<uint8_t>(targetId >> 8);
    key[11] = static_cast<uint8_t>(targetId);
    return key;
}

void check(int rc, const char* what) {
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", what, mdb_strerror(rc));
        std::abort();
    }
}

// Writes a tiny fixture DB at `dir`: entity 1 with two objects (a Bool and
// an Int field, matching a trivial made-up schema), one entity 2 object.
void writeFixture(const std::string& dir) {
    MDB_env* env = nullptr;
    check(mdb_env_create(&env), "mdb_env_create");
    check(mdb_env_set_maxdbs(env, 1), "mdb_env_set_maxdbs");
    check(mdb_env_set_mapsize(env, 16 * 1024 * 1024), "mdb_env_set_mapsize");
    check(mdb_env_open(env, dir.c_str(), 0, 0644), "mdb_env_open");

    MDB_txn* txn = nullptr;
    check(mdb_txn_begin(env, nullptr, 0, &txn), "mdb_txn_begin");
    MDB_dbi dbi = 0;
    check(mdb_dbi_open(txn, nullptr, 0, &dbi), "mdb_dbi_open");

    // Bare-minimum valid FlatBuffers tables: root offset(4) + vtable(4/6/8..).
    // We don't need real field contents here — dumpStreaming only needs
    // *some* entity in the schema and *some* decodable (possibly empty)
    // table per record; fb_decode_test.cpp already covers field decoding
    // in depth.
    // Generic on the key type: both the 8-byte object-data/schema keys and
    // the 12-byte relation-link keys go through this same helper.
    auto put = [&](const auto& key, const std::vector<uint8_t>& val) {
        MDB_val k{key.size(), const_cast<uint8_t*>(key.data())};
        MDB_val v{val.size(), const_cast<uint8_t*>(val.data())};
        check(mdb_put(txn, dbi, &k, &v, 0), "mdb_put");
    };

    // Minimal empty FlatBuffers table, vtable placed before the table (so
    // its soffset is a plain positive value, table_start - vtable_start):
    //   [0..3]  root uoffset_t = 8          (table starts at byte 8)
    //   [4..5]  vtable[0] vtsize = 4        (2-entry header, no fields)
    //   [6..7]  vtable[1] objsize = 4       (table is just its own soffset)
    //   [8..11] table soffset_t = 4         (8 - 4 = vtable_start)
    const std::vector<uint8_t> emptyTable = {
        8, 0, 0, 0,
        4, 0, 4, 0,
        4, 0, 0, 0,
    };

    put(dataKey(1, 1), emptyTable);
    put(dataKey(1, 2), emptyTable);
    put(dataKey(2, 1), emptyTable);

    // Weapon#1 --relation 1 (forward)--> Ammo#1, Ammo#2. Also write the
    // auto-maintained backward links (Ammo#1/#2 -> Weapon#1) to confirm
    // toManyTargets's forward-only prefix match correctly ignores them
    // rather than accidentally picking them up too.
    const std::vector<uint8_t> empty;
    put(relationKey(1, /*forward=*/0, 1, 1), empty);
    put(relationKey(1, /*forward=*/0, 1, 2), empty);
    put(relationKey(1, /*backward=*/2, 1, 1), empty);
    put(relationKey(1, /*backward=*/2, 2, 1), empty);

    check(mdb_txn_commit(txn), "mdb_txn_commit");
    mdb_env_close(env);
}

const char* kModelJson = R"({
  "entities": [
    {"id": "1:1", "name": "Ammo", "properties": []},
    {
      "id": "2:2",
      "name": "Weapon",
      "properties": [],
      "relations": [
        {"id": "1:99", "name": "compatibleAmmo", "targetId": "1:1"}
      ]
    }
  ]
})";

struct Seen {
    std::string entityName;
    uint32_t objectId;
    std::string fieldsJson;
};

}  // namespace

int main() {
    std::filesystem::path dir = makeTempDir("ob_dump_stream_test_");
    writeFixture(dir.string());

    // --- all records, in order ---
    {
        std::vector<Seen> seen;
        ob_dump_internal::dumpStreaming(dir.string(), kModelJson,
            [&](const std::string& name, uint32_t objId, const std::string& json) {
                seen.push_back({name, objId, json});
                return true;
            });

        assert(seen.size() == 3);
        assert(seen[0].entityName == "Ammo");
        assert(seen[0].objectId == 1);
        assert(seen[0].fieldsJson.find("\"id\":1") != std::string::npos);
        assert(seen[1].entityName == "Ammo");
        assert(seen[1].objectId == 2);
        assert(seen[2].entityName == "Weapon");
        assert(seen[2].objectId == 1);
        // ToMany relation ("compatibleAmmo", forward direction) resolved to
        // Weapon#1's two linked Ammo ids — the backward-direction entries
        // written into the same fixture (Ammo -> Weapon) must not leak in.
        assert(seen[2].fieldsJson.find("\"compatibleAmmo\":[1,2]") != std::string::npos);
        std::puts("dumpStreaming visits all records in order: OK");
        std::puts("dumpStreaming resolves ToMany relations: OK");
    }

    // --- early stop ---
    {
        int count = 0;
        ob_dump_internal::dumpStreaming(dir.string(), kModelJson,
            [&](const std::string&, uint32_t, const std::string&) {
                ++count;
                return false;  // stop after the first record
            });
        assert(count == 1);
        std::puts("dumpStreaming stops early when callback returns false: OK");
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);  // best-effort cleanup

    std::puts("dumper_stream_test: OK");
    return 0;
}
