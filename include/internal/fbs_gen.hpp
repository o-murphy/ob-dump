#ifndef OB_DUMP_INTERNAL_FBS_GEN_HPP
#define OB_DUMP_INTERNAL_FBS_GEN_HPP

#include <string>

namespace ob_dump_internal {

// Generates a valid FlatBuffers IDL (.fbs) text from an ObjectBox
// objectbox-model.json, so any language's `flatc` can produce a typed
// reader for the raw table bytes ob-dump reads dynamically — see
// docs/BACKLOG.md for what this does and doesn't replace (it does not
// replace LMDB access or the ObjectBox key-format parsing, only the
// per-record FlatBuffers decode).
//
// Field declaration order matches property id order, because that's what
// determines each field's vtable slot (see docs/BACKLOG.md). Property ids
// are permanent and can have gaps (retired properties): each gap is filled
// with a `(deprecated)` placeholder field so slot numbering stays aligned —
// flatc still counts a deprecated field's declaration towards the vtable
// slot index, it just skips generating an accessor for it. The retired
// property's original type isn't recoverable from model.json (only its uid
// is kept, in `retiredPropertyUids` — not its type), so the placeholder's
// own type is arbitrary (`ubyte`) and irrelevant: deprecated fields never
// get an accessor regardless of declared type.
//
// `Flex` properties (PropertyType 13) are emitted as `[ubyte]` — the raw,
// still-FlexBuffers-encoded bytes. ob_dump's own `--json`/`ob_dump()` *does*
// decode these (fb_decode.cpp recurses into the equivalent JSON shape using
// the same flexbuffers.h this project already depends on for FlatBuffers
// itself), but that's a separate code path from this .fbs generator: a
// `flatc`-generated reader in another language only gets the raw bytes back
// and would need its own FlexBuffers decode to go further, same as before.
//
// Throws std::runtime_error on the same conditions Schema::parse() does.
std::string generateFbs(const std::string& modelJson);

}  // namespace ob_dump_internal

#endif  // OB_DUMP_INTERNAL_FBS_GEN_HPP
