#ifndef OB_DUMP_INTERNAL_DUMPER_HPP
#define OB_DUMP_INTERNAL_DUMPER_HPP

#include <cstdint>
#include <functional>
#include <string>

namespace ob_dump_internal {

// Reads the ObjectBox LMDB store at `mdbPath` (see LmdbReader for accepted
// path forms) and returns a JSON dump shaped as
// `{ "<EntityName>": [ {...fields..., "id": <object_id>}, ... ], ... }`,
// using `modelJson` (the full contents of objectbox-model.json) as the
// schema. Entities with zero objects are omitted entirely, matching the
// reference Dart migration PoC this project ports from.
//
// Builds the entire result in memory before returning it — see
// dumpStreaming for a variant that doesn't, for large databases.
//
// Throws std::runtime_error on any failure (bad schema, bad LMDB store, or
// a corrupted record caught by fb_decode's Verifier checks).
std::string dumpToJson(const std::string& mdbPath, const std::string& modelJson);

// Called once per decoded record: entity name, object id, and a JSON object
// of that record's fields (same shape as one element of dumpToJson's
// per-entity arrays, including "id"). Return true to keep iterating, false
// to stop early (not an error).
using RecordCallback =
    std::function<bool(const std::string& entityName, uint32_t objectId, const std::string& fieldsJson)>;

// Streaming variant of dumpToJson: invokes `callback` once per decoded
// record instead of accumulating the whole database as one in-memory JSON
// tree — use this for large databases where dumpToJson's memory footprint
// (proportional to total data size) isn't acceptable. LMDB access itself is
// already lazy/paged regardless of which of these two is used; this only
// changes whether *decoded* records get accumulated.
//
// Same throwing behavior as dumpToJson.
void dumpStreaming(const std::string& mdbPath, const std::string& modelJson,
                   const RecordCallback& callback);

}  // namespace ob_dump_internal

#endif  // OB_DUMP_INTERNAL_DUMPER_HPP
