#include "radix.h"

#include "util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

enum {
    RADIX_BUCKETS = 257,
    RADIX_INSERTION_THRESHOLD = 32,
    RADIX_DEPTH_LIMIT = 4096
};

static void radix_range(struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, size_t depth);
static void insertion_range(struct rank_line *items, size_t lo, size_t hi, size_t depth);
static int compare_from_depth(const struct rank_line *a, const struct rank_line *b, size_t depth);
static size_t line_bucket(const struct rank_line *line, size_t depth);

bool
rank_radix_sort_lines(struct rank_lines *lines)
{
    struct rank_line *aux;

    if (lines->len < 2) {
        return true;
    }
    aux = rank_xmalloc(lines->len * sizeof(aux[0]));
    radix_range(lines->items, aux, 0, lines->len, 0);
    free(aux);
    return true;
}

static void
radix_range(struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, size_t depth)
{
    size_t counts[RADIX_BUCKETS] = {0};
    size_t starts[RADIX_BUCKETS];
    size_t next[RADIX_BUCKETS];
    size_t sum = lo;
    size_t i;
    size_t b;

    if (hi - lo <= RADIX_INSERTION_THRESHOLD || depth >= RADIX_DEPTH_LIMIT) {
        insertion_range(items, lo, hi, depth);
        return;
    }

    for (i = lo; i < hi; i++) {
        counts[line_bucket(&items[i], depth)]++;
    }
    for (b = 0; b < RADIX_BUCKETS; b++) {
        starts[b] = sum;
        next[b] = sum;
        sum += counts[b];
    }
    for (i = lo; i < hi; i++) {
        b = line_bucket(&items[i], depth);
        aux[next[b]++] = items[i];
    }
    for (i = lo; i < hi; i++) {
        items[i] = aux[i];
    }

    for (b = 1; b < RADIX_BUCKETS; b++) {
        size_t start = starts[b];
        size_t end = start + counts[b];

        if (end - start > 1) {
            radix_range(items, aux, start, end, depth + 1U);
        }
    }
}

static void
insertion_range(struct rank_line *items, size_t lo, size_t hi, size_t depth)
{
    size_t i;

    for (i = lo + 1U; i < hi; i++) {
        struct rank_line line = items[i];
        size_t j = i;

        while (j > lo && compare_from_depth(&line, &items[j - 1U], depth) < 0) {
            items[j] = items[j - 1U];
            j--;
        }
        items[j] = line;
    }
}

static int
compare_from_depth(const struct rank_line *a, const struct rank_line *b, size_t depth)
{
    size_t a_pos = depth;
    size_t b_pos = depth;

    while (a_pos < a->len && b_pos < b->len) {
        if (a->text[a_pos] < b->text[b_pos]) {
            return -1;
        }
        if (a->text[a_pos] > b->text[b_pos]) {
            return 1;
        }
        a_pos++;
        b_pos++;
    }
    if (a_pos == a->len && b_pos == b->len) {
        return 0;
    }
    return a_pos == a->len ? -1 : 1;
}

static size_t
line_bucket(const struct rank_line *line, size_t depth)
{
    if (depth >= line->len) {
        return 0;
    }
    return (size_t)line->text[depth] + 1U;
}
