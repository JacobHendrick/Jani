#ifndef JANI_TOOLS_IDLC_AST_H
#define JANI_TOOLS_IDLC_AST_H

#include <stddef.h>
#include <stdint.h>

#define IDL_NAME_MAX 64
#define IDL_PARAMS_MAX 8
#define IDL_SYSCALLS_MAX 32
#define IDL_FIELDS_MAX 16
#define IDL_RECORDS_MAX 16
#define IDL_ERROR_MAX 256

enum idl_type {
    IDL_TYPE_I32,
    IDL_TYPE_U32,
    IDL_TYPE_I64,
    IDL_TYPE_U64,
    IDL_TYPE_SLICE_U8,
    IDL_TYPE_SLICE_U8_OUT,
    IDL_TYPE_CAP,
    IDL_TYPE_CAP_OPT,
    IDL_TYPE_PTR_I32,
    IDL_TYPE_TYPE_ID,
    IDL_TYPE_OBJECT_REF,
    IDL_TYPE_VOID
};

struct idl_param {
    char name[IDL_NAME_MAX];
    enum idl_type type;
};

struct idl_syscall {
    char name[IDL_NAME_MAX];
    struct idl_param params[IDL_PARAMS_MAX];
    size_t param_count;
    enum idl_type result;
};

struct idl_field {
    char name[IDL_NAME_MAX];
    enum idl_type type;
    uint64_t offset;
};

struct idl_record {
    char name[IDL_NAME_MAX];
    struct idl_field fields[IDL_FIELDS_MAX];
    size_t field_count;
    uint64_t size;
};

struct idl_unit {
    struct idl_syscall syscalls[IDL_SYSCALLS_MAX];
    size_t syscall_count;
    struct idl_record records[IDL_RECORDS_MAX];
    size_t record_count;
    char error[IDL_ERROR_MAX];
    unsigned error_line;
};

#endif
