#ifndef RANK_CMP_H
#define RANK_CMP_H

#include <stddef.h>

#include "line.h"
#include "options.h"

struct rank_cmp_context {
    const struct rank_options *options;
    const struct rank_lines *lines;
    size_t calls;
    size_t bytes;
};

void rank_cmp_context_init(struct rank_cmp_context *ctx, const struct rank_options *options, const struct rank_lines *lines);
int rank_compare_lines(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b);
int rank_compare_lines_ascending(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b);
int rank_compare_unique(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b);

#endif
