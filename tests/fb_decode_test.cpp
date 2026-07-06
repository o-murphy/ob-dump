// Exercises decodeObject() against real, well-formed FlatBuffers tables
// built with flatbuffers::FlatBufferBuilder's low-level AddElement/AddOffset
// API — the same primitives ObjectBox itself uses (writing each property at
// the vtable slot derived from its permanent property id). No framework
// dependency: asserts + process exit code, run via ctest.
#include <cassert>
#include <cstdio>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/flexbuffers.h>
#include <nlohmann/json.hpp>

#include "internal/fb_decode.hpp"
#include "internal/schema.hpp"

using ob_dump_internal::EntityDef;
namespace ExternalPropertyType = ob_dump_internal::ExternalPropertyType;
using ob_dump_internal::PropertyDef;
using ob_dump_internal::PropertyType;
using ob_dump_internal::decodeObject;
using ob_dump_internal::slotFor;

namespace {

EntityDef makeAllTypesEntity() {
    EntityDef e;
    e.entityId = 1;
    e.name     = "AllTypes";
    e.properties = {
        {1, "boolField", PropertyType::Bool},
        {2, "byteField", PropertyType::Byte},
        {3, "shortField", PropertyType::Short},
        {4, "charField", PropertyType::Char},
        {5, "intField", PropertyType::Int},
        {6, "longField", PropertyType::Long},
        {7, "floatField", PropertyType::Float},
        {8, "doubleField", PropertyType::Double},
        {9, "stringField", PropertyType::String},
        {10, "dateField", PropertyType::Date},
        {11, "relationField", PropertyType::Relation},
        {12, "dateNanoField", PropertyType::DateNano},
        {13, "boolVectorField", PropertyType::BoolVector},
        {14, "byteVectorField", PropertyType::ByteVector},
        {15, "shortVectorField", PropertyType::ShortVector},
        {16, "charVectorField", PropertyType::CharVector},
        {17, "intVectorField", PropertyType::IntVector},
        {18, "longVectorField", PropertyType::LongVector},
        {19, "floatVectorField", PropertyType::FloatVector},
        {20, "doubleVectorField", PropertyType::DoubleVector},
        {21, "stringVectorField", PropertyType::StringVector},
        {22, "dateVectorField", PropertyType::DateVector},
        {23, "dateNanoVectorField", PropertyType::DateNanoVector},
    };
    return e;
}

// Builds one fully-populated record for makeAllTypesEntity(), using the
// unconditional 2-arg AddElement<T>(field, value) overload throughout so
// every field is force-written regardless of whether its value happens to
// equal that type's zero/default (FlatBufferBuilder's 3-arg overload skips
// writing default-valued fields — not what we want for a deterministic test
// fixture).
flatbuffers::FlatBufferBuilder buildAllTypesRecord() {
    flatbuffers::FlatBufferBuilder fbb;

    // Offset-type contents (strings/vectors) must be created before
    // StartTable() — FlatBuffers builds bottom-up.
    auto stringOff = fbb.CreateString(std::string("hello"));
    auto boolVecOff = fbb.CreateVector<uint8_t>({1, 0, 1});
    auto byteVecOff = fbb.CreateVector<int8_t>({-1, 2, -3});
    auto shortVecOff = fbb.CreateVector<int16_t>({-100, 200});
    auto charVecOff = fbb.CreateVector<uint16_t>({65, 66});
    auto intVecOff = fbb.CreateVector<int32_t>({-1000, 2000});
    auto longVecOff = fbb.CreateVector<int64_t>({-1, 0, 1});
    auto floatVecOff = fbb.CreateVector<float>({1.5f, -2.5f});
    auto doubleVecOff = fbb.CreateVector<double>({1.25, -2.5});
    auto stringVecOff = fbb.CreateVectorOfStrings(std::vector<std::string>{"a", "b"});
    auto dateVecOff = fbb.CreateVector<int64_t>({111, 222});
    auto dateNanoVecOff = fbb.CreateVector<int64_t>({333, 444});

    auto start = fbb.StartTable();
    fbb.AddElement<uint8_t>(slotFor(1), 1);              // boolField = true
    fbb.AddElement<int8_t>(slotFor(2), -5);               // byteField
    fbb.AddElement<int16_t>(slotFor(3), -1000);            // shortField
    fbb.AddElement<uint16_t>(slotFor(4), 0x4142);           // charField
    fbb.AddElement<int32_t>(slotFor(5), -100000);           // intField
    fbb.AddElement<int64_t>(slotFor(6), -1);                // longField
    fbb.AddElement<float>(slotFor(7), 3.5f);                // floatField
    fbb.AddElement<double>(slotFor(8), 0.0);                // doubleField — genuinely zero, must still be present
    fbb.AddOffset(slotFor(9), stringOff);                   // stringField
    fbb.AddElement<int64_t>(slotFor(10), 1700000000000);    // dateField
    fbb.AddElement<int64_t>(slotFor(11), 0);                // relationField — id 0, must still be present (see BACKLOG)
    fbb.AddElement<int64_t>(slotFor(12), 1700000000000000000LL); // dateNanoField
    fbb.AddOffset(slotFor(13), boolVecOff);
    fbb.AddOffset(slotFor(14), byteVecOff);
    fbb.AddOffset(slotFor(15), shortVecOff);
    fbb.AddOffset(slotFor(16), charVecOff);
    fbb.AddOffset(slotFor(17), intVecOff);
    fbb.AddOffset(slotFor(18), longVecOff);
    fbb.AddOffset(slotFor(19), floatVecOff);
    fbb.AddOffset(slotFor(20), doubleVecOff);
    fbb.AddOffset(slotFor(21), stringVecOff);
    fbb.AddOffset(slotFor(22), dateVecOff);
    fbb.AddOffset(slotFor(23), dateNanoVecOff);
    auto end = fbb.EndTable(start);
    fbb.Finish(flatbuffers::Offset<flatbuffers::Table>(end));

    return fbb;
}

void testAllTypesHappyPath() {
    auto fbb = buildAllTypesRecord();
    auto entity = makeAllTypesEntity();

    nlohmann::json j = decodeObject(fbb.GetBufferPointer(), fbb.GetSize(), entity);

    assert(j.at("boolField").get<bool>() == true);
    assert(j.at("byteField").get<int>() == -5);
    assert(j.at("shortField").get<int>() == -1000);
    assert(j.at("charField").get<int>() == 0x4142);
    assert(j.at("intField").get<int32_t>() == -100000);
    assert(j.at("longField").get<int64_t>() == -1);
    assert(j.at("floatField").get<float>() == 3.5f);
    assert(j.at("doubleField").get<double>() == 0.0);  // present, not omitted
    assert(j.at("stringField").get<std::string>() == "hello");
    assert(j.at("dateField").get<int64_t>() == 1700000000000);
    assert(j.at("relationField").get<int64_t>() == 0);  // present, not omitted
    assert(j.at("dateNanoField").get<int64_t>() == 1700000000000000000LL);

    assert((j.at("boolVectorField") == nlohmann::json::array({true, false, true})));
    assert((j.at("byteVectorField") == nlohmann::json::array({-1, 2, -3})));
    assert((j.at("shortVectorField") == nlohmann::json::array({-100, 200})));
    assert((j.at("charVectorField") == nlohmann::json::array({65, 66})));
    assert((j.at("intVectorField") == nlohmann::json::array({-1000, 2000})));
    assert((j.at("longVectorField") == nlohmann::json::array({-1, 0, 1})));
    assert(j.at("floatVectorField").at(0).get<float>() == 1.5f);
    assert(j.at("floatVectorField").at(1).get<float>() == -2.5f);
    assert((j.at("doubleVectorField") == nlohmann::json::array({1.25, -2.5})));
    assert((j.at("stringVectorField") == nlohmann::json::array({"a", "b"})));
    assert((j.at("dateVectorField") == nlohmann::json::array({111, 222})));
    assert((j.at("dateNanoVectorField") == nlohmann::json::array({333, 444})));

    std::puts("testAllTypesHappyPath: OK");
}

// A property present in the *schema* but never written to this particular
// record's vtable (e.g. added after this record, or simply never set) must
// be silently omitted from the JSON, not defaulted/nulled/thrown.
void testAbsentFieldIsOmitted() {
    flatbuffers::FlatBufferBuilder fbb;
    auto start = fbb.StartTable();
    fbb.AddElement<int32_t>(slotFor(1), 42);  // only property id 1 written
    auto end = fbb.EndTable(start);
    fbb.Finish(flatbuffers::Offset<flatbuffers::Table>(end));

    EntityDef entity;
    entity.entityId = 1;
    entity.name     = "Partial";
    entity.properties = {
        {1, "present", PropertyType::Int},
        {2, "neverWritten", PropertyType::Int},
    };

    nlohmann::json j = decodeObject(fbb.GetBufferPointer(), fbb.GetSize(), entity);
    assert(j.at("present").get<int32_t>() == 42);
    assert(!j.contains("neverWritten"));

    std::puts("testAbsentFieldIsOmitted: OK");
}

// A present-but-empty vector must still appear as `[]`, not be omitted —
// this is the exact behavior confirmed against real ObjectBox-written data
// (see docs/BACKLOG.md verification notes).
void testEmptyVectorIsPresentNotOmitted() {
    flatbuffers::FlatBufferBuilder fbb;
    auto emptyVecOff = fbb.CreateVector<double>(std::vector<double>{});
    auto start = fbb.StartTable();
    fbb.AddOffset(slotFor(1), emptyVecOff);
    auto end = fbb.EndTable(start);
    fbb.Finish(flatbuffers::Offset<flatbuffers::Table>(end));

    EntityDef entity;
    entity.entityId = 1;
    entity.name     = "EmptyVec";
    entity.properties = {{1, "vec", PropertyType::DoubleVector}};

    nlohmann::json j = decodeObject(fbb.GetBufferPointer(), fbb.GetSize(), entity);
    assert(j.contains("vec"));
    assert(j.at("vec").is_array());
    assert(j.at("vec").empty());

    std::puts("testEmptyVectorIsPresentNotOmitted: OK");
}

// Flex/Unknown property types must be skipped, not crash the whole record.
void testUnknownTypeIsSkipped() {
    flatbuffers::FlatBufferBuilder fbb;
    auto start = fbb.StartTable();
    fbb.AddElement<int32_t>(slotFor(1), 7);
    auto end = fbb.EndTable(start);
    fbb.Finish(flatbuffers::Offset<flatbuffers::Table>(end));

    EntityDef entity;
    entity.entityId = 1;
    entity.name     = "WithFlex";
    entity.properties = {
        {1, "known", PropertyType::Int},
        {2, "flexField", PropertyType::Flex},
    };

    nlohmann::json j = decodeObject(fbb.GetBufferPointer(), fbb.GetSize(), entity);
    assert(j.at("known").get<int32_t>() == 7);
    assert(!j.contains("flexField"));

    std::puts("testUnknownTypeIsSkipped: OK");
}

// UNSIGNED (ObjectBox "flags" bit 8192) must flip decoding to the unsigned
// variant of the same width — otherwise a genuinely-unsigned value large
// enough to set the sign bit would print as a wrong, negative number.
void testUnsignedIntegersDecodeCorrectly() {
    flatbuffers::FlatBufferBuilder fbb;
    auto ubyteVecOff = fbb.CreateVector<uint8_t>({200, 250});
    auto start = fbb.StartTable();
    fbb.AddElement<uint8_t>(slotFor(1), 200);                       // byteField: 200, not -56
    fbb.AddElement<uint32_t>(slotFor(2), 3000000000u);              // intField: 3e9, not negative
    fbb.AddElement<uint64_t>(slotFor(3), 10000000000000000000ULL);  // longField: > INT64_MAX
    fbb.AddOffset(slotFor(4), ubyteVecOff);                         // byteVectorField
    auto end = fbb.EndTable(start);
    fbb.Finish(flatbuffers::Offset<flatbuffers::Table>(end));

    EntityDef entity;
    entity.entityId = 1;
    entity.name     = "Unsigned";
    entity.properties = {
        {1, "byteField", PropertyType::Byte, true},
        {2, "intField", PropertyType::Int, true},
        {3, "longField", PropertyType::Long, true},
        {4, "byteVectorField", PropertyType::ByteVector, true},
    };

    nlohmann::json j = decodeObject(fbb.GetBufferPointer(), fbb.GetSize(), entity);
    assert(j.at("byteField").get<int>() == 200);
    assert(j.at("intField").get<uint32_t>() == 3000000000u);
    assert(j.at("longField").get<uint64_t>() == 10000000000000000000ULL);
    assert((j.at("byteVectorField") == nlohmann::json::array({200, 250})));

    std::puts("testUnsignedIntegersDecodeCorrectly: OK");
}

// ExternalPropertyType Uuid/Int128/Decimal128/Bson are all physically a
// ByteVector on the wire but represent an opaque blob — decoded as a hex
// string (Uuid additionally grouped into canonical 8-4-4-4-12 form) rather
// than a JSON array of small integers.
void testExternalTypeByteVectorsDecodeAsStrings() {
    flatbuffers::FlatBufferBuilder fbb;
    const std::vector<uint8_t> uuidBytes = {
        0x55, 0x0e, 0x84, 0x00, 0xe2, 0x9b, 0x41, 0xd4,
        0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00,
    };
    const std::vector<uint8_t> blobBytes = {0xde, 0xad, 0xbe, 0xef};
    auto uuidOff = fbb.CreateVector<uint8_t>(uuidBytes);
    auto blobOff = fbb.CreateVector<uint8_t>(blobBytes);

    auto start = fbb.StartTable();
    fbb.AddOffset(slotFor(1), uuidOff);
    fbb.AddOffset(slotFor(2), blobOff);
    auto end = fbb.EndTable(start);
    fbb.Finish(flatbuffers::Offset<flatbuffers::Table>(end));

    EntityDef entity;
    entity.entityId = 1;
    entity.name     = "ExternalTypes";
    entity.properties = {
        {1, "uuidField", PropertyType::ByteVector, false, ExternalPropertyType::Uuid},
        {2, "int128Field", PropertyType::ByteVector, false, ExternalPropertyType::Int128},
    };

    nlohmann::json j = decodeObject(fbb.GetBufferPointer(), fbb.GetSize(), entity);
    assert(j.at("uuidField").get<std::string>() == "550e8400-e29b-41d4-a716-446655440000");
    assert(j.at("int128Field").get<std::string>() == "deadbeef");

    std::puts("testExternalTypeByteVectorsDecodeAsStrings: OK");
}

// Wraps a FlexBuffers-encoded blob as the sole ByteVector field of a
// one-property "Flex" entity, decodes it, and returns the resulting value
// at that field (not the whole object) for easy assertions.
nlohmann::json decodeFlexValue(flexbuffers::Builder& flex) {
    flex.Finish();
    const auto& flexBuf = flex.GetBuffer();

    flatbuffers::FlatBufferBuilder fbb;
    auto flexOff = fbb.CreateVector<uint8_t>(flexBuf);
    auto start = fbb.StartTable();
    fbb.AddOffset(slotFor(1), flexOff);
    auto end = fbb.EndTable(start);
    fbb.Finish(flatbuffers::Offset<flatbuffers::Table>(end));

    EntityDef entity;
    entity.entityId = 1;
    entity.name     = "FlexHolder";
    entity.properties = {{1, "flexField", PropertyType::Flex}};

    nlohmann::json j = decodeObject(fbb.GetBufferPointer(), fbb.GetSize(), entity);
    return j.at("flexField");
}

// A FlexBuffers map decodes to the equivalent JSON object, recursing into
// nested vectors/scalars/strings correctly.
void testFlexMapDecodesToJsonObject() {
    flexbuffers::Builder flex;
    flex.Map([&] {
        flex.Int("a", 1);
        flex.String("b", "hello");
        flex.Vector("c", [&] {
            flex.Int(1);
            flex.Int(2);
            flex.Int(3);
        });
        flex.Bool("d", true);
        flex.Null("e");
    });

    nlohmann::json j = decodeFlexValue(flex);
    assert(j.at("a").get<int64_t>() == 1);
    assert(j.at("b").get<std::string>() == "hello");
    assert((j.at("c") == nlohmann::json::array({1, 2, 3})));
    assert(j.at("d").get<bool>() == true);
    assert(j.at("e").is_null());

    std::puts("testFlexMapDecodesToJsonObject: OK");
}

// A FlexBuffers value doesn't have to be a map at the root — a bare vector
// (or even a bare scalar) is equally valid and must decode correctly too.
void testFlexVectorRootDecodesToJsonArray() {
    flexbuffers::Builder flex;
    flex.Vector([&] {
        flex.String("x");
        flex.Double(2.5);
        flex.Bool(false);
    });

    nlohmann::json j = decodeFlexValue(flex);
    assert(j.is_array());
    assert(j.at(0).get<std::string>() == "x");
    assert(j.at(1).get<double>() == 2.5);
    assert(j.at(2).get<bool>() == false);

    std::puts("testFlexVectorRootDecodesToJsonArray: OK");
}

// A corrupted FlexBuffers blob (truncated mid-value) must be caught by
// flexbuffers::VerifyBuffer(), not just the outer FlatBuffers Verifier —
// the outer one only bounds-checks the ByteVector itself, not the
// independently-encoded FlexBuffers structure inside it.
void testCorruptFlexBufferThrows() {
    flexbuffers::Builder flex;
    flex.Map([&] {
        flex.String("a", "a reasonably long string so truncation lands mid-structure");
    });
    flex.Finish();
    auto flexBuf = flex.GetBuffer();
    flexBuf.resize(flexBuf.size() / 2);  // truncate — no longer a valid FlexBuffers value

    flatbuffers::FlatBufferBuilder fbb;
    auto flexOff = fbb.CreateVector<uint8_t>(flexBuf);
    auto start = fbb.StartTable();
    fbb.AddOffset(slotFor(1), flexOff);
    auto end = fbb.EndTable(start);
    fbb.Finish(flatbuffers::Offset<flatbuffers::Table>(end));

    EntityDef entity;
    entity.entityId = 1;
    entity.name     = "FlexHolder";
    entity.properties = {{1, "flexField", PropertyType::Flex}};

    bool threw = false;
    try {
        decodeObject(fbb.GetBufferPointer(), fbb.GetSize(), entity);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    std::puts("testCorruptFlexBufferThrows: OK");
}

// A truncated/corrupted buffer must be caught by flatbuffers::Verifier and
// surfaced as an exception, never read out of bounds.
void testCorruptBufferThrows() {
    flatbuffers::FlatBufferBuilder fbb;
    auto start = fbb.StartTable();
    fbb.AddElement<int32_t>(slotFor(1), 7);
    auto end = fbb.EndTable(start);
    fbb.Finish(flatbuffers::Offset<flatbuffers::Table>(end));

    EntityDef entity;
    entity.entityId = 1;
    entity.name     = "Truncated";
    entity.properties = {{1, "field", PropertyType::Int}};

    // Truncate to a few bytes — not enough to even hold a valid root offset.
    bool threw = false;
    try {
        decodeObject(fbb.GetBufferPointer(), 2, entity);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    std::puts("testCorruptBufferThrows: OK");
}

}  // namespace

int main() {
    testAllTypesHappyPath();
    testAbsentFieldIsOmitted();
    testEmptyVectorIsPresentNotOmitted();
    testUnknownTypeIsSkipped();
    testUnsignedIntegersDecodeCorrectly();
    testExternalTypeByteVectorsDecodeAsStrings();
    testFlexMapDecodesToJsonObject();
    testFlexVectorRootDecodesToJsonArray();
    testCorruptFlexBufferThrows();
    testCorruptBufferThrows();
    std::puts("fb_decode_test: OK");
    return 0;
}
