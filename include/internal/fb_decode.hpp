#ifndef OB_DUMP_INTERNAL_FB_DECODE_HPP
#define OB_DUMP_INTERNAL_FB_DECODE_HPP

#include <cstddef>
#include <cstdint>

#include <nlohmann/json_fwd.hpp>

#include "internal/schema.hpp"

namespace ob_dump_internal {

// property_id -> vtable slot (flatbuffers::voffset_t, which is uint16_t —
// returned as the plain typedef here so this header doesn't need to pull in
// <flatbuffers/flatbuffers.h>). Exposed (not file-local to fb_decode.cpp) so
// tests can build FlatBuffers fixtures at the exact same slots decodeObject()
// will look for, instead of keeping a second copy of this formula that could
// drift. See docs/BACKLOG.md for why this formula is correct.
uint16_t slotFor(int propertyId);

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
