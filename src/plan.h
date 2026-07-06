#ifndef RANK_PLAN_H
#define RANK_PLAN_H

#include "options.h"

enum rank_plan_kind {
    RANK_PLAN_SCALAR = 0,
    RANK_PLAN_RADIX_BYTES = 1
};

struct rank_plan {
    enum rank_plan_kind kind;
    const char *reason;
};

struct rank_plan rank_plan_from_options(const struct rank_options *options);

#endif
