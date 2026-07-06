#include "ob_dump.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <string>

#include "internal/dumper.hpp"
#include "internal/fbs_gen.hpp"
#include "internal/schema_json.hpp"

// Normally supplied by CMakeLists.txt (OB_DUMP_VERSION cache variable,
// overridden by the release workflow from the git tag) — this fallback is
// only for compiling this file outside that build system.
#ifndef OB_DUMP_VERSION
#define OB_DUMP_VERSION "0.1.0-alpha.1"
#endif

namespace {
// thread_local so concurrent callers on different threads don't clobber
// each other's error message; ob_dump_last_error()'s contract only promises
// validity "until the next ob_dump()/ob_dump_schema()/ob_dump_fbs() call on
// the same thread".
thread_local std::string g_lastError;

// Shared by all three public entry points: run `body`, malloc+copy its
// std::string result into a C string, and translate any thrown exception
// into a NULL return + g_lastError message instead of letting it cross the
// FFI boundary (which is undefined behavior for non-C++ callers).
char* runCatchingErrors(const std::function<std::string()>& body) {
    try {
        std::string result = body();
        char* out = static_cast<char*>(std::malloc(result.size() + 1));
        if (out == nullptr) {
            g_lastError = "ob_dump: out of memory";
            return nullptr;
        }
        std::memcpy(out, result.data(), result.size());
        out[result.size()] = '\0';
        return out;
    } catch (const std::exception& e) {
        g_lastError = e.what();
        return nullptr;
    } catch (...) {
        g_lastError = "ob_dump: unknown error";
        return nullptr;
    }
}
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

    const char* path = source->as.path;
    return runCatchingErrors([&] { return ob_dump_internal::dumpToJson(path, model_json); });
}

int ob_dump_stream(const ObDumpSource* source, const char* model_json,
                   ObDumpRecordCallback callback, void* user_data) {
    if (source == nullptr || model_json == nullptr || callback == nullptr) {
        g_lastError = "ob_dump_stream: source, model_json, and callback must not be null";
        return -1;
    }
    if (source->kind != OB_DUMP_SOURCE_PATH) {
        g_lastError =
            "ob_dump_stream: OB_DUMP_SOURCE_BUFFER is not implemented yet (see docs/BACKLOG.md)";
        return -1;
    }
    if (source->as.path == nullptr) {
        g_lastError = "ob_dump_stream: source path must not be null";
        return -1;
    }

    try {
        ob_dump_internal::dumpStreaming(
            source->as.path, model_json,
            [&](const std::string& entityName, uint32_t objectId, const std::string& fieldsJson) -> bool {
                return callback(entityName.c_str(), static_cast<int64_t>(objectId),
                                fieldsJson.c_str(), user_data) == 0;
            });
        return 0;
    } catch (const std::exception& e) {
        g_lastError = e.what();
        return -1;
    } catch (...) {
        g_lastError = "ob_dump_stream: unknown error";
        return -1;
    }
}

char* ob_dump_schema(const char* model_json) {
    if (model_json == nullptr) {
        g_lastError = "ob_dump_schema: model_json must not be null";
        return nullptr;
    }
    return runCatchingErrors([&] { return ob_dump_internal::schemaToJson(model_json); });
}

char* ob_dump_fbs(const char* model_json) {
    if (model_json == nullptr) {
        g_lastError = "ob_dump_fbs: model_json must not be null";
        return nullptr;
    }
    return runCatchingErrors([&] { return ob_dump_internal::generateFbs(model_json); });
}

void ob_dump_free(char* json) {
    std::free(json);
}

const char* ob_dump_last_error(void) {
    return g_lastError.c_str();
}

const char* ob_dump_version(void) {
    return OB_DUMP_VERSION;
}

}  // extern "C"
