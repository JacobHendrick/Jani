#ifndef JANI_TOOLS_HOSTED_CHECK_H
#define JANI_TOOLS_HOSTED_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/* First failure aborts the run with file:line and the failed expression;
 * successes are counted so the final summary proves tests actually ran. */
extern unsigned long checks_passed;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "CHECK FAILED %s:%d: %s\n",                     \
                    __FILE__, __LINE__, #condition);                        \
            exit(1);                                                        \
        }                                                                   \
        checks_passed++;                                                    \
    } while (0)

#endif
