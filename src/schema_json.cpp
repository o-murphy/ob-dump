#include "internal/schema_json.hpp"

#include <nlohmann/json.hpp>

#include "internal/fb_decode.hpp"  // slotFor
#include "internal/schema.hpp"

namespace ob_dump_internal {

std::string schemaToJson(const std::string& modelJson) {
    Schema schema = Schema::parse(modelJson);

    auto entities = nlohmann::json::array();
    for (const EntityDef* entity : schema.all()) {
        auto properties = nlohmann::json::array();
        for (const auto& prop : entity->properties) {
            nlohmann::json p = {
                {"id", prop.id},
                {"name", prop.name},
                {"type", toString(prop.type)},
                {"vtableSlot", slotFor(prop.id)},
            };
            // Only shown when notable, to keep this listing minimal — see
            // schema_json.hpp: most properties are signed with no external
            // type, and repeating "unsigned": false on every property would
            // be noise.
            if (prop.isUnsigned) p["unsigned"] = true;
            if (const char* ext = externalTypeName(prop.externalType)) p["externalType"] = ext;
            properties.push_back(std::move(p));
        }
        auto relations = nlohmann::json::array();
        for (const auto& rel : entity->relations) {
            relations.push_back({
                {"id", rel.id},
                {"name", rel.name},
                {"targetEntityId", rel.targetEntityId},
            });
        }

        nlohmann::json entityJson = {
            {"entityId", entity->entityId},
            {"name", entity->name},
            {"properties", std::move(properties)},
        };
        // Omitted entirely when empty — most entities have no ToMany
        // relation, and repeating "relations": [] everywhere is noise (same
        // reasoning as omitting "unsigned"/"externalType" above).
        if (!relations.empty()) entityJson["relations"] = std::move(relations);
        entities.push_back(std::move(entityJson));
    }

    nlohmann::json out = {{"entities", std::move(entities)}};
    return out.dump(2);
}

}  // namespace ob_dump_internal
