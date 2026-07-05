#ifndef OB_DUMP_H
#define OB_DUMP_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  ifdef OB_DUMP_BUILD
#    define OB_DUMP_API __declspec(dllexport)
#  else
#    define OB_DUMP_API __declspec(dllimport)
#  endif
#else
#  define OB_DUMP_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ObDumpSourceKind {
    /* path to a data.mdb file; opened directly by LMDB (true zero-copy mmap) */
    OB_DUMP_SOURCE_PATH = 0,
    /* in-memory buffer holding a full data.mdb image (see docs/BACKLOG.md:
       "OB_DUMP_SOURCE_BUFFER input mode" — not yet implemented) */
    OB_DUMP_SOURCE_BUFFER = 1,
} ObDumpSourceKind;

typedef struct ObDumpBuffer {
    const uint8_t* data;
    size_t         size;
} ObDumpBuffer;

typedef struct ObDumpSource {
    ObDumpSourceKind kind;
    union {
        const char*  path; /* OB_DUMP_SOURCE_PATH */
        ObDumpBuffer buffer; /* OB_DUMP_SOURCE_BUFFER */
    } as;
} ObDumpSource;

/*
 * Reads an ObjectBox LMDB database and returns a JSON dump, shaped as
 * `{ "<EntityName>": [ { ...fields..., "id": <object_id> }, ... ], ... }`.
 *
 * model_json must be the full *contents* of objectbox-model.json (the
 * schema), not a path to it.
 *
 * Returns a NUL-terminated JSON string on success, or NULL on failure —
 * call ob_dump_last_error() for a message in that case. The returned
 * pointer must be released with ob_dump_free(), never with free()/delete
 * directly: the allocator that produced it is not guaranteed to be the same
 * one available on the caller's side of the FFI boundary.
 */
OB_DUMP_API char* ob_dump(const ObDumpSource* source, const char* model_json);

OB_DUMP_API void ob_dump_free(char* json);

/*
 * Called once per decoded record by ob_dump_stream(). `entity_name` and
 * `fields_json` are valid only for the duration of this call — copy them if
 * you need them afterwards. `fields_json` is a JSON object with that
 * record's fields (same shape as one element of ob_dump()'s per-entity
 * arrays, including "id" — object_id is also passed separately for
 * convenience).
 *
 * Return 0 to keep iterating, non-zero to stop early (not treated as an
 * error by ob_dump_stream).
 */
typedef int (*ObDumpRecordCallback)(const char* entity_name, int64_t object_id,
                                    const char* fields_json, void* user_data);

/*
 * Streaming variant of ob_dump(): invokes `callback` once per decoded
 * record instead of building the whole database as one JSON string in
 * memory first. Use this for large databases where ob_dump()'s memory
 * footprint (proportional to total data size) isn't acceptable — LMDB
 * access itself is already lazy/paged either way, this only changes
 * whether decoded records get accumulated before you see them.
 *
 * Same null-argument and OB_DUMP_SOURCE_PATH-only rules as ob_dump().
 * Returns 0 on success (including a callback-requested early stop), or
 * non-zero on failure — see ob_dump_last_error().
 */
OB_DUMP_API int ob_dump_stream(const ObDumpSource* source, const char* model_json,
                               ObDumpRecordCallback callback, void* user_data);

/*
 * Re-serializes objectbox-model.json as a clean, minimal JSON schema
 * listing (entityId/name/properties with id/name/type/vtableSlot) — no
 * ObjectBox model.json noise (uids, indexes, retired-property arrays,
 * flags). Useful on its own, and as the entityId -> table-name/shape
 * lookup needed by anyone consuming ob_dump_fbs()'s output (see ob_dump_fbs).
 *
 * Same NULL/ob_dump_last_error()/ob_dump_free() contract as ob_dump().
 */
OB_DUMP_API char* ob_dump_schema(const char* model_json);

/*
 * Generates a FlatBuffers IDL (.fbs) describing every entity in
 * objectbox-model.json, so any language's `flatc` can generate a typed
 * reader for the raw FlatBuffers table bytes this library otherwise decodes
 * dynamically at runtime. This does NOT give a consumer LMDB access or the
 * ObjectBox key-format parsing (entity_id/object_id from the 8-byte LMDB
 * key) — only the per-record decode step. Pair with ob_dump_schema() for
 * the entityId -> table dispatch a standalone consumer still needs. See
 * docs/BACKLOG.md for the full rationale.
 *
 * Same NULL/ob_dump_last_error()/ob_dump_free() contract as ob_dump().
 */
OB_DUMP_API char* ob_dump_fbs(const char* model_json);

/* Thread-local message describing the most recent failure of ob_dump()/
   ob_dump_stream()/ob_dump_schema()/ob_dump_fbs() on this thread. Valid
   until the next call to any of those on the same thread. */
OB_DUMP_API const char* ob_dump_last_error(void);

OB_DUMP_API const char* ob_dump_version(void);

#ifdef __cplusplus
}
#endif

#endif /* OB_DUMP_H */
