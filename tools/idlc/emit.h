#ifndef JANI_TOOLS_IDLC_EMIT_H
#define JANI_TOOLS_IDLC_EMIT_H

#include "ast.h"

struct idl_writer {
    char *buffer;
    size_t capacity;
    size_t length;
    int overflowed;
};

void idl_write(struct idl_writer *writer, const char *format, ...);

int idl_wamr_signature(
    const struct idl_syscall *syscall,
    char *out,
    size_t capacity
);

const char *idl_type_name(enum idl_type type);

int idl_emit_table(const struct idl_unit *unit, struct idl_writer *writer);

int idl_emit_zig(const struct idl_unit *unit, struct idl_writer *writer);

int idl_emit_c(const struct idl_unit *unit, struct idl_writer *writer);

int idl_emit_conform(const struct idl_unit *unit, struct idl_writer *writer);

#endif
