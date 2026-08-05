#ifndef JANI_TOOLS_IDLC_PARSER_H
#define JANI_TOOLS_IDLC_PARSER_H

#include "ast.h"

int idl_parse(struct idl_unit *unit, const char *source, size_t length);

#endif
