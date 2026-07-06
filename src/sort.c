#include "sort.h"

#include "cmp.h"
#include "util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void merge_sort_range(struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, struct rank_cmp_context *cmp);
static void merge_ranges(struct rank_line *items, struct rank_line *aux, size_t lo, size_t mid, size_t hi, struct rank_cmp_context *cmp);

bool
rank_sort_lines(struct rank_lines *lines, const struct rank_options *options)
{
    struct rank_cmp_context cmp;
    struct rank_line *aux;
    size_t i;

    if (lines->len < 2) {
        return true;
    }

    aux = rank_xmalloc(lines->len * sizeof(aux[0]));
    rank_cmp_context_init(&cmp, options, lines);
    merge_sort_range(lines->items, aux, 0, lines->len, &cmp);
    if (getenv("RANK_DEBUG_STATS") != NULL) {
        fprintf(stderr, "rank: comparator calls=%zu bytes=%zu\n", cmp.calls, cmp.bytes);
    }
    if (getenv("RANK_DEBUG_VERIFY") != NULL) {
        rank_cmp_context_init(&cmp, options, lines);
        for (i = 1; i < lines->len; i++) {
            if (rank_compare_lines(&cmp, &lines->items[i - 1U], &lines->items[i]) > 0) {
                fprintf(stderr, "rank: internal sort verification failed at record %zu\n", i);
                free(aux);
                return false;
            }
        }
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
