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
// still-FlexBuffers-encoded bytes ob-dump itself doesn't decode either;
// decoding those bytes further is left to whoever consumes this .fbs.
//
// Throws std::runtime_error on the same conditions Schema::parse() does.
std::string generateFbs(const std::string& modelJson);

}  // namespace ob_dump_internal

#endif  // OB_DUMP_INTERNAL_FBS_GEN_HPP
