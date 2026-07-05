// Minimal smoke test for Schema::parse — no framework dependency, just
// asserts + a process exit code, run via ctest.
#include <cassert>
#include <cstdio>

#include "internal/schema.hpp"

static const char* kSampleModel = R"({
  "entities": [
    {
      "id": "1:1111",
      "name": "Ammo",
      "properties": [
        {"id": "1:1", "name": "id", "type": 6},
        {"id": "2:2", "name": "name", "type": 9},
        {"id": "6:3", "name": "bcMv1", "type": 29},
        {"id": "18:4", "name": "isFactory", "type": 1}
      ]
    }
  ]
})";

int main() {
    ob_dump_internal::Schema schema = ob_dump_internal::Schema::parse(kSampleModel);

    const ob_dump_internal::EntityDef* ammo = schema.find(1);
    assert(ammo != nullptr);
    assert(ammo->name == "Ammo");
    assert(ammo->properties.size() == 4);

    assert(ammo->properties[1].name == "name");
    assert(ammo->properties[1].type == ob_dump_internal::PropertyType::String);

    // Gap between property ids 6 and 18 (retired properties in between) must
    // parse fine — the gap itself is meaningful at decode time, not here.
    assert(ammo->properties[3].id == 18);
    assert(ammo->properties[3].type == ob_dump_internal::PropertyType::Bool);

    assert(schema.find(999) == nullptr);

    std::puts("schema_test: OK");
    return 0;
}
