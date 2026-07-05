#include "internal/fb_decode.hpp"

#include <flatbuffers/flatbuffers.h>
#include <nlohmann/json.hpp>

#include <stdexcept>

namespace ob_dump_internal {

// See declaration in fb_decode.hpp for why this lives at namespace scope
// instead of file-local. Generic FlatBuffers table convention (see
// docs/BACKLOG.md): vtable[0]/[1] are a 2-entry header, then one voffset_t
// per declared field in declaration order — ObjectBox binds each property's
// permanent numeric id as that declaration index.
uint16_t slotFor(int propertyId) {
    return static_cast<uint16_t>(4 + (propertyId - 1) * 2);
}

namespace {

using flatbuffers::Table;
using flatbuffers::Verifier;

// Bool needs its own function (not decodeScalar<T>): the wire type is a
// plain uint8_t, but the JSON value must be a real `true`/`false`, not `0`/`1`.
void decodeBool(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
               const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyField<uint8_t>(v, slot, sizeof(uint8_t))) {
        throw std::runtime_error("corrupt record: bad bool field '" + name + "'");
    }
    out[name] = t->GetField<uint8_t>(slot, 0) != 0;
}

// Covers Byte/Short/Char/Int/Long/Float/Double/Relation/Date/DateNano — all
// plain fixed-width scalars, differing only in T. `CheckField` (not "does
// the value equal T{}") is what decides presence, so a field genuinely
// stored as 0/0.0 round-trips correctly instead of being dropped.
template <typename T>
void decodeScalar(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
                  const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyField<T>(v, slot, sizeof(T))) {
        throw std::runtime_error("corrupt record: bad scalar field '" + name + "'");
    }
    // int64 fields (Long/Relation/Date/DateNano) are kept as native JSON
    // integers — nlohmann::json serializes them exactly, no double
    // round-trip. Real ObjectBox usage here is ToOne ids and epoch
    // timestamps, both far under the 2^53 threshold where naive JS JSON
    // parsers start losing precision, so no string-encoding fallback is
    // implemented (see docs/BACKLOG.md).
    out[name] = t->GetField<T>(slot, T{});
}

void decodeString(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
                  const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyOffset(v, slot)) {
        throw std::runtime_error("corrupt record: bad string offset '" + name + "'");
    }
    const auto* str = t->GetPointer<const flatbuffers::String*>(slot);
    if (!v.VerifyString(str)) {
        throw std::runtime_error("corrupt record: bad string contents '" + name + "'");
    }
    if (str != nullptr) {
        out[name] = str->str();
    }
}

// BoolVector needs its own function for the same reason decodeBool does:
// each element is a wire uint8_t but must become a JSON true/false.
void decodeBoolVector(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
                      const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyOffset(v, slot)) {
        throw std::runtime_error("corrupt record: bad vector offset '" + name + "'");
    }
    const auto* vec = t->GetPointer<const flatbuffers::Vector<uint8_t>*>(slot);
    if (!v.VerifyVector(vec)) {
        throw std::runtime_error("corrupt record: bad bool vector '" + name + "'");
    }
    if (vec == nullptr) return;
    auto arr = nlohmann::json::array();
    for (flatbuffers::uoffset_t i = 0; i < vec->size(); ++i) {
        arr.push_back(vec->Get(i) != 0);
    }
    out[name] = std::move(arr);
}

// Covers every other vector-of-scalar type (Byte/Short/Char/Int/Long/Float/
// Double/Date/DateNano vectors). A present-but-empty vector is emitted as
// `[]`, not omitted — matches decodeScalar's "presence over value" rule and
// is what real ObjectBox-written data actually does (verified against a
// live user database — see docs/BACKLOG.md verification notes).
template <typename T>
void decodeNumericVector(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
                         const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyOffset(v, slot)) {
        throw std::runtime_error("corrupt record: bad vector offset '" + name + "'");
    }
    const auto* vec = t->GetPointer<const flatbuffers::Vector<T>*>(slot);
    if (!v.VerifyVector(vec)) {
        throw std::runtime_error("corrupt record: bad vector '" + name + "'");
    }
    if (vec == nullptr) return;
    auto arr = nlohmann::json::array();
    for (flatbuffers::uoffset_t i = 0; i < vec->size(); ++i) {
        arr.push_back(vec->Get(i));
    }
    out[name] = std::move(arr);
}

void decodeStringVector(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
                       const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyOffset(v, slot)) {
        throw std::runtime_error("corrupt record: bad vector offset '" + name + "'");
    }
    const auto* vec =
        t->GetPointer<const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>*>(slot);
    if (!v.VerifyVector(vec) || !v.VerifyVectorOfStrings(vec)) {
        throw std::runtime_error("corrupt record: bad string vector '" + name + "'");
    }
    if (vec == nullptr) return;
    auto arr = nlohmann::json::array();
    for (flatbuffers::uoffset_t i = 0; i < vec->size(); ++i) {
        arr.push_back(vec->Get(i)->str());
    }
    out[name] = std::move(arr);
}

}  // namespace

nlohmann::json decodeObject(const uint8_t* data, size_t size, const EntityDef& entity) {
    Verifier verifier(data, size);

    if (verifier.VerifyOffset<flatbuffers::uoffset_t>(0) == 0) {
        throw std::runtime_error("corrupt record: invalid root offset (entity " + entity.name + ")");
    }
    const auto* table = flatbuffers::GetRoot<Table>(data);
    if (!table->VerifyTableStart(verifier)) {
        throw std::runtime_error("corrupt record: invalid vtable (entity " + entity.name + ")");
    }

    nlohmann::json out = nlohmann::json::object();

    for (const auto& prop : entity.properties) {
        if (prop.name == "id") continue;  // parsed from the LMDB key instead

        const auto slot = slotFor(prop.id);
        switch (prop.type) {
            case PropertyType::Bool:   decodeBool(table, verifier, slot, prop.name, out); break;
            case PropertyType::Byte:   decodeScalar<int8_t>(table, verifier, slot, prop.name, out); break;
            case PropertyType::Short:  decodeScalar<int16_t>(table, verifier, slot, prop.name, out); break;
            // Unicode code unit — unsigned by convention, see PropertyType::Char doc comment.
            case PropertyType::Char:   decodeScalar<uint16_t>(table, verifier, slot, prop.name, out); break;
            case PropertyType::Int:    decodeScalar<int32_t>(table, verifier, slot, prop.name, out); break;
            case PropertyType::Long:
            case PropertyType::Date:
            case PropertyType::Relation:
            case PropertyType::DateNano:
                decodeScalar<int64_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::Float:  decodeScalar<float>(table, verifier, slot, prop.name, out); break;
            case PropertyType::Double: decodeScalar<double>(table, verifier, slot, prop.name, out); break;
            case PropertyType::String: decodeString(table, verifier, slot, prop.name, out); break;

            case PropertyType::BoolVector: decodeBoolVector(table, verifier, slot, prop.name, out); break;
            case PropertyType::ByteVector:
                decodeNumericVector<int8_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::ShortVector:
                decodeNumericVector<int16_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::CharVector:
                decodeNumericVector<uint16_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::IntVector:
                decodeNumericVector<int32_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::LongVector:
            case PropertyType::DateVector:
            case PropertyType::DateNanoVector:
                decodeNumericVector<int64_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::FloatVector:
                decodeNumericVector<float>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::DoubleVector:
                decodeNumericVector<double>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::StringVector:
                decodeStringVector(table, verifier, slot, prop.name, out);
                break;

            case PropertyType::Flex:
            case PropertyType::Unknown:
                // Not implemented (see docs/BACKLOG.md "Explicitly out of
                // scope") — skip rather than fail the whole record.
                break;
        }
    }

    verifier.EndTable();
    return out;
}

}  // namespace ob_dump_internal
