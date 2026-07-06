#include "internal/fb_decode.hpp"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/flexbuffers.h>
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

std::string toHex(const uint8_t* data, size_t size) {
    static const char* kHexDigits = "0123456789abcdef";
    std::string hex;
    hex.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        hex.push_back(kHexDigits[data[i] >> 4]);
        hex.push_back(kHexDigits[data[i] & 0x0F]);
    }
    return hex;
}

// Groups a 16-byte hex string into the canonical 8-4-4-4-12 UUID form.
// Falls back to plain hex for anything else — a malformed/non-UUID field
// shouldn't throw just because of its externalType annotation.
std::string hexAsUuid(const std::string& hex) {
    if (hex.size() != 32) return hex;
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
          hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

// Hex-encodes a byte vector's raw contents. Used for ExternalPropertyType
// codes whose base wire type is ByteVector but that represent an opaque
// blob (Int128, Decimal128, Bson) rather than a list of small integers —
// see docs/BACKLOG.md. `asUuid` additionally groups the hex digits into
// the canonical UUID string form.
void decodeByteVectorAsBlobString(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
                                  const std::string& name, nlohmann::json& out, bool asUuid) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyOffset(v, slot)) {
        throw std::runtime_error("corrupt record: bad vector offset '" + name + "'");
    }
    const auto* vec = t->GetPointer<const flatbuffers::Vector<uint8_t>*>(slot);
    if (!v.VerifyVector(vec)) {
        throw std::runtime_error("corrupt record: bad byte vector '" + name + "'");
    }
    if (vec == nullptr) return;

    std::string hex = toHex(vec->Data(), vec->size());
    out[name] = asUuid ? hexAsUuid(hex) : hex;
}

// Recursively converts a FlexBuffers value into the equivalent JSON shape.
// Forward-declared because it's mutually recursive with flexVectorToJson.
nlohmann::json flexToJson(flexbuffers::Reference ref);

// Vector/TypedVector/FixedTypedVector are three different concrete classes
// with no common base, but all three independently provide size() and
// operator[](i)->Reference — genericizing over that shared shape here
// avoids three near-identical hand-written loops.
template <typename VecT>
nlohmann::json flexVectorToJson(const VecT& vec) {
    auto arr = nlohmann::json::array();
    for (size_t i = 0; i < vec.size(); ++i) {
        arr.push_back(flexToJson(vec[i]));
    }
    return arr;
}

nlohmann::json flexToJson(flexbuffers::Reference ref) {
    // Order matters: IsVector() is also true for maps (FlexBuffers models a
    // map as a pair of parallel vectors internally), so IsMap() must be
    // checked first.
    if (ref.IsNull()) {
        return nullptr;
    } else if (ref.IsBool()) {
        return ref.AsBool();
    } else if (ref.IsInt()) {
        return ref.AsInt64();
    } else if (ref.IsUInt()) {
        return ref.AsUInt64();
    } else if (ref.IsFloat()) {
        return ref.AsDouble();
    } else if (ref.IsString()) {
        return ref.AsString().str();
    } else if (ref.IsBlob()) {
        // Same "opaque bytes -> hex string" convention as ByteVector's
        // ExternalPropertyType handling above, for the same reason: no
        // single JSON-native representation for an arbitrary byte blob.
        auto blob = ref.AsBlob();
        return toHex(blob.data(), blob.size());
    } else if (ref.IsMap()) {
        auto m = ref.AsMap();
        auto keys = m.Keys();
        auto vals = m.Values();
        auto obj = nlohmann::json::object();
        for (size_t i = 0; i < keys.size(); ++i) {
            obj[keys[i].AsKey()] = flexToJson(vals[i]);
        }
        return obj;
    } else if (ref.IsTypedVector()) {
        return flexVectorToJson(ref.AsTypedVector());
    } else if (ref.IsFixedTypedVector()) {
        return flexVectorToJson(ref.AsFixedTypedVector());
    } else if (ref.IsVector()) {
        return flexVectorToJson(ref.AsVector());
    }
    return nullptr;  // Unrecognized FlexBuffers type — treat as absent.
}

// Flex properties are stored on the wire as a plain ByteVector containing
// an embedded, independently-encoded FlexBuffers value (a different,
// dynamic/self-describing format — see docs/BACKLOG.md). The outer
// flatbuffers::Verifier only bounds-checks that ByteVector itself; the
// bytes inside it need their own verification, which FlexBuffers provides
// as flexbuffers::VerifyBuffer().
void decodeFlex(const Table* t, Verifier& v, flatbuffers::voffset_t slot,
               const std::string& name, nlohmann::json& out) {
    if (!t->CheckField(slot)) return;
    if (!t->VerifyOffset(v, slot)) {
        throw std::runtime_error("corrupt record: bad vector offset '" + name + "'");
    }
    const auto* vec = t->GetPointer<const flatbuffers::Vector<uint8_t>*>(slot);
    if (!v.VerifyVector(vec)) {
        throw std::runtime_error("corrupt record: bad Flex byte vector '" + name + "'");
    }
    if (vec == nullptr) return;

    if (!flexbuffers::VerifyBuffer(vec->Data(), vec->size())) {
        throw std::runtime_error("corrupt record: invalid FlexBuffers contents in '" + name + "'");
    }
    out[name] = flexToJson(flexbuffers::GetRoot(vec->Data(), vec->size()));
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
            case PropertyType::Byte:
                if (prop.isUnsigned) decodeScalar<uint8_t>(table, verifier, slot, prop.name, out);
                else                 decodeScalar<int8_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::Short:
                if (prop.isUnsigned) decodeScalar<uint16_t>(table, verifier, slot, prop.name, out);
                else                 decodeScalar<int16_t>(table, verifier, slot, prop.name, out);
                break;
            // Unicode code unit — unsigned by convention, see PropertyType::Char doc comment.
            case PropertyType::Char:   decodeScalar<uint16_t>(table, verifier, slot, prop.name, out); break;
            case PropertyType::Int:
                if (prop.isUnsigned) decodeScalar<uint32_t>(table, verifier, slot, prop.name, out);
                else                 decodeScalar<int32_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::Long:
            case PropertyType::Date:
            case PropertyType::Relation:
            case PropertyType::DateNano:
                if (prop.isUnsigned) decodeScalar<uint64_t>(table, verifier, slot, prop.name, out);
                else                 decodeScalar<int64_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::Float:  decodeScalar<float>(table, verifier, slot, prop.name, out); break;
            case PropertyType::Double: decodeScalar<double>(table, verifier, slot, prop.name, out); break;
            case PropertyType::String: decodeString(table, verifier, slot, prop.name, out); break;

            case PropertyType::BoolVector: decodeBoolVector(table, verifier, slot, prop.name, out); break;
            case PropertyType::ByteVector:
                // Int128/Decimal128/Bson: opaque blob, hex string. Uuid:
                // same, but grouped as a canonical UUID string. Anything
                // else: a plain array of (signed unless UNSIGNED) bytes.
                if (prop.externalType == ExternalPropertyType::Uuid) {
                    decodeByteVectorAsBlobString(table, verifier, slot, prop.name, out, /*asUuid=*/true);
                } else if (prop.externalType == ExternalPropertyType::Int128 ||
                          prop.externalType == ExternalPropertyType::Decimal128 ||
                          prop.externalType == ExternalPropertyType::Bson) {
                    decodeByteVectorAsBlobString(table, verifier, slot, prop.name, out, /*asUuid=*/false);
                } else if (prop.isUnsigned) {
                    decodeNumericVector<uint8_t>(table, verifier, slot, prop.name, out);
                } else {
                    decodeNumericVector<int8_t>(table, verifier, slot, prop.name, out);
                }
                break;
            case PropertyType::ShortVector:
                if (prop.isUnsigned) decodeNumericVector<uint16_t>(table, verifier, slot, prop.name, out);
                else                 decodeNumericVector<int16_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::CharVector:
                decodeNumericVector<uint16_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::IntVector:
                if (prop.isUnsigned) decodeNumericVector<uint32_t>(table, verifier, slot, prop.name, out);
                else                 decodeNumericVector<int32_t>(table, verifier, slot, prop.name, out);
                break;
            case PropertyType::LongVector:
            case PropertyType::DateVector:
            case PropertyType::DateNanoVector:
                if (prop.isUnsigned) decodeNumericVector<uint64_t>(table, verifier, slot, prop.name, out);
                else                 decodeNumericVector<int64_t>(table, verifier, slot, prop.name, out);
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
                decodeFlex(table, verifier, slot, prop.name, out);
                break;
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
