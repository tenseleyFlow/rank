#include "plan.h"

#include "rank_locale.h"

#include <stdbool.h>
#include <stdlib.h>

static bool has_special_sort(const struct rank_options *options);
static bool has_text_modifiers(const struct rank_options *options);

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
    if (options->debug) {
        plan.reason = "debug output";
        return plan;
    }
    if (has_text_modifiers(options)) {
        if (rank_locale_collation_identity() && options->sort_mode == RANK_SORT_BYTE && !(options->reverse && options->unique)) {
            if (options->key_count == 0) {
                plan.kind = RANK_PLAN_RADIX_TRANSFORMED;
                plan.reason = "whole-line filtered radix";
                return plan;
            }
            if (options->key_count == 1 && !options->reverse && !options->unique && !options->keys[0].reverse && options->keys[0].sort_mode == RANK_SORT_BYTE
                && (options->stable || options->ignore_case || options->dictionary_order || options->ignore_nonprinting)) {
                plan.kind = RANK_PLAN_RADIX_TRANSFORMED;
                plan.reason = options->stable ? "single stable filtered key radix" : "single filtered key radix";
                return plan;
            }
        }
        plan.reason = "text modifier";
        return plan;
    }
    if (!rank_locale_collation_identity()) {
        if (options->sort_mode == RANK_SORT_BYTE && options->key_count == 0 && !options->reverse && !options->unique) {
            plan.kind = RANK_PLAN_RADIX_TRANSFORMED;
            plan.reason = "whole-line transformed radix";
            return plan;
        }
        if (options->sort_mode == RANK_SORT_BYTE && options->key_count == 1 && !options->reverse && !options->unique && !options->keys[0].reverse && options->keys[0].sort_mode == RANK_SORT_BYTE) {
            plan.kind = RANK_PLAN_RADIX_TRANSFORMED;
            plan.reason = options->stable ? "single stable transformed key radix" : "single transformed key radix";
            return plan;
        }
        plan.reason = "non-identity collation";
        return plan;
    }
    if (has_special_sort(options)) {
        plan.reason = "special comparator";
        return plan;
    }
    if (options->key_count > 0) {
        plan.kind = RANK_PLAN_RADIX_KEYS;
        plan.reason = options->unique ? "unique key byte radix" : (options->key_count == 1 ? (options->stable ? "single stable key byte radix" : "single key byte radix") : "multi-key byte radix");
        return plan;
    }
    plan.kind = RANK_PLAN_RADIX_BYTES;
    plan.reason = "whole-line byte radix";
    return plan;
}

const char *
rank_plan_name(enum rank_plan_kind kind)
{
    switch (kind) {
    case RANK_PLAN_SCALAR:
        return "scalar";
    case RANK_PLAN_RADIX_BYTES:
        return "radix-bytes";
    case RANK_PLAN_RADIX_KEYS:
        return "radix-keys";
    case RANK_PLAN_RADIX_TRANSFORMED:
        return "radix-transformed";
    }
    return "unknown";
}

static bool
has_text_modifiers(const struct rank_options *options)
{
    size_t i;

    if (options->ignore_case || options->dictionary_order || options->ignore_nonprinting) {
        return true;
    }
    for (i = 0; i < options->key_count; i++) {
        if (options->keys[i].ignore_case || options->keys[i].dictionary_order || options->keys[i].ignore_nonprinting) {
            return true;
        }
    }
    return false;
}

static bool
has_special_sort(const struct rank_options *options)
{
    size_t i;

    if (options->sort_mode != RANK_SORT_BYTE) {
        return true;
    }
    for (i = 0; i < options->key_count; i++) {
        if (options->keys[i].sort_mode != RANK_SORT_BYTE) {
            return true;
        }
    }
    return false;
}
