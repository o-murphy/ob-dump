#ifndef OB_DUMP_INTERNAL_DUMPER_HPP
#define OB_DUMP_INTERNAL_DUMPER_HPP

#include <string>

namespace ob_dump_internal {

// Reads the ObjectBox LMDB store at `mdbPath` (see LmdbReader for accepted
// path forms) and returns a JSON dump shaped as
// `{ "<EntityName>": [ {...fields..., "id": <object_id>}, ... ], ... }`,
// using `modelJson` (the full contents of objectbox-model.json) as the
// schema. Entities with zero objects are omitted entirely, matching
// tools/ob_migration_poc/bin/export_json.dart in the ebalistyka repo this
// was ported from.
//
// Throws std::runtime_error on any failure (bad schema, bad LMDB store, or
// a corrupted record caught by fb_decode's Verifier checks).
std::string dumpToJson(const std::string& mdbPath, const std::string& modelJson);

}  // namespace ob_dump_internal

#endif  // OB_DUMP_INTERNAL_DUMPER_HPP
