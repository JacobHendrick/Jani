#ifndef JANI_TOOLS_IDLC_LEXER_H
#define JANI_TOOLS_IDLC_LEXER_H

#include <stddef.h>
#include <stdint.h>

enum idl_token_kind {
    IDL_TOK_EOF,
    IDL_TOK_IDENT,
    IDL_TOK_NUMBER,
    IDL_TOK_SYSCALL,
    IDL_TOK_RECORD,
    IDL_TOK_SIZE,
    IDL_TOK_LPAREN,
    IDL_TOK_RPAREN,
    IDL_TOK_LBRACE,
    IDL_TOK_RBRACE,
    IDL_TOK_LANGLE,
    IDL_TOK_RANGLE,
    IDL_TOK_COMMA,
    IDL_TOK_COLON,
    IDL_TOK_SEMI,
    IDL_TOK_ARROW,
    IDL_TOK_QUESTION,
    IDL_TOK_AT,
    IDL_TOK_ERROR
};

struct idl_token {
    enum idl_token_kind kind;
    const char *text;
    size_t length;
    uint64_t number;
    unsigned line;
};

struct idl_lexer {
    const char *source;
    size_t length;
    size_t position;
    unsigned line;
};

void idl_lexer_init(struct idl_lexer *lexer, const char *source, size_t length);

struct idl_token idl_lexer_next(struct idl_lexer *lexer);

#endif
