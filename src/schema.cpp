#include "internal/schema.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ob_dump_internal {

namespace {

// Property/entity "id" fields are formatted "<id>:<uid>" — we only need the
// numeric id (the uid is ObjectBox's own stable-rename tracking, irrelevant
// for decoding).
int parseLeadingId(const std::string& idUid) {
    auto colon = idUid.find(':');
    if (colon == std::string::npos) {
        throw std::runtime_error("malformed id field (expected \"id:uid\"): " + idUid);
    }
    return std::stoi(idUid.substr(0, colon));
}

PropertyType toPropertyType(int raw) {
    switch (raw) {
        case static_cast<int>(PropertyType::Bool):         return PropertyType::Bool;
        case static_cast<int>(PropertyType::Long):         return PropertyType::Long;
        case static_cast<int>(PropertyType::Double):       return PropertyType::Double;
        case static_cast<int>(PropertyType::String):       return PropertyType::String;
        case static_cast<int>(PropertyType::Relation):     return PropertyType::Relation;
        case static_cast<int>(PropertyType::DoubleVector): return PropertyType::DoubleVector;
        case static_cast<int>(PropertyType::StringVector): return PropertyType::StringVector;
        default:                                           return PropertyType::Unknown;
    }
}

}  // namespace

Schema Schema::parse(const std::string& modelJson) {
    nlohmann::json model = nlohmann::json::parse(modelJson);

    Schema schema;
    if (!model.contains("entities") || !model["entities"].is_array()) {
        throw std::runtime_error("model json has no \"entities\" array");
    }

    for (const auto& e : model["entities"]) {
        EntityDef entity;
        entity.entityId = parseLeadingId(e.at("id").get<std::string>());
        entity.name     = e.at("name").get<std::string>();

        for (const auto& p : e.at("properties")) {
            PropertyDef prop;
            prop.id   = parseLeadingId(p.at("id").get<std::string>());
            prop.name = p.at("name").get<std::string>();
            prop.type = toPropertyType(p.at("type").get<int>());
            // Unknown types are kept (not skipped) so fb_decode can report
            // exactly which property it couldn't read, rather than silently
            // producing a field count mismatch.
            entity.properties.push_back(std::move(prop));
        }

        schema.entitiesById_.emplace(entity.entityId, std::move(entity));
    }

    return schema;
}

const EntityDef* Schema::find(int entityId) const {
    auto it = entitiesById_.find(entityId);
    return it == entitiesById_.end() ? nullptr : &it->second;
}

}  // namespace ob_dump_internal
