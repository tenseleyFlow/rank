#ifndef RANK_PLAN_H
#define RANK_PLAN_H

#include "options.h"

enum rank_plan_kind {
    RANK_PLAN_SCALAR = 0,
    RANK_PLAN_RADIX_BYTES = 1,
    RANK_PLAN_RADIX_KEYS = 2,
    RANK_PLAN_RADIX_TRANSFORMED = 3
};

struct rank_plan {
    enum rank_plan_kind kind;
    const char *reason;
};

struct rank_plan rank_plan_from_options(const struct rank_options *options);
const char *rank_plan_name(enum rank_plan_kind kind);

#endif
