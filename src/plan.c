#include "plan.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool identity_collation(void);
static bool locale_is_identity_name(const char *name);

struct rank_plan
rank_plan_from_options(const struct rank_options *options)
{
    struct rank_plan plan;

    plan.kind = RANK_PLAN_SCALAR;
    plan.reason = "scalar fallback";

    if (getenv("RANK_FORCE_SCALAR") != NULL) {
        plan.reason = "forced scalar";
        return plan;
    }
    if (options->key_count > 0) {
        plan.reason = "keyed sort";
        return plan;
    }
    if (options->debug) {
        plan.reason = "debug output";
        return plan;
    }
    if (!identity_collation()) {
        plan.reason = "non-identity collation";
        return plan;
    }

    plan.kind = RANK_PLAN_RADIX_BYTES;
    plan.reason = "whole-line byte radix";
    return plan;
}

static bool
identity_collation(void)
{
    const char *lc_all = getenv("LC_ALL");
    const char *lc_collate = getenv("LC_COLLATE");
    const char *lang = getenv("LANG");

    if (lc_all != NULL && lc_all[0] != '\0') {
        return locale_is_identity_name(lc_all);
    }
    if (lc_collate != NULL && lc_collate[0] != '\0') {
        return locale_is_identity_name(lc_collate);
    }
    if (lang != NULL && lang[0] != '\0') {
        return locale_is_identity_name(lang);
    }
    return true;
}

static bool
locale_is_identity_name(const char *name)
{
    return strcmp(name, "C") == 0 || strcmp(name, "POSIX") == 0;
}
