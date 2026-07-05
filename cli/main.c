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

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <base>.mdb <objectbox-model.json> [-o <out.json>]\n", argv[0]);
        return 2;
    }

    const char* mdb_path   = argv[1];
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

    ObDumpSource source;
    source.kind    = OB_DUMP_SOURCE_PATH;
    source.as.path = mdb_path;

    char* result = ob_dump(&source, model_json);
    free(model_json);

    if (result == NULL) {
        fprintf(stderr, "error: %s\n", ob_dump_last_error());
        return 1;
    }

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
