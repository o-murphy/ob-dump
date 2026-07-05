#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ob_dump.h"

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    char* buf = (char*)malloc((size_t)size + 1);
    if (buf == NULL) { fclose(f); return NULL; }

    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(buf); return NULL; }

    buf[size] = '\0';
    return buf;
}

static void print_usage(const char* argv0) {
    fprintf(stderr,
        "usage: %s --json   <base>.mdb <objectbox-model.json> [-o <out.json>]\n"
        "       %s --schema <objectbox-model.json> [-o <out.json>]\n"
        "       %s --fbs    <objectbox-model.json> [-o <out.fbs>]\n",
        argv0, argv0, argv0);
}

// Writes `result` to out_path if given, else stdout (with a trailing
// newline for readability on a terminal). Always calls ob_dump_free(result).
static int write_result_and_free(char* result, const char* out_path) {
    FILE* out = stdout;
    if (out_path != NULL) {
        out = fopen(out_path, "wb");
        if (out == NULL) {
            fprintf(stderr, "error: cannot open output file: %s\n", out_path);
            ob_dump_free(result);
            return 1;
        }
    }

    fwrite(result, 1, strlen(result), out);
    if (out == stdout) fputc('\n', out);
    if (out != stdout) fclose(out);

    ob_dump_free(result);
    return 0;
}

// --schema/--fbs: one positional arg (model path) + optional `-o out`.
static int run_schema_or_fbs(int argc, char** argv, int is_schema) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 2;
    }
    const char* model_path = argv[2];
    const char* out_path   = NULL;
    if (argc >= 5 && strcmp(argv[3], "-o") == 0) {
        out_path = argv[4];
    }

    char* model_json = read_file(model_path);
    if (model_json == NULL) {
        fprintf(stderr, "error: cannot read model json file: %s\n", model_path);
        return 1;
    }

    char* result = is_schema ? ob_dump_schema(model_json) : ob_dump_fbs(model_json);
    free(model_json);

    if (result == NULL) {
        fprintf(stderr, "error: %s\n", ob_dump_last_error());
        return 1;
    }
    return write_result_and_free(result, out_path);
}

// --json: two positional args (mdb path, model path) + optional `-o out`.
static int run_json(int argc, char** argv) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 2;
    }
    const char* mdb_path   = argv[2];
    const char* model_path = argv[3];
    const char* out_path   = NULL;
    if (argc >= 6 && strcmp(argv[4], "-o") == 0) {
        out_path = argv[5];
    }

    char* model_json = read_file(model_path);
    if (model_json == NULL) {
        fprintf(stderr, "error: cannot read model json file: %s\n", model_path);
        return 1;
    }

    ObDumpSource source;
    source.kind    = OB_DUMP_SOURCE_PATH;
    source.as.path = mdb_path;

    char* result = ob_dump(&source, model_json);
    free(model_json);

    if (result == NULL) {
        fprintf(stderr, "error: %s\n", ob_dump_last_error());
        return 1;
    }
    return write_result_and_free(result, out_path);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "--json") == 0)   return run_json(argc, argv);
    if (strcmp(argv[1], "--schema") == 0) return run_schema_or_fbs(argc, argv, 1);
    if (strcmp(argv[1], "--fbs") == 0)    return run_schema_or_fbs(argc, argv, 0);

    fprintf(stderr, "error: unknown mode '%s'\n", argv[1]);
    print_usage(argv[0]);
    return 2;
}
