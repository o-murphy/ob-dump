#include "internal/fb_decode.hpp"

#include <flatbuffers/flatbuffers.h>
#include <nlohmann/json.hpp>

#include <stdexcept>

namespace ob_dump_internal {

namespace {

// property_id -> vtable slot. Generic FlatBuffers table convention (see
// docs/BACKLOG.md): vtable[0]/[1] are a 2-entry header, then one voffset_t
// per declared field in declaration order — ObjectBox binds each property's
// permanent numeric id as that declaration index.
flatbuffers::voffset_t slotFor(int propertyId) {
    return static_cast<flatbuffers::voffset_t>(4 + (propertyId - 1) * 2);
}

using flatbuffers::Table;
using flatbuffers::Verifier;

void decodeBool(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
               const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyField<uint8_t>(v, slot, sizeof(uint8_t))) {
        throw std::runtime_error("corrupt record: bad bool field '" + name + "'");
    }
    out[name] = t->GetField<uint8_t>(slot, 0) != 0;
}

void decodeLong(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
                const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyField<int64_t>(v, slot, sizeof(int64_t))) {
        throw std::runtime_error("corrupt record: bad int64 field '" + name + "'");
    }
    // Kept as a native JSON integer (nlohmann::json serializes int64_t
    // exactly, no double round-trip). The only int64 fields ObjectBox uses
    // here are ToOne relation ids and ms-since-epoch dates — both far under
    // the 2^53 threshold where naive JS JSON parsers start losing precision,
    // so no string-encoding fallback is implemented (see docs/BACKLOG.md).
    out[name] = t->GetField<int64_t>(slot, 0);
}

void decodeDouble(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
                  const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyField<double>(v, slot, sizeof(double))) {
        throw std::runtime_error("corrupt record: bad double field '" + name + "'");
    }
    out[name] = t->GetField<double>(slot, 0.0);
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

void decodeDoubleVector(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
                       const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyOffset(v, slot)) {
        throw std::runtime_error("corrupt record: bad vector offset '" + name + "'");
    }
    const auto* vec = t->GetPointer<const flatbuffers::Vector<double>*>(slot);
    if (!v.VerifyVector(vec)) {
        throw std::runtime_error("corrupt record: bad double vector '" + name + "'");
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
            case PropertyType::Bool:         decodeBool(table, verifier, slot, prop.name, out); break;
            case PropertyType::Long:
            case PropertyType::Relation:     decodeLong(table, verifier, slot, prop.name, out); break;
            case PropertyType::Double:       decodeDouble(table, verifier, slot, prop.name, out); break;
            case PropertyType::String:       decodeString(table, verifier, slot, prop.name, out); break;
            case PropertyType::DoubleVector: decodeDoubleVector(table, verifier, slot, prop.name, out); break;
            case PropertyType::StringVector: decodeStringVector(table, verifier, slot, prop.name, out); break;
            case PropertyType::Unknown:
                // Not one of the types this reader supports yet (see
                // docs/BACKLOG.md "Explicitly out of scope") — skip rather
                // than fail the whole record.
                break;
        }
    }

    verifier.EndTable();
    return out;
}

}  // namespace ob_dump_internal
