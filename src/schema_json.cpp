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
            properties.push_back({
                {"id", prop.id},
                {"name", prop.name},
                {"type", toString(prop.type)},
                {"vtableSlot", slotFor(prop.id)},
            });
        }
        entities.push_back({
            {"entityId", entity->entityId},
            {"name", entity->name},
            {"properties", std::move(properties)},
        });
    }

    nlohmann::json out = {{"entities", std::move(entities)}};
    return out.dump(2);
}

}  // namespace ob_dump_internal
