#ifndef OB_DUMP_INTERNAL_SCHEMA_HPP
#define OB_DUMP_INTERNAL_SCHEMA_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ob_dump_internal {

// Full ObjectBox PropertyType enum, codes taken from the authoritative
// source (official `objectbox` Dart package,
// lib/src/modelinfo/enums.dart) — not guessed. `Flex` is intentionally
// listed but not decoded (see docs/BACKLOG.md "Explicitly out of scope":
// it's a nested FlexBuffers blob, a different encoding entirely, deferred
// on purpose). `ToMany` relations aren't in this enum at all — they live in
// a separate LMDB relation-index structure, not a table field.
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
    Flex           = 13, // NOT decoded — see docs/BACKLOG.md
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

struct PropertyDef {
    int id;   // permanent numeric id from "id:uid" (drives vtable slot = 4 + (id-1)*2)
    std::string name;
    PropertyType type;
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

private:
    std::unordered_map<int, EntityDef> entitiesById_;
};

}  // namespace ob_dump_internal

#endif  // OB_DUMP_INTERNAL_SCHEMA_HPP
