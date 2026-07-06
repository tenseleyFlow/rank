#ifndef RANK_UTIL_H
#define RANK_UTIL_H

#include "options.h"

enum {
    RANK_EXIT_SUCCESS = 0,
    RANK_EXIT_DISORDER = 1,
    RANK_EXIT_SERIOUS = 2
};

void rank_diag(const struct rank_options *options, const char *message);
void rank_diagf(const struct rank_options *options, const char *format, ...);
void *rank_xmalloc(size_t size);
void *rank_xrealloc(void *ptr, size_t size);

#endif
