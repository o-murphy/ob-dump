#include <cassert>
#include <cstdio>

#include <nlohmann/json.hpp>

#include "internal/schema_json.hpp"

static const char* kSampleModel = R"({
  "entities": [
    {
      "id": "1:1111",
      "name": "Ammo",
      "properties": [
        {"id": "1:1", "name": "id", "type": 6},
        {"id": "2:2", "name": "name", "type": 9},
        {"id": "18:4", "name": "isFactory", "type": 1},
        {"id": "19:5", "name": "unsignedCount", "type": 6, "flags": 8192},
        {"id": "20:6", "name": "externalId", "type": 23, "externalType": 102}
      ]
    },
    {
      "id": "2:2222",
      "name": "Weapon",
      "properties": [
        {"id": "1:1", "name": "id", "type": 6}
      ],
      "relations": [
        {"id": "1:9999", "name": "compatibleAmmo", "targetId": "1:1111"}
      ]
    }
  ]
})";

int main() {
    std::string jsonText = ob_dump_internal::schemaToJson(kSampleModel);
    nlohmann::json j = nlohmann::json::parse(jsonText);

    assert(j.at("entities").is_array());
    assert(j.at("entities").size() == 2);

    // Sorted by entityId — Ammo (1) before Weapon (2).
    const auto& ammo = j.at("entities").at(0);
    assert(ammo.at("entityId").get<int>() == 1);
    assert(ammo.at("name").get<std::string>() == "Ammo");
    assert(ammo.at("properties").size() == 5);

    const auto& nameProp = ammo.at("properties").at(1);
    assert(nameProp.at("id").get<int>() == 2);
    assert(nameProp.at("name").get<std::string>() == "name");
    assert(nameProp.at("type").get<std::string>() == "String");
    assert(nameProp.at("vtableSlot").get<int>() == 4 + (2 - 1) * 2);
    // Not notable — "unsigned"/"externalType" must be absent, not "false"/null.
    assert(!nameProp.contains("unsigned"));
    assert(!nameProp.contains("externalType"));

    const auto& gappedProp = ammo.at("properties").at(2);
    assert(gappedProp.at("id").get<int>() == 18);
    assert(gappedProp.at("vtableSlot").get<int>() == 4 + (18 - 1) * 2);

    const auto& unsignedProp = ammo.at("properties").at(3);
    assert(unsignedProp.at("name").get<std::string>() == "unsignedCount");
    assert(unsignedProp.at("unsigned").get<bool>() == true);

    const auto& externalProp = ammo.at("properties").at(4);
    assert(externalProp.at("name").get<std::string>() == "externalId");
    assert(externalProp.at("externalType").get<std::string>() == "Uuid");

    const auto& weapon = j.at("entities").at(1);
    assert(weapon.at("name").get<std::string>() == "Weapon");

    // Not notable for Ammo — no relations declared, key must be absent
    // entirely rather than an empty array.
    assert(!ammo.contains("relations"));

    assert(weapon.at("relations").size() == 1);
    const auto& rel = weapon.at("relations").at(0);
    assert(rel.at("id").get<int>() == 1);
    assert(rel.at("name").get<std::string>() == "compatibleAmmo");
    assert(rel.at("targetEntityId").get<int>() == 1);

    std::puts("schema_json_test: OK");
    return 0;
}
