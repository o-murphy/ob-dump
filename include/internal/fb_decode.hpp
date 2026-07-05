#ifndef OB_DUMP_INTERNAL_FB_DECODE_HPP
#define OB_DUMP_INTERNAL_FB_DECODE_HPP

#include <cstddef>
#include <cstdint>

#include <nlohmann/json_fwd.hpp>

#include "internal/schema.hpp"

namespace ob_dump_internal {

// Decodes one object's raw FlatBuffers table bytes into a JSON object keyed
// by property name (does not include "id" — the caller adds that from the
// LMDB key). Fields whose vtable slot is absent or out of range (e.g. a
// property added/retired after this particular record was written) are
// simply omitted — this is normal FlatBuffers forward/backward
// compatibility, not corruption.
//
// Bounds-checked via flatbuffers::Verifier (no schema/.fbs required — the
// property_id -> vtable slot mapping is computed at runtime from `entity`).
// Throws std::runtime_error if `data` fails verification (a genuinely
// corrupted/truncated record).
nlohmann::json decodeObject(const uint8_t* data, size_t size, const EntityDef& entity);

}  // namespace ob_dump_internal

#endif  // OB_DUMP_INTERNAL_FB_DECODE_HPP
