#ifndef OB_DUMP_INTERNAL_LMDB_READER_HPP
#define OB_DUMP_INTERNAL_LMDB_READER_HPP

#include <cstdint>
#include <functional>
#include <string>

// Forward-declare LMDB's opaque types so this header doesn't force every
// includer to pull in <lmdb.h>.
struct MDB_env;
struct MDB_txn;

namespace ob_dump_internal {

// One ObjectBox object-data record found in the root/unnamed LMDB db.
// `data`/`size` point directly into the LMDB read transaction's mmap — no
// copy is made. Valid only for the lifetime of the owning LmdbReader's
// current read transaction.
struct ObjectRecord {
    int      entityId;
    uint32_t objectId;
    const uint8_t* data;
    size_t   size;
};

// Opens an ObjectBox LMDB store read-only and walks its root database.
//
// Key format (8 bytes, root/unnamed db only — ObjectBox never uses named
// sub-databases): [type:1][0x00][0x00][entity_id*4:1][object_id:u32 BE].
// type: 0x00 = schema entry, 0x18 = object data, 0x20 = index entry.
// See docs/BACKLOG.md for how this was determined.
class LmdbReader {
public:
    // `path` may be either the directory containing data.mdb+lock.mdb, or
    // the data.mdb file itself (opened with MDB_NOSUBDIR in that case).
    // Throws std::runtime_error on any LMDB error.
    explicit LmdbReader(const std::string& path);
    ~LmdbReader();

    LmdbReader(const LmdbReader&)            = delete;
    LmdbReader& operator=(const LmdbReader&) = delete;

    // Invokes cb once per object-data record (key type 0x18). Records with
    // no matching entity in the schema are silently skipped by the caller,
    // not here — this layer only parses the key format, it knows nothing
    // about the schema.
    //
    // cb returns true to keep iterating, false to stop early — used by
    // streaming consumers (see dumper.hpp's dumpStreaming) that may want to
    // bail out before walking a large database to completion.
    void forEachObject(const std::function<bool(const ObjectRecord&)>& cb) const;

private:
    MDB_env* env_ = nullptr;
    MDB_txn* txn_ = nullptr;
    unsigned int dbi_ = 0;
};

}  // namespace ob_dump_internal

#endif  // OB_DUMP_INTERNAL_LMDB_READER_HPP
