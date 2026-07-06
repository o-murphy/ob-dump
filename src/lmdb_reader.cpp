#include "internal/lmdb_reader.hpp"

#include <lmdb.h>

#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace ob_dump_internal {

namespace {

[[maybe_unused]] constexpr uint8_t kKeyTypeSchema   = 0x00;
constexpr uint8_t kKeyTypeData     = 0x18;
[[maybe_unused]] constexpr uint8_t kKeyTypeIndex    = 0x20;  // unverified — see lmdb_reader.hpp
constexpr uint8_t kKeyTypeRelation = 0x08;
constexpr uint8_t kRelationDirectionForward = 0;

// std::filesystem rather than POSIX stat()/S_ISREG: those aren't available
// as-is on MSVC, whereas <filesystem> is portable across every platform
// this project targets (C++17, already our language standard).
bool isRegularFile(const std::string& path) {
    std::error_code ec;
    bool result = std::filesystem::is_regular_file(path, ec);
    if (ec) {
        throw std::runtime_error("cannot stat path: " + path + ": " + ec.message());
    }
    return result;
}

void check(int rc, const char* what) {
    if (rc != 0) {
        throw std::runtime_error(std::string(what) + ": " + mdb_strerror(rc));
    }
}

uint32_t readUint32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
}

void writeUint32BE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

}  // namespace

LmdbReader::LmdbReader(const std::string& path) {
    check(mdb_env_create(&env_), "mdb_env_create");

    // Root/unnamed db only — doesn't count against max_dbs, but keep a small
    // headroom in case a future ObjectBox version adds named sub-dbs.
    check(mdb_env_set_maxdbs(env_, 1), "mdb_env_set_maxdbs");
    // Virtual address space reservation only (mmap), not real memory —
    // generous headroom is free. 4 GiB comfortably covers any realistic
    // ObjectBox store.
    check(mdb_env_set_mapsize(env_, static_cast<size_t>(4) << 30), "mdb_env_set_mapsize");

    unsigned int envFlags = MDB_RDONLY;
    if (isRegularFile(path)) {
        envFlags |= MDB_NOSUBDIR;
    }

    int rc = mdb_env_open(env_, path.c_str(), envFlags, 0644);
    if (rc != 0) {
        mdb_env_close(env_);
        env_ = nullptr;
        throw std::runtime_error(std::string("mdb_env_open(") + path + "): " + mdb_strerror(rc));
    }

    rc = mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn_);
    if (rc != 0) {
        mdb_env_close(env_);
        env_ = nullptr;
        throw std::runtime_error(std::string("mdb_txn_begin: ") + mdb_strerror(rc));
    }

    MDB_dbi dbi = 0;
    rc = mdb_dbi_open(txn_, nullptr, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn_);
        mdb_env_close(env_);
        txn_ = nullptr;
        env_ = nullptr;
        throw std::runtime_error(std::string("mdb_dbi_open: ") + mdb_strerror(rc));
    }
    dbi_ = dbi;
}

LmdbReader::~LmdbReader() {
    if (txn_ != nullptr) {
        mdb_txn_abort(txn_);
    }
    if (env_ != nullptr) {
        mdb_env_close(env_);
    }
}

void LmdbReader::forEachObject(const std::function<bool(const ObjectRecord&)>& cb) const {
    MDB_cursor* cursor = nullptr;
    check(mdb_cursor_open(txn_, static_cast<MDB_dbi>(dbi_), &cursor), "mdb_cursor_open");

    MDB_val key{};
    MDB_val val{};
    int rc = mdb_cursor_get(cursor, &key, &val, MDB_FIRST);
    while (rc == 0) {
        const auto* keyBytes = static_cast<const uint8_t*>(key.mv_data);

        if (key.mv_size == 8 && keyBytes[0] == kKeyTypeData) {
            ObjectRecord rec{};
            rec.entityId = keyBytes[3] / 4;
            rec.objectId = readUint32BE(keyBytes + 4);
            rec.data     = static_cast<const uint8_t*>(val.mv_data);
            rec.size     = val.mv_size;
            if (!cb(rec)) break;  // early stop requested
        }
        // kKeyTypeSchema / kKeyTypeIndex / anything else: not object data,
        // nothing for this layer to do with it.

        rc = mdb_cursor_get(cursor, &key, &val, MDB_NEXT);
    }
    mdb_cursor_close(cursor);

    if (rc != MDB_NOTFOUND) {
        check(rc, "mdb_cursor_get");
    }
}

std::vector<uint32_t> LmdbReader::toManyTargets(int relationId, uint32_t sourceObjectId) const {
    std::vector<uint32_t> targets;

    // All links for this (relation, direction, source) share the same
    // 8-byte prefix and vary only in the trailing target id — since LMDB
    // sorts keys byte-for-byte, they're contiguous. Seek to the smallest
    // possible key with that prefix (target id 0), then walk forward while
    // the prefix still matches.
    uint8_t prefix[8];
    prefix[0] = kKeyTypeRelation;
    prefix[1] = 0x00;
    prefix[2] = 0x00;
    prefix[3] = static_cast<uint8_t>((relationId << 2) | kRelationDirectionForward);
    writeUint32BE(prefix + 4, sourceObjectId);

    uint8_t searchKey[12];
    std::memcpy(searchKey, prefix, sizeof(prefix));
    writeUint32BE(searchKey + 8, 0);

    MDB_cursor* cursor = nullptr;
    check(mdb_cursor_open(txn_, static_cast<MDB_dbi>(dbi_), &cursor), "mdb_cursor_open");

    MDB_val key{};
    key.mv_size = sizeof(searchKey);
    key.mv_data = searchKey;
    MDB_val val{};

    int rc = mdb_cursor_get(cursor, &key, &val, MDB_SET_RANGE);
    while (rc == 0) {
        const auto* keyBytes = static_cast<const uint8_t*>(key.mv_data);
        if (key.mv_size != sizeof(searchKey) || std::memcmp(keyBytes, prefix, sizeof(prefix)) != 0) {
            break;  // past the last link for this (relation, source)
        }
        targets.push_back(readUint32BE(keyBytes + 8));
        rc = mdb_cursor_get(cursor, &key, &val, MDB_NEXT);
    }
    mdb_cursor_close(cursor);

    if (rc != 0 && rc != MDB_NOTFOUND) {
        check(rc, "mdb_cursor_get");
    }

    return targets;
}

}  // namespace ob_dump_internal
