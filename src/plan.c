#include "plan.h"

struct rank_plan
rank_plan_from_options(const struct rank_options *options)
{
    struct rank_plan plan;

    (void)options;
    plan.kind = RANK_PLAN_UNIMPLEMENTED;
    return plan;
}
