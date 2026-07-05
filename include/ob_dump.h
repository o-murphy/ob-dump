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

/* Thread-local message describing the most recent failure of ob_dump() on
   this thread. Valid until the next ob_dump() call on the same thread. */
OB_DUMP_API const char* ob_dump_last_error(void);

OB_DUMP_API const char* ob_dump_version(void);

#ifdef __cplusplus
}
#endif

#endif /* OB_DUMP_H */
