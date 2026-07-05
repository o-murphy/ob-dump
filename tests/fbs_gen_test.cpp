#include <cassert>
#include <cstdio>
#include <string>

#include "internal/fbs_gen.hpp"

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

// ids: 1, 2 present; 3, 4 retired (gap); 5 is a DoubleVector; 6 is Flex;
// 7 is an unrecognized raw type code (999) — exercises every branch of the
// generator: normal scalar, normal vector, gap placeholders, Flex-as-raw-
// bytes, and Unknown-as-deprecated-but-named.
static const char* kSampleModel = R"({
  "entities": [
    {
      "id": "1:1111",
      "name": "Ammo",
      "properties": [
        {"id": "1:1", "name": "id", "type": 6},
        {"id": "2:2", "name": "name", "type": 9},
        {"id": "5:5", "name": "coeffs", "type": 29},
        {"id": "6:6", "name": "extra", "type": 13},
        {"id": "7:7", "name": "weird", "type": 999}
      ]
    }
  ]
})";

int main() {
    std::string fbs = ob_dump_internal::generateFbs(kSampleModel);

    assert(contains(fbs, "table Ammo {"));

    // Normal scalar and normal vector fields, in id order.
    assert(contains(fbs, "id:long;"));
    assert(contains(fbs, "name:string;"));
    assert(contains(fbs, "coeffs:[double];"));

    // Gaps at ids 3 and 4 must be present as named-by-id deprecated placeholders.
    assert(contains(fbs, "_reserved_3:ubyte (deprecated);"));
    assert(contains(fbs, "_reserved_4:ubyte (deprecated);"));

    // Flex: raw bytes, NOT marked deprecated — it's real, just undecoded data.
    assert(contains(fbs, "extra:[ubyte];"));
    assert(!contains(fbs, "extra:[ubyte]; (deprecated)"));

    // Unrecognized type code: kept by name, but deprecated since its shape is unknown.
    assert(contains(fbs, "weird:ubyte (deprecated);"));

    // Field order must follow id order: id(1) before name(2) before
    // _reserved_3 before _reserved_4 before coeffs(5) before extra(6)
    // before weird(7).
    auto pos = [&](const std::string& s) { return fbs.find(s); };
    assert(pos("id:long;") < pos("name:string;"));
    assert(pos("name:string;") < pos("_reserved_3"));
    assert(pos("_reserved_3") < pos("_reserved_4"));
    assert(pos("_reserved_4") < pos("coeffs:[double];"));
    assert(pos("coeffs:[double];") < pos("extra:[ubyte];"));
    assert(pos("extra:[ubyte];") < pos("weird:ubyte (deprecated);"));

    std::puts("fbs_gen_test: OK");
    return 0;
}
