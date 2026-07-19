#include "sort.h"

#include "cmp.h"
#include "radix.h"
#include "util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static bool scalar_sort_lines(struct rank_lines *lines, const struct rank_options *options);
static void merge_sort_range(struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, struct rank_cmp_context *cmp);
static void merge_ranges(struct rank_line *items, struct rank_line *aux, size_t lo, size_t mid, size_t hi, struct rank_cmp_context *cmp);
static void reverse_output(struct rank_lines *lines);
static const struct rank_line *output_line_at(const struct rank_lines *lines, size_t index);
static bool maybe_use_monotonic_run(struct rank_lines *lines, const struct rank_options *options);
static bool verify_sorted(const struct rank_lines *lines, const struct rank_options *options);

bool
rank_sort_lines(struct rank_lines *lines, const struct rank_options *options, const struct rank_plan *plan)
{
    bool ok;

    if (getenv("RANK_DEBUG_PLAN") != NULL) {
        fprintf(stderr, "rank: plan=%s reason=%s\n",
            rank_plan_name(plan->kind), plan->reason);
    }

    if (plan->kind == RANK_PLAN_RADIX_BYTES) {
        struct rank_radix_stats stats;

        if (maybe_use_monotonic_run(lines, options)) {
            ok = true;
            if (getenv("RANK_DEBUG_STATS") != NULL) {
                fprintf(stderr, "rank: radix presorted=1\n");
            }
        } else {
            ok = rank_radix_sort_lines(lines, options, &stats);
            if (!ok) {
                return scalar_sort_lines(lines, options);
            }
            if (ok && options->reverse) {
                reverse_output(lines);
            }
            if (getenv("RANK_DEBUG_STATS") != NULL) {
                fprintf(stderr, "rank: radix passes=%zu classified=%zu insertion_sorts=%zu\n",
                    stats.passes, stats.classified, stats.insertion_sorts);
            }
        }
    } else if (plan->kind == RANK_PLAN_RADIX_KEYS) {
        struct rank_radix_stats stats;
        bool want_stats = getenv("RANK_DEBUG_STATS") != NULL;

        ok = rank_radix_sort_key(lines, options, want_stats ? &stats : NULL);
        if (want_stats) {
            fprintf(stderr, "rank: key radix passes=%zu classified=%zu insertion_sorts=%zu\n",
                stats.passes, stats.classified, stats.insertion_sorts);
        }
    } else if (plan->kind == RANK_PLAN_RADIX_TRANSFORMED) {
        struct rank_radix_stats stats;
        bool want_stats = getenv("RANK_DEBUG_STATS") != NULL;

        ok = options->key_count > 0
            ? rank_radix_sort_transformed_key(lines, options, want_stats ? &stats : NULL)
            : rank_radix_sort_transformed_lines(lines, options, want_stats ? &stats : NULL);
        if (!ok) {
            return scalar_sort_lines(lines, options);
        }
        if (ok && options->reverse) {
            reverse_output(lines);
        }
        if (want_stats) {
            fprintf(stderr, "rank: transformed radix passes=%zu classified=%zu insertion_sorts=%zu\n",
                stats.passes, stats.classified, stats.insertion_sorts);
        }
    } else {
        ok = scalar_sort_lines(lines, options);
    }

    if (ok && getenv("RANK_DEBUG_VERIFY") != NULL && !verify_sorted(lines, options)) {
        return false;
    }
    return ok;
}

static bool
scalar_sort_lines(struct rank_lines *lines, const struct rank_options *options)
{
    struct rank_cmp_context cmp;
    struct rank_line *aux;

    if (lines->len < 2) {
        return true;
    }

    aux = rank_xmalloc(lines->len * sizeof(aux[0]));
    rank_cmp_context_init(&cmp, options, lines);
    merge_sort_range(lines->items, aux, 0, lines->len, &cmp);
    if (getenv("RANK_DEBUG_STATS") != NULL) {
        fprintf(stderr, "rank: comparator calls=%zu bytes=%zu\n", cmp.calls, cmp.bytes);
    }
    free(aux);
    return true;
}

static void
merge_sort_range(struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, struct rank_cmp_context *cmp)
{
    size_t mid;

    if (hi - lo < 2) {
        return;
    }

    mid = lo + (hi - lo) / 2U;
    merge_sort_range(items, aux, lo, mid, cmp);
    merge_sort_range(items, aux, mid, hi, cmp);
    merge_ranges(items, aux, lo, mid, hi, cmp);
}

static void
merge_ranges(struct rank_line *items, struct rank_line *aux, size_t lo, size_t mid, size_t hi, struct rank_cmp_context *cmp)
{
    size_t left = lo;
    size_t right = mid;
    size_t out = lo;
    size_t i;

    while (left < mid && right < hi) {
        if (rank_compare_lines(cmp, &items[left], &items[right]) <= 0) {
            aux[out++] = items[left++];
        } else {
            aux[out++] = items[right++];
        }
    }
    while (left < mid) {
        aux[out++] = items[left++];
    }
    while (right < hi) {
        aux[out++] = items[right++];
    }
    for (i = lo; i < hi; i++) {
        items[i] = aux[i];
    }
}

static bool
maybe_use_monotonic_run(struct rank_lines *lines, const struct rank_options *options)
{
    int direction = lines->monotonic_direction;

    if (!lines->monotonic_valid) {
        return false;
    }
    if ((direction < 0 && options->reverse) || (direction > 0 && !options->reverse)) {
        reverse_output(lines);
    }
    return true;
}

static void
reverse_output(struct rank_lines *lines)
{
    lines->output_reversed = !lines->output_reversed;
}

static const struct rank_line *
output_line_at(const struct rank_lines *lines, size_t index)
{
    if (lines->output_reversed) {
        index = lines->len - 1U - index;
    }
    return &lines->items[index];
}

static bool
verify_sorted(const struct rank_lines *lines, const struct rank_options *options)
{
    struct rank_cmp_context cmp;
    size_t i;

    rank_cmp_context_init(&cmp, options, lines);
    for (i = 1; i < lines->len; i++) {
        if (rank_compare_lines(&cmp, output_line_at(lines, i - 1U), output_line_at(lines, i)) > 0) {
            fprintf(stderr, "rank: internal sort verification failed at record %zu\n", i);
            return false;
        }
    }
    return true;
}
