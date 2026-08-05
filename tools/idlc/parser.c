#include "parser.h"

#include <stdio.h>
#include <string.h>

#include "lexer.h"

struct idl_parser {
    struct idl_lexer lexer;
    struct idl_token current;
    struct idl_unit *unit;
    int failed;
};

static void fail(struct idl_parser *parser, const char *message) {
    if (parser->failed) {
        return;
    }

    parser->failed = 1;
    parser->unit->error_line = parser->current.line;
    snprintf(parser->unit->error, IDL_ERROR_MAX, "%s", message);
}

static void fail_token(
    struct idl_parser *parser,
    const char *message
) {
    char detail[IDL_ERROR_MAX];
    size_t length;

    if (parser->failed) {
        return;
    }

    length = parser->current.length;
    if (length > 32u) {
        length = 32u;
    }

    snprintf(detail, sizeof(detail), "%s near \"%.*s\"", message,
             (int)length, parser->current.text);
    fail(parser, detail);
}

static void advance(struct idl_parser *parser) {
    parser->current = idl_lexer_next(&parser->lexer);
}

static int check(const struct idl_parser *parser, enum idl_token_kind kind) {
    return parser->current.kind == kind;
}

static int accept(struct idl_parser *parser, enum idl_token_kind kind) {
    if (!check(parser, kind)) {
        return 0;
    }

    advance(parser);
    return 1;
}

static int expect(
    struct idl_parser *parser,
    enum idl_token_kind kind,
    const char *message
) {
    if (!accept(parser, kind)) {
        fail_token(parser, message);
        return 0;
    }

    return 1;
}

static int is_name_token(enum idl_token_kind kind) {
    return (kind == IDL_TOK_IDENT) || (kind == IDL_TOK_SIZE) ||
           (kind == IDL_TOK_SYSCALL) || (kind == IDL_TOK_RECORD);
}

static int expect_name(
    struct idl_parser *parser,
    struct idl_token *out,
    const char *message
) {
    if (!is_name_token(parser->current.kind)) {
        fail_token(parser, message);
        return 0;
    }

    *out = parser->current;
    advance(parser);
    return 1;
}

static int token_matches(const struct idl_token *token, const char *text) {
    size_t length = strlen(text);

    return (token->length == length) &&
           (memcmp(token->text, text, length) == 0);
}

static int copy_name(
    struct idl_parser *parser,
    const struct idl_token *token,
    char *out
) {
    if (token->length >= (size_t)IDL_NAME_MAX) {
        fail(parser, "identifier is too long");
        return 0;
    }

    memcpy(out, token->text, token->length);
    out[token->length] = '\0';
    return 1;
}

static int parse_type(struct idl_parser *parser, enum idl_type *out) {
    struct idl_token token = parser->current;

    if (!expect(parser, IDL_TOK_IDENT, "expected a type")) {
        return 0;
    }

    if (token_matches(&token, "i32")) {
        *out = IDL_TYPE_I32;
        return 1;
    }
    if (token_matches(&token, "u32")) {
        *out = IDL_TYPE_U32;
        return 1;
    }
    if (token_matches(&token, "i64")) {
        *out = IDL_TYPE_I64;
        return 1;
    }
    if (token_matches(&token, "u64")) {
        *out = IDL_TYPE_U64;
        return 1;
    }
    if (token_matches(&token, "type-id")) {
        *out = IDL_TYPE_TYPE_ID;
        return 1;
    }
    if (token_matches(&token, "object-ref")) {
        *out = IDL_TYPE_OBJECT_REF;
        return 1;
    }

    if (token_matches(&token, "cap")) {
        *out = accept(parser, IDL_TOK_QUESTION) ? IDL_TYPE_CAP_OPT
                                                : IDL_TYPE_CAP;
        return 1;
    }

    if (token_matches(&token, "slice")) {
        struct idl_token inner;

        if (!expect(parser, IDL_TOK_LANGLE, "expected < after slice")) {
            return 0;
        }
        inner = parser->current;
        if (!expect(parser, IDL_TOK_IDENT, "expected an element type")) {
            return 0;
        }
        if (!token_matches(&inner, "u8")) {
            fail(parser, "slice supports only u8 in IDL v1");
            return 0;
        }
        if (!expect(parser, IDL_TOK_RANGLE, "expected > after slice element")) {
            return 0;
        }
        *out = IDL_TYPE_SLICE_U8;
        return 1;
    }

    if (token_matches(&token, "ptr")) {
        struct idl_token inner;

        if (!expect(parser, IDL_TOK_LANGLE, "expected < after ptr")) {
            return 0;
        }
        inner = parser->current;
        if (!expect(parser, IDL_TOK_IDENT, "expected a pointee type")) {
            return 0;
        }
        if (!token_matches(&inner, "i32")) {
            fail(parser, "ptr supports only i32 in IDL v1");
            return 0;
        }
        if (!expect(parser, IDL_TOK_RANGLE, "expected > after ptr pointee")) {
            return 0;
        }
        *out = IDL_TYPE_PTR_I32;
        return 1;
    }

    parser->current = token;
    fail_token(parser, "unknown type");
    return 0;
}

static int parse_syscall(struct idl_parser *parser) {
    struct idl_unit *unit = parser->unit;
    struct idl_syscall *syscall;
    struct idl_token name;

    if (unit->syscall_count >= (size_t)IDL_SYSCALLS_MAX) {
        fail(parser, "too many syscalls");
        return 0;
    }

    syscall = &unit->syscalls[unit->syscall_count];
    syscall->param_count = 0;
    syscall->result = IDL_TYPE_VOID;

    if (!expect_name(parser, &name, "expected a syscall name")) {
        return 0;
    }
    if (!copy_name(parser, &name, syscall->name)) {
        return 0;
    }

    if (!expect(parser, IDL_TOK_LPAREN, "expected ( after the syscall name")) {
        return 0;
    }

    if (!check(parser, IDL_TOK_RPAREN)) {
        do {
            struct idl_param *param;
            struct idl_token param_name;

            if (syscall->param_count >= (size_t)IDL_PARAMS_MAX) {
                fail(parser, "too many parameters");
                return 0;
            }

            param = &syscall->params[syscall->param_count];

            if (!expect_name(parser, &param_name,
                             "expected a parameter name")) {
                return 0;
            }
            if (!copy_name(parser, &param_name, param->name)) {
                return 0;
            }
            if (!expect(parser, IDL_TOK_COLON,
                        "expected : after the parameter name")) {
                return 0;
            }
            if (!parse_type(parser, &param->type)) {
                return 0;
            }

            syscall->param_count += 1;
        } while (accept(parser, IDL_TOK_COMMA));
    }

    if (!expect(parser, IDL_TOK_RPAREN, "expected ) after the parameters")) {
        return 0;
    }

    if (accept(parser, IDL_TOK_ARROW)) {
        if (!parse_type(parser, &syscall->result)) {
            return 0;
        }
        if ((syscall->result != IDL_TYPE_I32) &&
            (syscall->result != IDL_TYPE_I64)) {
            fail(parser, "a syscall must return i32 or i64");
            return 0;
        }
    }

    if (!expect(parser, IDL_TOK_SEMI, "expected ; after the syscall")) {
        return 0;
    }

    unit->syscall_count += 1;
    return 1;
}

static int parse_record(struct idl_parser *parser) {
    struct idl_unit *unit = parser->unit;
    struct idl_record *record;
    struct idl_token name;
    struct idl_token size;

    if (unit->record_count >= (size_t)IDL_RECORDS_MAX) {
        fail(parser, "too many records");
        return 0;
    }

    record = &unit->records[unit->record_count];
    record->field_count = 0;

    if (!expect_name(parser, &name, "expected a record name")) {
        return 0;
    }
    if (!copy_name(parser, &name, record->name)) {
        return 0;
    }

    if (!expect(parser, IDL_TOK_SIZE, "expected size after the record name")) {
        return 0;
    }
    size = parser->current;
    if (!expect(parser, IDL_TOK_NUMBER, "expected a record size")) {
        return 0;
    }
    record->size = size.number;

    if (!expect(parser, IDL_TOK_LBRACE, "expected { after the record size")) {
        return 0;
    }

    while (!check(parser, IDL_TOK_RBRACE)) {
        struct idl_field *field;
        struct idl_token field_name;
        struct idl_token offset;

        if (check(parser, IDL_TOK_EOF)) {
            fail(parser, "unterminated record");
            return 0;
        }
        if (record->field_count >= (size_t)IDL_FIELDS_MAX) {
            fail(parser, "too many fields");
            return 0;
        }

        field = &record->fields[record->field_count];

        if (!expect_name(parser, &field_name, "expected a field name")) {
            return 0;
        }
        if (!copy_name(parser, &field_name, field->name)) {
            return 0;
        }
        if (!expect(parser, IDL_TOK_COLON, "expected : after the field name")) {
            return 0;
        }
        if (!parse_type(parser, &field->type)) {
            return 0;
        }
        if (!expect(parser, IDL_TOK_AT, "expected @ before the field offset")) {
            return 0;
        }
        offset = parser->current;
        if (!expect(parser, IDL_TOK_NUMBER, "expected a field offset")) {
            return 0;
        }
        field->offset = offset.number;
        if (!expect(parser, IDL_TOK_SEMI, "expected ; after the field")) {
            return 0;
        }

        record->field_count += 1;
    }

    if (!expect(parser, IDL_TOK_RBRACE, "expected } to close the record")) {
        return 0;
    }

    unit->record_count += 1;
    return 1;
}

int idl_parse(struct idl_unit *unit, const char *source, size_t length) {
    struct idl_parser parser;

    unit->syscall_count = 0;
    unit->record_count = 0;
    unit->error[0] = '\0';
    unit->error_line = 0;

    parser.unit = unit;
    parser.failed = 0;
    idl_lexer_init(&parser.lexer, source, length);
    advance(&parser);

    while (!check(&parser, IDL_TOK_EOF)) {
        if (accept(&parser, IDL_TOK_SYSCALL)) {
            if (!parse_syscall(&parser)) {
                return 0;
            }
        } else if (accept(&parser, IDL_TOK_RECORD)) {
            if (!parse_record(&parser)) {
                return 0;
            }
        } else {
            fail_token(&parser, "expected syscall or record");
            return 0;
        }
    }

    return 1;
}
