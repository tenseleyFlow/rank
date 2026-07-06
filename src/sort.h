#ifndef RANK_SORT_H
#define RANK_SORT_H

#include <stdbool.h>

#include "line.h"
#include "options.h"
#include "plan.h"

bool rank_sort_lines(struct rank_lines *lines, const struct rank_options *options, const struct rank_plan *plan);

#endif
