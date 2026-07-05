#include "internal/dumper.hpp"

#include <nlohmann/json.hpp>

#include "internal/fb_decode.hpp"
#include "internal/lmdb_reader.hpp"
#include "internal/schema.hpp"

namespace ob_dump_internal {

std::string dumpToJson(const std::string& mdbPath, const std::string& modelJson) {
    Schema schema = Schema::parse(modelJson);
    LmdbReader reader(mdbPath);

    nlohmann::json result = nlohmann::json::object();

    reader.forEachObject([&](const ObjectRecord& rec) {
        const EntityDef* entity = schema.find(rec.entityId);
        if (entity == nullptr) {
            // Object record for an entity id the model doesn't know about —
            // model/data mismatch. Skip rather than fail the whole dump.
            return;
        }

        nlohmann::json obj = decodeObject(rec.data, rec.size, *entity);
        obj["id"] = rec.objectId;

        auto& arr = result[entity->name];
        if (!arr.is_array()) arr = nlohmann::json::array();
        arr.push_back(std::move(obj));
    });

    return result.dump(2);
}

}  // namespace ob_dump_internal
