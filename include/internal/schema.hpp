#ifndef OB_DUMP_INTERNAL_SCHEMA_HPP
#define OB_DUMP_INTERNAL_SCHEMA_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ob_dump_internal {

// ObjectBox PropertyType codes actually observed in a real model.json.
// See docs/BACKLOG.md "Scope" for what's implemented vs. explicitly deferred.
enum class PropertyType : int {
    Bool          = 1,
    Long          = 6,  // int64. NOT the same code as ObjectBox's dedicated
                        // `Date` type (10, also int64 on the wire) — that
                        // type isn't used anywhere in ebalistyka's current
                        // schema and isn't implemented here; see
                        // docs/BACKLOG.md "Explicitly out of scope".
    Double        = 8,
    String        = 9,
    Relation      = 11, // ToOne, stored as int64 fk
    DoubleVector  = 29,
    StringVector  = 30,
    Unknown       = -1,
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
