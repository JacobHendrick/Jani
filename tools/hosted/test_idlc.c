#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../idlc/lexer.h"
#include "../idlc/parser.h"
#include "../idlc/emit.h"
#include "check.h"

unsigned long checks_passed;

static struct idl_token lex_one(struct idl_lexer *lexer) {
    return idl_lexer_next(lexer);
}

static int token_is(struct idl_token token, const char *text) {
    return (strlen(text) == token.length) &&
           (memcmp(token.text, text, token.length) == 0);
}

static void test_lexes_a_syscall_declaration(void) {
    const char source[] = "syscall self() -> i32;";
    struct idl_lexer lexer;
    struct idl_token name;
    struct idl_token type;

    idl_lexer_init(&lexer, source, sizeof(source) - 1u);

    CHECK(lex_one(&lexer).kind == IDL_TOK_SYSCALL);

    name = lex_one(&lexer);
    CHECK(name.kind == IDL_TOK_IDENT);
    CHECK(token_is(name, "self"));

    CHECK(lex_one(&lexer).kind == IDL_TOK_LPAREN);
    CHECK(lex_one(&lexer).kind == IDL_TOK_RPAREN);
    CHECK(lex_one(&lexer).kind == IDL_TOK_ARROW);

    type = lex_one(&lexer);
    CHECK(type.kind == IDL_TOK_IDENT);
    CHECK(token_is(type, "i32"));

    CHECK(lex_one(&lexer).kind == IDL_TOK_SEMI);
    CHECK(lex_one(&lexer).kind == IDL_TOK_EOF);
}

static void test_lexes_hyphenated_identifiers(void) {
    const char source[] = "type-id object-ref";
    struct idl_lexer lexer;
    struct idl_token first;
    struct idl_token second;

    idl_lexer_init(&lexer, source, sizeof(source) - 1u);

    first = lex_one(&lexer);
    CHECK(first.kind == IDL_TOK_IDENT);
    CHECK(token_is(first, "type-id"));

    second = lex_one(&lexer);
    CHECK(second.kind == IDL_TOK_IDENT);
    CHECK(token_is(second, "object-ref"));

    CHECK(lex_one(&lexer).kind == IDL_TOK_EOF);
}

static void test_arrow_is_not_an_identifier(void) {
    const char source[] = "cap? -> slice<u8>";
    struct idl_lexer lexer;

    idl_lexer_init(&lexer, source, sizeof(source) - 1u);

    CHECK(lex_one(&lexer).kind == IDL_TOK_IDENT);
    CHECK(lex_one(&lexer).kind == IDL_TOK_QUESTION);
    CHECK(lex_one(&lexer).kind == IDL_TOK_ARROW);
    CHECK(lex_one(&lexer).kind == IDL_TOK_IDENT);
    CHECK(lex_one(&lexer).kind == IDL_TOK_LANGLE);
    CHECK(lex_one(&lexer).kind == IDL_TOK_IDENT);
    CHECK(lex_one(&lexer).kind == IDL_TOK_RANGLE);
    CHECK(lex_one(&lexer).kind == IDL_TOK_EOF);
}

static void test_identifier_does_not_swallow_an_abutting_arrow(void) {
    const char source[] = "self()->i32 type-id->x";
    struct idl_lexer lexer;
    struct idl_token ident;

    idl_lexer_init(&lexer, source, sizeof(source) - 1u);

    ident = lex_one(&lexer);
    CHECK(ident.kind == IDL_TOK_IDENT);
    CHECK(token_is(ident, "self"));
    CHECK(lex_one(&lexer).kind == IDL_TOK_LPAREN);
    CHECK(lex_one(&lexer).kind == IDL_TOK_RPAREN);
    CHECK(lex_one(&lexer).kind == IDL_TOK_ARROW);

    ident = lex_one(&lexer);
    CHECK(ident.kind == IDL_TOK_IDENT);
    CHECK(token_is(ident, "i32"));

    ident = lex_one(&lexer);
    CHECK(ident.kind == IDL_TOK_IDENT);
    CHECK(token_is(ident, "type-id"));
    CHECK(lex_one(&lexer).kind == IDL_TOK_ARROW);

    ident = lex_one(&lexer);
    CHECK(ident.kind == IDL_TOK_IDENT);
    CHECK(token_is(ident, "x"));
}

static void test_lexes_numbers_and_offsets(void) {
    const char source[] = "@ 16 size 72";
    struct idl_lexer lexer;
    struct idl_token sixteen;
    struct idl_token seventy_two;

    idl_lexer_init(&lexer, source, sizeof(source) - 1u);

    CHECK(lex_one(&lexer).kind == IDL_TOK_AT);

    sixteen = lex_one(&lexer);
    CHECK(sixteen.kind == IDL_TOK_NUMBER);
    CHECK(sixteen.number == 16u);

    CHECK(lex_one(&lexer).kind == IDL_TOK_SIZE);

    seventy_two = lex_one(&lexer);
    CHECK(seventy_two.kind == IDL_TOK_NUMBER);
    CHECK(seventy_two.number == 72u);
}

static void test_lexes_hex_numbers(void) {
    const char source[] = "0x4A414E495F524F54 0";
    struct idl_lexer lexer;
    struct idl_token magic;
    struct idl_token zero;

    idl_lexer_init(&lexer, source, sizeof(source) - 1u);

    magic = lex_one(&lexer);
    CHECK(magic.kind == IDL_TOK_NUMBER);
    CHECK(magic.number == UINT64_C(0x4A414E495F524F54));

    zero = lex_one(&lexer);
    CHECK(zero.kind == IDL_TOK_NUMBER);
    CHECK(zero.number == 0u);
}

static void test_skips_comments_and_tracks_lines(void) {
    const char source[] = "// a comment\nsyscall\n// another\nexit";
    struct idl_lexer lexer;
    struct idl_token first;
    struct idl_token second;

    idl_lexer_init(&lexer, source, sizeof(source) - 1u);

    first = lex_one(&lexer);
    CHECK(first.kind == IDL_TOK_SYSCALL);
    CHECK(first.line == 2u);

    second = lex_one(&lexer);
    CHECK(second.kind == IDL_TOK_IDENT);
    CHECK(second.line == 4u);
}

static void test_reports_unknown_punctuation(void) {
    const char source[] = "syscall $ self";
    struct idl_lexer lexer;

    idl_lexer_init(&lexer, source, sizeof(source) - 1u);

    CHECK(lex_one(&lexer).kind == IDL_TOK_SYSCALL);
    CHECK(lex_one(&lexer).kind == IDL_TOK_ERROR);
}

static void test_empty_source_is_immediately_eof(void) {
    const char source[] = "";
    struct idl_lexer lexer;

    idl_lexer_init(&lexer, source, 0);

    CHECK(lex_one(&lexer).kind == IDL_TOK_EOF);
    CHECK(lex_one(&lexer).kind == IDL_TOK_EOF);
}

static void test_parses_a_syscall_with_a_slice(void) {
    const char source[] = "syscall log(message: slice<u8>) -> i32;";
    struct idl_unit unit;

    CHECK(idl_parse(&unit, source, sizeof(source) - 1u) == 1);
    CHECK(unit.syscall_count == 1u);
    CHECK(strcmp(unit.syscalls[0].name, "log") == 0);
    CHECK(unit.syscalls[0].param_count == 1u);
    CHECK(unit.syscalls[0].params[0].type == IDL_TYPE_SLICE_U8);
    CHECK(strcmp(unit.syscalls[0].params[0].name, "message") == 0);
    CHECK(unit.syscalls[0].result == IDL_TYPE_I32);
}

static void test_parses_every_type(void) {
    const char source[] =
        "syscall a(t: type-id, s: u32) -> i32;"
        "syscall b(c: cap, o: u32, buf: slice<u8>) -> i64;"
        "syscall c(x: cap?, p: ptr<i32>, d: u64) -> i32;"
        "syscall d(code: i32);";
    struct idl_unit unit;

    CHECK(idl_parse(&unit, source, sizeof(source) - 1u) == 1);
    CHECK(unit.syscall_count == 4u);
    CHECK(unit.syscalls[0].params[0].type == IDL_TYPE_TYPE_ID);
    CHECK(unit.syscalls[0].params[1].type == IDL_TYPE_U32);
    CHECK(unit.syscalls[1].params[0].type == IDL_TYPE_CAP);
    CHECK(unit.syscalls[1].params[2].type == IDL_TYPE_SLICE_U8);
    CHECK(unit.syscalls[1].result == IDL_TYPE_I64);
    CHECK(unit.syscalls[2].params[0].type == IDL_TYPE_CAP_OPT);
    CHECK(unit.syscalls[2].params[1].type == IDL_TYPE_PTR_I32);
    CHECK(unit.syscalls[2].params[2].type == IDL_TYPE_U64);
    CHECK(unit.syscalls[3].result == IDL_TYPE_VOID);
    CHECK(unit.syscalls[3].param_count == 1u);
}

static void test_parses_a_record(void) {
    const char source[] =
        "record component_root_record size 72 {\n"
        "    magic: u64 @ 0;\n"
        "    module_id: object-ref @ 16;\n"
        "}";
    struct idl_unit unit;

    CHECK(idl_parse(&unit, source, sizeof(source) - 1u) == 1);
    CHECK(unit.record_count == 1u);
    CHECK(strcmp(unit.records[0].name, "component_root_record") == 0);
    CHECK(unit.records[0].size == 72u);
    CHECK(unit.records[0].field_count == 2u);
    CHECK(strcmp(unit.records[0].fields[0].name, "magic") == 0);
    CHECK(unit.records[0].fields[0].type == IDL_TYPE_U64);
    CHECK(unit.records[0].fields[0].offset == 0u);
    CHECK(unit.records[0].fields[1].type == IDL_TYPE_OBJECT_REF);
    CHECK(unit.records[0].fields[1].offset == 16u);
}

static void test_parses_syscalls_and_records_together(void) {
    const char source[] =
        "syscall self() -> i32;\n"
        "record h size 8 { magic: u64 @ 0; }\n"
        "syscall exit(code: i32);\n";
    struct idl_unit unit;

    CHECK(idl_parse(&unit, source, sizeof(source) - 1u) == 1);
    CHECK(unit.syscall_count == 2u);
    CHECK(unit.record_count == 1u);
}

static void test_keywords_are_contextual_and_usable_as_names(void) {
    const char source[] =
        "syscall object_create(type: type-id, size: u32) -> i32;"
        "record record size 16 { size: u64 @ 0; syscall: u64 @ 8; }";
    struct idl_unit unit;

    CHECK(idl_parse(&unit, source, sizeof(source) - 1u) == 1);
    CHECK(unit.syscall_count == 1u);
    CHECK(unit.syscalls[0].param_count == 2u);
    CHECK(strcmp(unit.syscalls[0].params[1].name, "size") == 0);
    CHECK(unit.syscalls[0].params[1].type == IDL_TYPE_U32);
    CHECK(unit.record_count == 1u);
    CHECK(strcmp(unit.records[0].name, "record") == 0);
    CHECK(strcmp(unit.records[0].fields[0].name, "size") == 0);
    CHECK(strcmp(unit.records[0].fields[1].name, "syscall") == 0);
}

static void test_rejects_unknown_types_and_bad_syntax(void) {
    struct idl_unit unit;
    const char bad_type[] = "syscall a(x: f32) -> i32;";
    const char missing_semi[] = "syscall a() -> i32";
    const char bad_keyword[] = "nonsense a() -> i32;";
    const char missing_colon[] = "syscall a(x u32) -> i32;";
    const char bad_slice[] = "syscall a(x: slice<u16>) -> i32;";
    const char record_no_size[] = "record h { magic: u64 @ 0; }";

    CHECK(idl_parse(&unit, bad_type, sizeof(bad_type) - 1u) == 0);
    CHECK(unit.error[0] != '\0');
    CHECK(unit.error_line == 1u);
    CHECK(idl_parse(&unit, missing_semi, sizeof(missing_semi) - 1u) == 0);
    CHECK(idl_parse(&unit, bad_keyword, sizeof(bad_keyword) - 1u) == 0);
    CHECK(idl_parse(&unit, missing_colon, sizeof(missing_colon) - 1u) == 0);
    CHECK(idl_parse(&unit, bad_slice, sizeof(bad_slice) - 1u) == 0);
    CHECK(idl_parse(&unit, record_no_size, sizeof(record_no_size) - 1u) == 0);
}

static void test_reports_the_line_of_the_error(void) {
    const char source[] =
        "syscall a() -> i32;\n"
        "syscall b() -> i32;\n"
        "syscall c(x: f32) -> i32;\n";
    struct idl_unit unit;

    CHECK(idl_parse(&unit, source, sizeof(source) - 1u) == 0);
    CHECK(unit.error_line == 3u);
}

static void test_rejects_overlong_names(void) {
    char source[512];
    struct idl_unit unit;
    size_t index;

    memcpy(source, "syscall ", 8u);
    for (index = 0; index < 200u; index++) {
        source[8u + index] = 'a';
    }
    memcpy(source + 208u, "() -> i32;", 10u);

    CHECK(idl_parse(&unit, source, 218u) == 0);
    CHECK(unit.error[0] != '\0');
}

static void check_signature(const char *declaration, const char *expected) {
    struct idl_unit unit;
    char signature[32];

    CHECK(idl_parse(&unit, declaration, strlen(declaration)) == 1);
    CHECK(idl_wamr_signature(&unit.syscalls[0], signature,
                             sizeof(signature)) == 1);
    CHECK(strcmp(signature, expected) == 0);
}

static void test_lowering_reproduces_the_existing_table(void) {
    check_signature("syscall log(m: slice<u8>) -> i32;", "(ii)i");
    check_signature("syscall object_create(t: type-id, s: u32) -> i32;",
                    "(IIi)i");
    check_signature(
        "syscall object_read(c: cap, o: u32, b: slice<u8>) -> i32;",
        "(iiii)i");
    check_signature(
        "syscall object_write(c: cap, o: u32, b: slice<u8>) -> i32;",
        "(iiii)i");
    check_signature("syscall object_size(c: cap) -> i64;", "(i)I");
    check_signature("syscall cap_drop(c: cap) -> i32;", "(i)i");
    check_signature(
        "syscall message_send(t: cap, p: slice<u8>, c: cap?) -> i32;",
        "(iiii)i");
    check_signature(
        "syscall message_recv(b: slice<u8>, c: ptr<i32>) -> i32;",
        "(iii)i");
    check_signature("syscall timer_set(d: u64) -> i32;", "(I)i");
    check_signature("syscall time_logical() -> i64;", "()I");
    check_signature("syscall self() -> i32;", "()i");
    check_signature("syscall exit(code: i32);", "(i)");
}

static void test_signature_reports_overflow(void) {
    struct idl_unit unit;
    const char source[] = "syscall a(b: slice<u8>, c: slice<u8>) -> i32;";
    char tiny[4];

    CHECK(idl_parse(&unit, source, sizeof(source) - 1u) == 1);
    CHECK(idl_wamr_signature(&unit.syscalls[0], tiny, sizeof(tiny)) == 0);
}

static void test_writer_flags_overflow_instead_of_truncating(void) {
    char buffer[8];
    struct idl_writer writer = { buffer, sizeof(buffer), 0, 0 };

    idl_write(&writer, "%s", "short");
    CHECK(writer.overflowed == 0);
    idl_write(&writer, "%s", "much too long to fit");
    CHECK(writer.overflowed == 1);
    CHECK(writer.length < sizeof(buffer));
}

static void test_table_emitter_names_the_impl(void) {
    const char source[] = "syscall self() -> i32;";
    struct idl_unit unit;
    char buffer[4096];
    struct idl_writer writer = { buffer, sizeof(buffer), 0, 0 };

    CHECK(idl_parse(&unit, source, sizeof(source) - 1u) == 1);
    CHECK(idl_emit_table(&unit, &writer) == 1);
    CHECK(writer.overflowed == 0);
    CHECK(strstr(buffer, "\"jani_self\"") != NULL);
    CHECK(strstr(buffer, "jani_self_impl") != NULL);
    CHECK(strstr(buffer, "\"()i\"") != NULL);
    CHECK(strstr(buffer, "do not edit") != NULL);
    CHECK(strstr(buffer, "NativeSymbol jani_symbols[]") != NULL);
}

int main(void) {
    checks_passed = 0;

    test_lexes_a_syscall_declaration();
    test_lexes_hyphenated_identifiers();
    test_arrow_is_not_an_identifier();
    test_identifier_does_not_swallow_an_abutting_arrow();
    test_lexes_numbers_and_offsets();
    test_lexes_hex_numbers();
    test_skips_comments_and_tracks_lines();
    test_reports_unknown_punctuation();
    test_empty_source_is_immediately_eof();

    test_parses_a_syscall_with_a_slice();
    test_parses_every_type();
    test_parses_a_record();
    test_parses_syscalls_and_records_together();
    test_keywords_are_contextual_and_usable_as_names();
    test_rejects_unknown_types_and_bad_syntax();
    test_reports_the_line_of_the_error();
    test_rejects_overlong_names();

    test_lowering_reproduces_the_existing_table();
    test_signature_reports_overflow();
    test_writer_flags_overflow_instead_of_truncating();
    test_table_emitter_names_the_impl();

    printf("test_idlc: %lu checks passed\n", checks_passed);
    return 0;
}
