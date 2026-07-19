#ifndef RANK_RADIX_H
#define RANK_RADIX_H

#include <stdbool.h>

#include "line.h"

struct rank_radix_stats {
    size_t passes;
    size_t classified;
    size_t insertion_sorts;
};

bool rank_radix_sort_lines(struct rank_lines *lines, const struct rank_options *options, struct rank_radix_stats *stats);
bool rank_radix_sort_key(struct rank_lines *lines, const struct rank_options *options, struct rank_radix_stats *stats);
bool rank_radix_sort_transformed_lines(struct rank_lines *lines, const struct rank_options *options, struct rank_radix_stats *stats);
bool rank_radix_sort_transformed_key(struct rank_lines *lines, const struct rank_options *options, struct rank_radix_stats *stats);

#endif
