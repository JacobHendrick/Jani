#include "lexer.h"

#include <string.h>

static int is_ident_start(char c) {
    return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
           (c == '_');
}

static int is_digit(char c) {
    return (c >= '0') && (c <= '9');
}

static int is_ident_part(char c) {
    return is_ident_start(c) || is_digit(c) || (c == '-');
}

static struct idl_token make(
    const struct idl_lexer *lexer,
    enum idl_token_kind kind,
    const char *text,
    size_t length
) {
    struct idl_token token;

    token.kind = kind;
    token.text = text;
    token.length = length;
    token.number = 0;
    token.line = lexer->line;
    return token;
}

void idl_lexer_init(
    struct idl_lexer *lexer,
    const char *source,
    size_t length
) {
    lexer->source = source;
    lexer->length = length;
    lexer->position = 0;
    lexer->line = 1;
}

static void skip_trivia(struct idl_lexer *lexer) {
    while (lexer->position < lexer->length) {
        char c = lexer->source[lexer->position];

        if (c == '\n') {
            lexer->line += 1;
            lexer->position += 1;
        } else if ((c == ' ') || (c == '\t') || (c == '\r')) {
            lexer->position += 1;
        } else if ((c == '/') && ((lexer->position + 1u) < lexer->length) &&
                   (lexer->source[lexer->position + 1u] == '/')) {
            while ((lexer->position < lexer->length) &&
                   (lexer->source[lexer->position] != '\n')) {
                lexer->position += 1;
            }
        } else {
            return;
        }
    }
}

static int followed_by(const struct idl_lexer *lexer, size_t ahead, char c) {
    return ((lexer->position + ahead) < lexer->length) &&
           (lexer->source[lexer->position + ahead] == c);
}

static struct idl_token lex_identifier(struct idl_lexer *lexer) {
    const char *start = lexer->source + lexer->position;
    size_t length = 0;

    while (((lexer->position + length) < lexer->length) &&
           is_ident_part(lexer->source[lexer->position + length])) {
        if ((lexer->source[lexer->position + length] == '-') &&
            followed_by(lexer, length + 1u, '>')) {
            break;
        }
        length += 1;
    }
    lexer->position += length;

    if ((length == 7u) && (memcmp(start, "syscall", 7u) == 0)) {
        return make(lexer, IDL_TOK_SYSCALL, start, length);
    }
    if ((length == 6u) && (memcmp(start, "record", 6u) == 0)) {
        return make(lexer, IDL_TOK_RECORD, start, length);
    }
    if ((length == 4u) && (memcmp(start, "size", 4u) == 0)) {
        return make(lexer, IDL_TOK_SIZE, start, length);
    }
    return make(lexer, IDL_TOK_IDENT, start, length);
}

static struct idl_token lex_number(struct idl_lexer *lexer) {
    const char *start = lexer->source + lexer->position;
    struct idl_token token;
    uint64_t value = 0;
    size_t length = 0;
    int hex = 0;

    if ((start[0] == '0') && followed_by(lexer, 1u, 'x')) {
        hex = 1;
        length = 2;
    }

    while ((lexer->position + length) < lexer->length) {
        char d = lexer->source[lexer->position + length];

        if (is_digit(d)) {
            value = (value * (hex ? 16u : 10u)) + (uint64_t)(d - '0');
        } else if (hex && (d >= 'a') && (d <= 'f')) {
            value = (value * 16u) + (uint64_t)(d - 'a') + 10u;
        } else if (hex && (d >= 'A') && (d <= 'F')) {
            value = (value * 16u) + (uint64_t)(d - 'A') + 10u;
        } else {
            break;
        }
        length += 1;
    }
    lexer->position += length;

    token = make(lexer, IDL_TOK_NUMBER, start, length);
    token.number = value;
    return token;
}

struct idl_token idl_lexer_next(struct idl_lexer *lexer) {
    const char *start;
    char c;

    skip_trivia(lexer);

    if (lexer->position >= lexer->length) {
        return make(lexer, IDL_TOK_EOF, lexer->source + lexer->length, 0);
    }

    start = lexer->source + lexer->position;
    c = *start;

    if (is_ident_start(c)) {
        return lex_identifier(lexer);
    }

    if (is_digit(c)) {
        return lex_number(lexer);
    }

    if ((c == '-') && followed_by(lexer, 1u, '>')) {
        lexer->position += 2;
        return make(lexer, IDL_TOK_ARROW, start, 2);
    }

    lexer->position += 1;

    switch (c) {
    case '(':
        return make(lexer, IDL_TOK_LPAREN, start, 1);
    case ')':
        return make(lexer, IDL_TOK_RPAREN, start, 1);
    case '{':
        return make(lexer, IDL_TOK_LBRACE, start, 1);
    case '}':
        return make(lexer, IDL_TOK_RBRACE, start, 1);
    case '<':
        return make(lexer, IDL_TOK_LANGLE, start, 1);
    case '>':
        return make(lexer, IDL_TOK_RANGLE, start, 1);
    case ',':
        return make(lexer, IDL_TOK_COMMA, start, 1);
    case ':':
        return make(lexer, IDL_TOK_COLON, start, 1);
    case ';':
        return make(lexer, IDL_TOK_SEMI, start, 1);
    case '?':
        return make(lexer, IDL_TOK_QUESTION, start, 1);
    case '@':
        return make(lexer, IDL_TOK_AT, start, 1);
    default:
        return make(lexer, IDL_TOK_ERROR, start, 1);
    }
}
