#include "ob_dump.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include "internal/dumper.hpp"

namespace {
// thread_local so concurrent callers on different threads don't clobber
// each other's error message; ob_dump_last_error()'s contract only promises
// validity "until the next ob_dump() call on the same thread".
thread_local std::string g_lastError;
}  // namespace

extern "C" {

char* ob_dump(const ObDumpSource* source, const char* model_json) {
    if (source == nullptr || model_json == nullptr) {
        g_lastError = "ob_dump: source and model_json must not be null";
        return nullptr;
    }
    if (source->kind != OB_DUMP_SOURCE_PATH) {
        g_lastError =
            "ob_dump: OB_DUMP_SOURCE_BUFFER is not implemented yet (see docs/BACKLOG.md)";
        return nullptr;
    }
    if (source->as.path == nullptr) {
        g_lastError = "ob_dump: source path must not be null";
        return nullptr;
    }

    try {
        std::string json = ob_dump_internal::dumpToJson(source->as.path, model_json);
        char* out = static_cast<char*>(std::malloc(json.size() + 1));
        if (out == nullptr) {
            g_lastError = "ob_dump: out of memory";
            return nullptr;
        }
        std::memcpy(out, json.data(), json.size());
        out[json.size()] = '\0';
        return out;
    } catch (const std::exception& e) {
        g_lastError = e.what();
        return nullptr;
    } catch (...) {
        g_lastError = "ob_dump: unknown error";
        return nullptr;
    }
}

void ob_dump_free(char* json) {
    std::free(json);
}

const char* ob_dump_last_error(void) {
    return g_lastError.c_str();
}

const char* ob_dump_version(void) {
    return "0.1.0";
}

}  // extern "C"
