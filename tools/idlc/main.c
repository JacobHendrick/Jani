#include <stdio.h>
#include <string.h>

#include "emit.h"
#include "parser.h"

#define IDL_SOURCE_MAX 65536
#define IDL_OUTPUT_MAX 262144

struct emitter_entry {
    const char *name;
    int (*emit)(const struct idl_unit *, struct idl_writer *);
};

static const struct emitter_entry emitters[] = {
    { "zig", idl_emit_zig },
    { "c", idl_emit_c },
    { "table", idl_emit_table },
    { "conform", idl_emit_conform }
};

static char source[IDL_SOURCE_MAX];
static char output[IDL_OUTPUT_MAX];

static const char *option_value(const char *argument, const char *prefix) {
    size_t length = strlen(prefix);

    if (strncmp(argument, prefix, length) != 0) {
        return NULL;
    }

    return argument + length;
}

static int read_source(const char *path, size_t *length_out) {
    FILE *file = fopen(path, "rb");
    size_t length;

    if (file == NULL) {
        fprintf(stderr, "idlc: cannot open %s\n", path);
        return 0;
    }

    length = fread(source, 1, sizeof(source), file);

    if (!feof(file)) {
        fprintf(stderr, "idlc: %s is larger than %d bytes\n", path,
                IDL_SOURCE_MAX);
        fclose(file);
        return 0;
    }

    fclose(file);
    *length_out = length;
    return 1;
}

static int write_output(const char *path, const char *text, size_t length) {
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
        fprintf(stderr, "idlc: cannot write %s\n", path);
        return 0;
    }

    if (fwrite(text, 1, length, file) != length) {
        fprintf(stderr, "idlc: short write to %s\n", path);
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

int main(int argc, char **argv) {
    const char *emit_name = NULL;
    const char *out_path = NULL;
    const char *in_path = NULL;
    const struct emitter_entry *chosen = NULL;
    struct idl_unit unit;
    struct idl_writer writer;
    size_t length = 0;
    size_t index;
    int argument;

    for (argument = 1; argument < argc; argument++) {
        const char *value;

        value = option_value(argv[argument], "--emit=");
        if (value != NULL) {
            emit_name = value;
            continue;
        }

        value = option_value(argv[argument], "--out=");
        if (value != NULL) {
            out_path = value;
            continue;
        }

        if (in_path != NULL) {
            fprintf(stderr, "idlc: more than one input file\n");
            return 1;
        }
        in_path = argv[argument];
    }

    if ((emit_name == NULL) || (out_path == NULL) || (in_path == NULL)) {
        fprintf(stderr,
                "usage: idlc --emit=<zig|c|table|conform> --out=<path>"
                " <input.idl>\n");
        return 1;
    }

    for (index = 0; index < (sizeof(emitters) / sizeof(emitters[0]));
         index++) {
        if (strcmp(emitters[index].name, emit_name) == 0) {
            chosen = &emitters[index];
        }
    }

    if (chosen == NULL) {
        fprintf(stderr, "idlc: unknown emitter \"%s\"\n", emit_name);
        return 1;
    }

    if (!read_source(in_path, &length)) {
        return 1;
    }

    if (!idl_parse(&unit, source, length)) {
        fprintf(stderr, "%s:%u: %s\n", in_path, unit.error_line, unit.error);
        return 1;
    }

    writer.buffer = output;
    writer.capacity = sizeof(output);
    writer.length = 0;
    writer.overflowed = 0;

    if (!chosen->emit(&unit, &writer)) {
        fprintf(stderr, "idlc: %s emitter failed for %s\n", emit_name,
                in_path);
        return 1;
    }

    if (!write_output(out_path, writer.buffer, writer.length)) {
        return 1;
    }

    return 0;
}
