#include "internal/schema.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
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
        case static_cast<int>(PropertyType::Bool):           return PropertyType::Bool;
        case static_cast<int>(PropertyType::Byte):           return PropertyType::Byte;
        case static_cast<int>(PropertyType::Short):          return PropertyType::Short;
        case static_cast<int>(PropertyType::Char):           return PropertyType::Char;
        case static_cast<int>(PropertyType::Int):            return PropertyType::Int;
        case static_cast<int>(PropertyType::Long):           return PropertyType::Long;
        case static_cast<int>(PropertyType::Float):          return PropertyType::Float;
        case static_cast<int>(PropertyType::Double):         return PropertyType::Double;
        case static_cast<int>(PropertyType::String):         return PropertyType::String;
        case static_cast<int>(PropertyType::Date):           return PropertyType::Date;
        case static_cast<int>(PropertyType::Relation):       return PropertyType::Relation;
        case static_cast<int>(PropertyType::DateNano):       return PropertyType::DateNano;
        case static_cast<int>(PropertyType::Flex):           return PropertyType::Flex;
        case static_cast<int>(PropertyType::BoolVector):     return PropertyType::BoolVector;
        case static_cast<int>(PropertyType::ByteVector):     return PropertyType::ByteVector;
        case static_cast<int>(PropertyType::ShortVector):    return PropertyType::ShortVector;
        case static_cast<int>(PropertyType::CharVector):     return PropertyType::CharVector;
        case static_cast<int>(PropertyType::IntVector):      return PropertyType::IntVector;
        case static_cast<int>(PropertyType::LongVector):     return PropertyType::LongVector;
        case static_cast<int>(PropertyType::FloatVector):    return PropertyType::FloatVector;
        case static_cast<int>(PropertyType::DoubleVector):   return PropertyType::DoubleVector;
        case static_cast<int>(PropertyType::StringVector):   return PropertyType::StringVector;
        case static_cast<int>(PropertyType::DateVector):     return PropertyType::DateVector;
        case static_cast<int>(PropertyType::DateNanoVector): return PropertyType::DateNanoVector;
        default:                                             return PropertyType::Unknown;
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

std::vector<const EntityDef*> Schema::all() const {
    std::vector<const EntityDef*> result;
    result.reserve(entitiesById_.size());
    for (const auto& [id, entity] : entitiesById_) {
        result.push_back(&entity);
    }
    std::sort(result.begin(), result.end(),
             [](const EntityDef* a, const EntityDef* b) { return a->entityId < b->entityId; });
    return result;
}

const char* toString(PropertyType type) {
    switch (type) {
        case PropertyType::Bool:           return "Bool";
        case PropertyType::Byte:           return "Byte";
        case PropertyType::Short:          return "Short";
        case PropertyType::Char:           return "Char";
        case PropertyType::Int:            return "Int";
        case PropertyType::Long:           return "Long";
        case PropertyType::Float:          return "Float";
        case PropertyType::Double:         return "Double";
        case PropertyType::String:         return "String";
        case PropertyType::Date:           return "Date";
        case PropertyType::Relation:       return "Relation";
        case PropertyType::DateNano:       return "DateNano";
        case PropertyType::Flex:           return "Flex";
        case PropertyType::BoolVector:     return "BoolVector";
        case PropertyType::ByteVector:     return "ByteVector";
        case PropertyType::ShortVector:    return "ShortVector";
        case PropertyType::CharVector:     return "CharVector";
        case PropertyType::IntVector:      return "IntVector";
        case PropertyType::LongVector:     return "LongVector";
        case PropertyType::FloatVector:    return "FloatVector";
        case PropertyType::DoubleVector:   return "DoubleVector";
        case PropertyType::StringVector:   return "StringVector";
        case PropertyType::DateVector:     return "DateVector";
        case PropertyType::DateNanoVector: return "DateNanoVector";
        case PropertyType::Unknown:        return "Unknown";
    }
    return "Unknown";
}

}  // namespace ob_dump_internal
