#ifndef OB_DUMP_INTERNAL_SCHEMA_HPP
#define OB_DUMP_INTERNAL_SCHEMA_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ob_dump_internal {

// Full ObjectBox PropertyType enum, codes taken from the authoritative
// source (official `objectbox` Dart package,
// lib/src/modelinfo/enums.dart) — not guessed. `ToMany` relations aren't in
// this enum at all — they live in a separate LMDB relation-index structure,
// not a table field (see docs/BACKLOG.md "Explicitly out of scope").
enum class PropertyType : int {
    Bool           = 1,
    Byte           = 2,  // int8
    Short          = 3,  // int16
    Char           = 4,  // 16-bit code unit; decoded as a plain integer, not
                         // a JSON string — it's not guaranteed to be a valid
                         // standalone Unicode scalar value (could be half of
                         // a UTF-16 surrogate pair).
    Int            = 5,  // int32
    Long           = 6,  // int64. NOT the same code as `Date` (10) or
                         // `DateNano` (12) — distinct PropertyType codes
                         // that happen to share the int64 wire format.
    Float          = 7,  // 32-bit float
    Double         = 8,
    String         = 9,
    Date           = 10, // int64 ms-since-epoch
    Relation       = 11, // ToOne, stored as int64 fk
    DateNano       = 12, // int64 ns-since-epoch
    Flex           = 13, // dynamic FlexBuffers value, decoded recursively
                         // into the equivalent JSON shape — see fb_decode.cpp
    BoolVector     = 22,
    ByteVector     = 23,
    ShortVector    = 24,
    CharVector     = 25,
    IntVector      = 26,
    LongVector     = 27,
    FloatVector    = 28,
    DoubleVector   = 29,
    StringVector   = 30,
    DateVector     = 31,
    DateNanoVector = 32,
    Unknown        = -1,
};

// ObjectBox's `flags` bitmask on a property (official `objectbox` Dart
// package, lib/src/modelinfo/enums.dart, class OBXPropertyFlag). We only
// need UNSIGNED: it's the one bit that changes how an integer scalar/vector
// must be decoded (the sign, not the width — width still comes from
// PropertyType). Every other flag (INDEXED, UNIQUE, ...) doesn't affect
// decoding and isn't tracked here.
constexpr int kPropertyFlagUnsigned = 8192;

// ObjectBox's `externalType` — an optional annotation layer *on top of* a
// base PropertyType, for interop with an external system that has no
// default ObjectBox type mapping (numeric values from the official
// `objectbox` Dart package, lib/src/modelinfo/enums.dart, class
// OBXExternalPropertyType — start at 100 specifically to never collide
// with PropertyType's own codes). The base type still determines the wire
// encoding (e.g. Uuid is still physically a ByteVector); externalType only
// adds semantic meaning on top. See docs/BACKLOG.md "Schema export" (or
// wherever this is documented) for exactly which of these get a nicer
// decode (Uuid/Int128/Decimal128/Bson as hex/UUID strings) vs. which stay
// out of scope (vectors-of-blobs, Mongo-specific ones).
namespace ExternalPropertyType {
constexpr int Unknown      = 0;
constexpr int Int128       = 100;  // ByteVector, 16 bytes
constexpr int Uuid         = 102;  // ByteVector, 16 bytes
constexpr int Decimal128   = 103;  // ByteVector, 16 bytes (IEEE 754 decimal128)
constexpr int FlexMap      = 107;  // Flex
constexpr int FlexVector   = 108;  // Flex
constexpr int Json         = 109;  // String
constexpr int Bson         = 110;  // ByteVector
constexpr int JavaScript   = 111;  // String
constexpr int JsonToNative = 112;  // String
// Not decoded specially — deliberately, not just unimplemented. Unlike
// every other code above, objectbox.h documents no "Representing type:"
// for these (checked: same gap for MongoIdVector too) — the wire format
// isn't part of the spec we have, so decoding it would mean guessing.
// See docs/BACKLOG.md "Explicitly out of scope" for the full reasoning.
constexpr int Int128Vector = 116;
constexpr int UuidVector   = 118;
}  // namespace ExternalPropertyType

struct PropertyDef {
    int id;   // permanent numeric id from "id:uid" (drives vtable slot = 4 + (id-1)*2)
    std::string name;
    PropertyType type;
    bool isUnsigned  = false;  // kPropertyFlagUnsigned bit of "flags"
    int  externalType = ExternalPropertyType::Unknown;  // "externalType", if present
};

struct EntityDef {
    int entityId;
    std::string name;
    std::vector<PropertyDef> properties;
};

class Schema {
public:
    // Throws std::runtime_error on malformed JSON or missing required fields.
    static Schema parse(const std::string& modelJson);

    const EntityDef* find(int entityId) const;

    // All entities, sorted by entityId — used by the schema-json exporter
    // and the .fbs generator, both of which need deterministic output.
    std::vector<const EntityDef*> all() const;

private:
    std::unordered_map<int, EntityDef> entitiesById_;
};

// Human-readable name for a PropertyType, e.g. for the schema-json export
// and error messages. Returns "Unknown" for PropertyType::Unknown (an
// unrecognized raw type code — see Schema::parse).
const char* toString(PropertyType type);

// Human-readable name for an ExternalPropertyType code, or nullptr for
// ExternalPropertyType::Unknown / an unrecognized code (schema-json omits
// the field in that case rather than printing a meaningless placeholder).
const char* externalTypeName(int code);

}  // namespace ob_dump_internal

#endif  // OB_DUMP_INTERNAL_SCHEMA_HPP
