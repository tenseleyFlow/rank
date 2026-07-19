#include "radix.h"

#include "sys/thread.h"
#include "util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    RADIX_BUCKETS = 257,
    RADIX_INSERTION_THRESHOLD = 24,
    RADIX_DEPTH_LIMIT = 4096,
    KEY_RADIX_INSERTION_THRESHOLD = 48,
    RADIX_PARALLEL_MIN_LINES = 65536
};

/* Top-level bucket ranges handed to worker threads: disjoint, so the
   sorted result is identical regardless of scheduling. */
struct radix_child {
    size_t lo;
    size_t hi;
    size_t depth;
};

struct radix_children {
    struct radix_child items[RADIX_BUCKETS];
    size_t count;
};

struct rank_radix_line {
    uint32_t off;
    uint32_t len;
};

struct rank_radix_context {
    const unsigned char *base;
    struct rank_radix_stats *stats;
};

static void radix_range(struct rank_radix_line *items, size_t lo, size_t hi, size_t depth, struct rank_radix_context *ctx, struct radix_children *emit);
static void insertion_range(struct rank_radix_line *items, size_t lo, size_t hi, size_t depth, const struct rank_radix_context *ctx);
static size_t radix_parallel_threads(size_t requested, size_t line_count);
static void radix_line_task(void *arg, size_t index);
static void key_radix_task(void *arg, size_t index);
static void sum_stats(struct rank_radix_stats *total, const struct rank_radix_stats *parts, size_t count);
static int compare_from_depth(const struct rank_radix_line *a, const struct rank_radix_line *b, size_t depth, const struct rank_radix_context *ctx);
static size_t line_bucket(const struct rank_radix_line *line, size_t depth, const struct rank_radix_context *ctx);
static size_t skip_common_prefix_lcp(struct rank_radix_line *items, size_t lo, size_t hi, size_t depth, struct rank_radix_context *ctx);
static void swap_lines(struct rank_radix_line *a, struct rank_radix_line *b);
static void key_radix_range(struct rank_lines *lines, const struct rank_options *options, struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, size_t key_id, size_t depth, struct rank_radix_stats *stats, struct radix_children *emit);
static void key_insertion_range(struct rank_lines *lines, const struct rank_options *options, struct rank_line *items, size_t lo, size_t hi, size_t key_id, size_t depth);
static int compare_key_from_depth(const struct rank_lines *lines, const struct rank_options *options, const struct rank_line *a, const struct rank_line *b, size_t key_id, size_t depth);
static size_t key_bucket(const struct rank_lines *lines, const struct rank_line *line, size_t key_id, size_t depth);
static size_t skip_key_common_prefix_lcp(const struct rank_lines *lines, struct rank_line *items, size_t lo, size_t hi, size_t key_id, size_t depth, struct rank_radix_stats *stats);
static bool maybe_use_key_monotonic(struct rank_lines *lines, const struct rank_options *options);
static bool maybe_use_key_range_monotonic(struct rank_lines *lines, const struct rank_options *options, struct rank_line *items, size_t lo, size_t hi, size_t key_id);
static void reverse_line_range(struct rank_line *items, size_t lo, size_t hi);
static void sort_line_last_resort_groups(struct rank_lines *lines, const struct rank_options *options, struct rank_line *aux, size_t lo, size_t hi, size_t key_id);
static void sort_next_key_groups(struct rank_lines *lines, const struct rank_options *options, struct rank_line *aux, size_t lo, size_t hi, size_t key_id);
static bool same_key(const struct rank_lines *lines, const struct rank_options *options, const struct rank_line *a, const struct rank_line *b, size_t key_id);
static void line_radix_range(struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, size_t depth, struct rank_radix_stats *stats);
static void line_insertion_range(struct rank_line *items, size_t lo, size_t hi, size_t depth);
static int compare_line_from_depth(const struct rank_line *a, const struct rank_line *b, size_t depth);
static size_t full_line_bucket(const struct rank_line *line, size_t depth);
static void transformed_radix_range(const struct rank_lines *lines, struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, size_t depth, struct rank_radix_stats *stats);
static void transformed_insertion_range(const struct rank_lines *lines, struct rank_line *items, size_t lo, size_t hi, size_t depth);
static int compare_transformed_from_depth(const struct rank_lines *lines, const struct rank_line *a, const struct rank_line *b, size_t depth);
static size_t transformed_bucket(const struct rank_lines *lines, const struct rank_line *line, size_t depth);
static void transformed_key_radix_range(const struct rank_lines *lines, struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, size_t depth, struct rank_radix_stats *stats);
static void transformed_key_insertion_range(const struct rank_lines *lines, struct rank_line *items, size_t lo, size_t hi, size_t depth);
static int compare_transformed_key_from_depth(const struct rank_lines *lines, const struct rank_line *a, const struct rank_line *b, size_t depth);
static size_t transformed_key_bucket(const struct rank_lines *lines, const struct rank_line *line, size_t depth);
static bool transformed_key_same(const struct rank_lines *lines, const struct rank_line *a, const struct rank_line *b);
static void sort_transformed_key_equal_groups(struct rank_lines *lines, struct rank_line *aux);
static void sort_transformed_key_equal_groups_raw(struct rank_lines *lines, struct rank_line *aux);
static bool transformed_line_same(const struct rank_lines *lines, const struct rank_line *a, const struct rank_line *b);
static void sort_transformed_line_equal_groups_original(struct rank_lines *lines, struct rank_line *aux);
static void compact_transformed_line_groups(struct rank_lines *lines);

struct radix_line_parallel {
    struct rank_radix_line *items;
    const unsigned char *base;
    const struct radix_children *children;
    struct rank_radix_stats *parts;
};

struct key_radix_parallel {
    struct rank_lines *lines;
    const struct rank_options *options;
    struct rank_line *aux;
    const struct radix_children *children;
    struct rank_radix_stats *parts;
};

bool
rank_radix_sort_lines(struct rank_lines *lines, const struct rank_options *options, struct rank_radix_stats *stats)
{
    struct rank_radix_line *items;
    struct rank_radix_context ctx;
    size_t threads;
    size_t i;

    if (stats != NULL) {
        stats->passes = 0;
        stats->classified = 0;
        stats->insertion_sorts = 0;
    }
    if (lines->len < 2) {
        return true;
    }
    if (lines->data_len > UINT32_MAX) {
        return false;
    }

    items = rank_xmalloc(lines->len * sizeof(items[0]));
    ctx.base = lines->data;
    ctx.stats = stats;
    for (i = 0; i < lines->len; i++) {
        items[i].off = (uint32_t)lines->items[i].off;
        items[i].len = (uint32_t)lines->items[i].len;
    }

    threads = radix_parallel_threads(options != NULL ? options->parallel : 1, lines->len);
    if (threads > 1) {
        struct radix_children children;

        children.count = 0;
        radix_range(items, 0, lines->len, 0, &ctx, &children);
        if (children.count > 0) {
            struct rank_radix_stats *parts = rank_xmalloc(children.count * sizeof(parts[0]));
            struct radix_line_parallel pctx;

            memset(parts, 0, children.count * sizeof(parts[0]));
            pctx.items = items;
            pctx.base = lines->data;
            pctx.children = &children;
            pctx.parts = parts;
            rank_run_tasks(radix_line_task, &pctx, children.count, threads);
            if (stats != NULL) {
                sum_stats(stats, parts, children.count);
            }
            free(parts);
        }
    } else {
        radix_range(items, 0, lines->len, 0, &ctx, NULL);
    }
    for (i = 0; i < lines->len; i++) {
        lines->items[i].off = items[i].off;
        lines->items[i].len = items[i].len;
        lines->items[i].text = lines->data + items[i].off;
    }
    free(items);
    return true;
}

bool
rank_radix_sort_key(struct rank_lines *lines, const struct rank_options *options, struct rank_radix_stats *stats)
{
    struct rank_line *aux;
    size_t threads;

    if (stats != NULL) {
        stats->passes = 0;
        stats->classified = 0;
        stats->insertion_sorts = 0;
    }
    if (lines->len < 2) {
        return true;
    }
    if (maybe_use_key_monotonic(lines, options)) {
        return true;
    }

    aux = rank_xmalloc(lines->len * sizeof(aux[0]));
    threads = radix_parallel_threads(options->parallel, lines->len);
    if (threads > 1) {
        struct radix_children children;

        children.count = 0;
        key_radix_range(lines, options, lines->items, aux, 0, lines->len, 0, 0, stats, &children);
        if (children.count > 0) {
            struct rank_radix_stats *parts = rank_xmalloc(children.count * sizeof(parts[0]));
            struct key_radix_parallel pctx;

            memset(parts, 0, children.count * sizeof(parts[0]));
            pctx.lines = lines;
            pctx.options = options;
            pctx.aux = aux;
            pctx.children = &children;
            pctx.parts = parts;
            rank_run_tasks(key_radix_task, &pctx, children.count, threads);
            if (stats != NULL) {
                sum_stats(stats, parts, children.count);
            }
            free(parts);
        }
    } else {
        key_radix_range(lines, options, lines->items, aux, 0, lines->len, 0, 0, stats, NULL);
    }
    if (options->key_count > 1) {
        sort_next_key_groups(lines, options, aux, 0, lines->len, 0);
    } else if (!options->stable && !options->unique) {
        sort_line_last_resort_groups(lines, options, aux, 0, lines->len, 0);
    }
    free(aux);
    return true;
}

bool
rank_radix_sort_transformed_lines(struct rank_lines *lines, const struct rank_options *options, struct rank_radix_stats *stats)
{
    struct rank_line *aux;
    size_t i;

    if (stats != NULL) {
        stats->passes = 0;
        stats->classified = 0;
        stats->insertion_sorts = 0;
    }
    if (lines->len < 2) {
        return true;
    }
    if (lines->line_transforms == NULL) {
        return false;
    }
    for (i = 0; i < lines->len; i++) {
        const struct rank_transformed_span *span = rank_line_transform(lines, &lines->items[i]);

        if (span == NULL || !span->valid) {
            return false;
        }
    }

    aux = rank_xmalloc(lines->len * sizeof(aux[0]));
    transformed_radix_range(lines, lines->items, aux, 0, lines->len, 0, stats);
    if (!options->stable && !options->unique) {
        sort_transformed_line_equal_groups_original(lines, aux);
    } else if (options->unique && options->reverse) {
        compact_transformed_line_groups(lines);
    }
    free(aux);
    return true;
}

bool
rank_radix_sort_transformed_key(struct rank_lines *lines, const struct rank_options *options, struct rank_radix_stats *stats)
{
    struct rank_line *aux;
    size_t i;

    if (stats != NULL) {
        stats->passes = 0;
        stats->classified = 0;
        stats->insertion_sorts = 0;
    }
    if (lines->len < 2) {
        return true;
    }
    if (options->key_count != 1 || lines->key_transforms == NULL) {
        return false;
    }
    for (i = 0; i < lines->len; i++) {
        const struct rank_transformed_span *key = rank_line_key_transform(lines, &lines->items[i], 0);

        if (key == NULL || !key->valid) {
            return false;
        }
        if (!options->stable && lines->line_transforms != NULL) {
            const struct rank_transformed_span *line = rank_line_transform(lines, &lines->items[i]);

            if (!line->valid) {
                return false;
            }
        }
    }

    aux = rank_xmalloc(lines->len * sizeof(aux[0]));
    transformed_key_radix_range(lines, lines->items, aux, 0, lines->len, 0, stats);
    if (!options->stable) {
        if (lines->line_transforms != NULL) {
            sort_transformed_key_equal_groups(lines, aux);
        } else {
            sort_transformed_key_equal_groups_raw(lines, aux);
        }
    }
    free(aux);
    return true;
}

static void
radix_range(struct rank_radix_line *items, size_t lo, size_t hi, size_t depth, struct rank_radix_context *ctx, struct radix_children *emit)
{
    size_t counts[RADIX_BUCKETS] = {0};
    size_t starts[RADIX_BUCKETS];
    size_t next[RADIX_BUCKETS];
    size_t sum = lo;
    size_t min_len = 0;
    size_t i;
    size_t b;
    bool min_len_known = false;

    depth = skip_common_prefix_lcp(items, lo, hi, depth, ctx);
    while (true) {
        size_t nonempty = 0;
        size_t only_bucket = 0;

        if (hi - lo <= RADIX_INSERTION_THRESHOLD || depth >= RADIX_DEPTH_LIMIT) {
            ctx->stats->insertion_sorts++;
            insertion_range(items, lo, hi, depth, ctx);
            return;
        }

        memset(counts, 0, sizeof(counts));
        ctx->stats->classified += hi - lo;
        if (!min_len_known) {
            min_len = items[lo].len;
            for (i = lo; i < hi; i++) {
                if (items[i].len < min_len) {
                    min_len = items[i].len;
                }
                counts[line_bucket(&items[i], depth, ctx)]++;
            }
            min_len_known = true;
        } else if (depth < min_len) {
            for (i = lo; i < hi; i++) {
                counts[(size_t)ctx->base[items[i].off + depth] + 1U]++;
            }
        } else {
            for (i = lo; i < hi; i++) {
                counts[line_bucket(&items[i], depth, ctx)]++;
            }
        }
        for (b = 0; b < RADIX_BUCKETS; b++) {
            if (counts[b] != 0) {
                nonempty++;
                only_bucket = b;
            }
        }
        if (nonempty != 1) {
            break;
        }
        if (only_bucket == 0) {
            return;
        }
        depth++;
    }

    ctx->stats->passes++;
    for (b = 0; b < RADIX_BUCKETS; b++) {
        starts[b] = sum;
        next[b] = sum;
        sum += counts[b];
    }
    for (b = 0; b < RADIX_BUCKETS; b++) {
        size_t end = starts[b] + counts[b];
        bool direct_bucket = depth < min_len;

        i = next[b];
        while (i < end) {
            size_t bucket = direct_bucket ? (size_t)ctx->base[items[i].off + depth] + 1U : line_bucket(&items[i], depth, ctx);

            if (bucket == b) {
                i++;
                next[b] = i;
            } else {
                swap_lines(&items[i], &items[next[bucket]++]);
            }
        }
    }

    for (b = 1; b < RADIX_BUCKETS; b++) {
        size_t start = starts[b];
        size_t end = start + counts[b];

        if (end - start > 1) {
            if (emit != NULL) {
                emit->items[emit->count].lo = start;
                emit->items[emit->count].hi = end;
                emit->items[emit->count].depth = depth + 1U;
                emit->count++;
            } else {
                radix_range(items, start, end, depth + 1U, ctx, NULL);
            }
        }
    }
}

static void
insertion_range(struct rank_radix_line *items, size_t lo, size_t hi, size_t depth, const struct rank_radix_context *ctx)
{
    size_t i;

    for (i = lo + 1U; i < hi; i++) {
        struct rank_radix_line line = items[i];
        size_t left = lo;
        size_t right = i;
        size_t j;

        while (left < right) {
            size_t mid = left + (right - left) / 2U;

            if (compare_from_depth(&line, &items[mid], depth, ctx) < 0) {
                right = mid;
            } else {
                left = mid + 1U;
            }
        }

        for (j = i; j > left; j--) {
            items[j] = items[j - 1U];
        }
        items[left] = line;
    }
}

static int
compare_from_depth(const struct rank_radix_line *a, const struct rank_radix_line *b, size_t depth, const struct rank_radix_context *ctx)
{
    size_t a_pos = depth;
    size_t b_pos = depth;

    while (a_pos < a->len && b_pos < b->len) {
        unsigned char a_byte = ctx->base[a->off + a_pos];
        unsigned char b_byte = ctx->base[b->off + b_pos];

        if (a_byte < b_byte) {
            return -1;
        }
        if (a_byte > b_byte) {
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
line_bucket(const struct rank_radix_line *line, size_t depth, const struct rank_radix_context *ctx)
{
    if (depth >= line->len) {
        return 0;
    }
    return (size_t)ctx->base[line->off + depth] + 1U;
}

static void
swap_lines(struct rank_radix_line *a, struct rank_radix_line *b)
{
    struct rank_radix_line tmp = *a;

    *a = *b;
    *b = tmp;
}

static size_t
radix_parallel_threads(size_t requested, size_t line_count)
{
    const char *env = getenv("RANK_PARALLEL_MIN");
    size_t min_lines = RADIX_PARALLEL_MIN_LINES;

    if (env != NULL && env[0] != '\0') {
        min_lines = (size_t)strtoul(env, NULL, 10);
    }
    if (requested <= 1 || line_count < min_lines) {
        return 1;
    }
    return requested;
}

static void
radix_line_task(void *arg, size_t index)
{
    struct radix_line_parallel *p = arg;
    const struct radix_child *child = &p->children->items[index];
    struct rank_radix_context ctx;

    ctx.base = p->base;
    ctx.stats = &p->parts[index];
    radix_range(p->items, child->lo, child->hi, child->depth, &ctx, NULL);
}

static void
key_radix_task(void *arg, size_t index)
{
    struct key_radix_parallel *p = arg;
    const struct radix_child *child = &p->children->items[index];

    key_radix_range(p->lines, p->options, p->lines->items, p->aux, child->lo, child->hi, 0, child->depth, &p->parts[index], NULL);
}

static void
sum_stats(struct rank_radix_stats *total, const struct rank_radix_stats *parts, size_t count)
{
    size_t i;

    if (total == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        total->passes += parts[i].passes;
        total->classified += parts[i].classified;
        total->insertion_sorts += parts[i].insertion_sorts;
    }
}

static size_t
skip_common_prefix_lcp(struct rank_radix_line *items, size_t lo, size_t hi, size_t depth, struct rank_radix_context *ctx)
{
    const struct rank_radix_line *first = &items[lo];
    size_t limit = first->len;
    size_t i;
    bool same_len = true;

    if (hi - lo <= RADIX_INSERTION_THRESHOLD || depth >= RADIX_DEPTH_LIMIT) {
        return depth;
    }
    if (limit > RADIX_DEPTH_LIMIT) {
        limit = RADIX_DEPTH_LIMIT;
    }
    if (depth >= limit) {
        return depth;
    }

    for (i = lo + 1U; i < hi; i++) {
        const struct rank_radix_line *line = &items[i];
        size_t common_limit = line->len < limit ? line->len : limit;
        size_t pos = depth;

        if (line->len != first->len) {
            same_len = false;
        }
        while (pos < common_limit && ctx->base[first->off + pos] == ctx->base[line->off + pos]) {
            pos++;
        }
        limit = pos;
        if (limit == depth) {
            return depth;
        }
    }

    ctx->stats->classified += (hi - lo) * (limit - depth);
    if (limit == first->len && same_len) {
        return RADIX_DEPTH_LIMIT;
    }
    return limit;
}

static void
key_radix_range(struct rank_lines *lines, const struct rank_options *options, struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, size_t key_id, size_t depth, struct rank_radix_stats *stats, struct radix_children *emit)
{
    size_t counts[RADIX_BUCKETS] = {0};
    size_t starts[RADIX_BUCKETS];
    size_t next[RADIX_BUCKETS];
    size_t sum = lo;
    size_t i;
    size_t b;
    bool reverse = options->reverse != options->keys[key_id].reverse;

    depth = skip_key_common_prefix_lcp(lines, items, lo, hi, key_id, depth, stats);
    if (hi - lo <= KEY_RADIX_INSERTION_THRESHOLD || depth >= RADIX_DEPTH_LIMIT) {
        if (stats != NULL) {
            stats->insertion_sorts++;
        }
        key_insertion_range(lines, options, items, lo, hi, key_id, depth);
        return;
    }

    if (stats != NULL) {
        stats->passes++;
        stats->classified += hi - lo;
    }
    for (i = lo; i < hi; i++) {
        counts[key_bucket(lines, &items[i], key_id, depth)]++;
    }
    if (reverse) {
        for (b = RADIX_BUCKETS; b > 0; b--) {
            size_t bucket = b - 1U;

            starts[bucket] = sum;
            next[bucket] = sum;
            sum += counts[bucket];
        }
    } else {
        for (b = 0; b < RADIX_BUCKETS; b++) {
            starts[b] = sum;
            next[b] = sum;
            sum += counts[b];
        }
    }
    for (i = lo; i < hi; i++) {
        b = key_bucket(lines, &items[i], key_id, depth);
        aux[next[b]++] = items[i];
    }
    memcpy(&items[lo], &aux[lo], (hi - lo) * sizeof(items[0]));

    for (b = 1; b < RADIX_BUCKETS; b++) {
        size_t start = starts[b];
        size_t end = start + counts[b];

        if (end - start > 1) {
            if (emit != NULL) {
                emit->items[emit->count].lo = start;
                emit->items[emit->count].hi = end;
                emit->items[emit->count].depth = depth + 1U;
                emit->count++;
            } else {
                key_radix_range(lines, options, items, aux, start, end, key_id, depth + 1U, stats, NULL);
            }
        }
    }
}

static void
key_insertion_range(struct rank_lines *lines, const struct rank_options *options, struct rank_line *items, size_t lo, size_t hi, size_t key_id, size_t depth)
{
    size_t i;

    for (i = lo + 1U; i < hi; i++) {
        struct rank_line line = items[i];
        size_t left = lo;
        size_t right = i;
        size_t j;

        while (left < right) {
            size_t mid = left + (right - left) / 2U;

            if (compare_key_from_depth(lines, options, &line, &items[mid], key_id, depth) < 0) {
                right = mid;
            } else {
                left = mid + 1U;
            }
        }
        for (j = i; j > left; j--) {
            items[j] = items[j - 1U];
        }
        items[left] = line;
    }
}

static int
compare_key_from_depth(const struct rank_lines *lines, const struct rank_options *options, const struct rank_line *a, const struct rank_line *b, size_t key_id, size_t depth)
{
    const struct rank_key_span *a_key = rank_line_key_span(lines, a, key_id);
    const struct rank_key_span *b_key = rank_line_key_span(lines, b, key_id);
    size_t a_pos = depth;
    size_t b_pos = depth;
    int result;

    while (a_pos < a_key->len && b_pos < b_key->len) {
        if (a_key->ptr[a_pos] < b_key->ptr[b_pos]) {
            result = -1;
            return options->reverse != options->keys[key_id].reverse ? -result : result;
        }
        if (a_key->ptr[a_pos] > b_key->ptr[b_pos]) {
            result = 1;
            return options->reverse != options->keys[key_id].reverse ? -result : result;
        }
        a_pos++;
        b_pos++;
    }
    if (a_pos == a_key->len && b_pos == b_key->len) {
        return 0;
    }
    result = a_pos == a_key->len ? -1 : 1;
    return options->reverse != options->keys[key_id].reverse ? -result : result;
}

static size_t
key_bucket(const struct rank_lines *lines, const struct rank_line *line, size_t key_id, size_t depth)
{
    const struct rank_key_span *key = rank_line_key_span(lines, line, key_id);

    if (depth >= key->len) {
        return 0;
    }
    return (size_t)key->ptr[depth] + 1U;
}

static size_t
skip_key_common_prefix_lcp(const struct rank_lines *lines, struct rank_line *items, size_t lo, size_t hi, size_t key_id, size_t depth, struct rank_radix_stats *stats)
{
    const struct rank_key_span *first = rank_line_key_span(lines, &items[lo], key_id);
    size_t limit = first->len;
    size_t i;
    bool same_len = true;

    if (hi - lo <= KEY_RADIX_INSERTION_THRESHOLD || depth >= RADIX_DEPTH_LIMIT) {
        return depth;
    }
    if (limit > RADIX_DEPTH_LIMIT) {
        limit = RADIX_DEPTH_LIMIT;
    }
    if (depth >= limit) {
        return depth;
    }

    for (i = lo + 1U; i < hi; i++) {
        const struct rank_key_span *key = rank_line_key_span(lines, &items[i], key_id);
        size_t common_limit = key->len < limit ? key->len : limit;
        size_t pos = depth;

        if (key->len != first->len) {
            same_len = false;
        }
        while (pos < common_limit && first->ptr[pos] == key->ptr[pos]) {
            pos++;
        }
        limit = pos;
        if (limit == depth) {
            return depth;
        }
    }

    if (stats != NULL) {
        stats->classified += (hi - lo) * (limit - depth);
    }
    if (limit == first->len && same_len) {
        return RADIX_DEPTH_LIMIT;
    }
    return limit;
}

static bool
maybe_use_key_monotonic(struct rank_lines *lines, const struct rank_options *options)
{
    bool ascending = true;
    bool strictly_descending = true;
    size_t i;

    if (!options->stable || options->unique || options->key_count != 1) {
        return false;
    }
    for (i = 1; i < lines->len; i++) {
        int cmp = compare_key_from_depth(lines, options, &lines->items[i - 1U], &lines->items[i], 0, 0);

        if (cmp > 0) {
            ascending = false;
        }
        if (cmp <= 0) {
            strictly_descending = false;
        }
        if (!ascending && !strictly_descending) {
            return false;
        }
    }
    if (strictly_descending) {
        lines->output_reversed = !lines->output_reversed;
    }
    return true;
}

static bool
maybe_use_key_range_monotonic(struct rank_lines *lines, const struct rank_options *options, struct rank_line *items, size_t lo, size_t hi, size_t key_id)
{
    bool ascending = true;
    bool strictly_descending = true;
    size_t i;

    for (i = lo + 1U; i < hi; i++) {
        int cmp = compare_key_from_depth(lines, options, &items[i - 1U], &items[i], key_id, 0);

        if (cmp > 0) {
            ascending = false;
        }
        if (cmp <= 0) {
            strictly_descending = false;
        }
        if (!ascending && !strictly_descending) {
            return false;
        }
    }
    if (strictly_descending) {
        reverse_line_range(items, lo, hi);
    }
    return true;
}

static void
reverse_line_range(struct rank_line *items, size_t lo, size_t hi)
{
    while (hi - lo > 1) {
        struct rank_line tmp;

        hi--;
        tmp = items[lo];
        items[lo] = items[hi];
        items[hi] = tmp;
        lo++;
    }
}

static void
sort_line_last_resort_groups(struct rank_lines *lines, const struct rank_options *options, struct rank_line *aux, size_t lo, size_t hi, size_t key_id)
{
    size_t start = lo;
    size_t i;

    for (i = lo + 1U; i <= hi; i++) {
        if (i < hi && same_key(lines, options, &lines->items[start], &lines->items[i], key_id)) {
            continue;
        }
        if (i - start > 1) {
            line_radix_range(lines->items, aux, start, i, 0, NULL);
            if (options->reverse) {
                reverse_line_range(lines->items, start, i);
            }
        }
        start = i;
    }
}

static void
sort_next_key_groups(struct rank_lines *lines, const struct rank_options *options, struct rank_line *aux, size_t lo, size_t hi, size_t key_id)
{
    size_t next_key = key_id + 1U;
    size_t start = lo;
    size_t i;

    for (i = lo + 1U; i <= hi; i++) {
        if (i < hi && same_key(lines, options, &lines->items[start], &lines->items[i], key_id)) {
            continue;
        }
        if (i - start > 1) {
            if (!maybe_use_key_range_monotonic(lines, options, lines->items, start, i, next_key)) {
                key_radix_range(lines, options, lines->items, aux, start, i, next_key, 0, NULL, NULL);
            }
            if (next_key + 1U < options->key_count) {
                sort_next_key_groups(lines, options, aux, start, i, next_key);
            } else if (!options->stable && !options->unique) {
                sort_line_last_resort_groups(lines, options, aux, start, i, next_key);
            }
        }
        start = i;
    }
}

static bool
same_key(const struct rank_lines *lines, const struct rank_options *options, const struct rank_line *a, const struct rank_line *b, size_t key_id)
{
    return compare_key_from_depth(lines, options, a, b, key_id, 0) == 0;
}

static void
line_radix_range(struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, size_t depth, struct rank_radix_stats *stats)
{
    size_t counts[RADIX_BUCKETS] = {0};
    size_t starts[RADIX_BUCKETS];
    size_t next[RADIX_BUCKETS];
    size_t sum = lo;
    size_t i;
    size_t b;

    if (hi - lo <= RADIX_INSERTION_THRESHOLD || depth >= RADIX_DEPTH_LIMIT) {
        if (stats != NULL) {
            stats->insertion_sorts++;
        }
        line_insertion_range(items, lo, hi, depth);
        return;
    }

    if (stats != NULL) {
        stats->passes++;
        stats->classified += hi - lo;
    }
    for (i = lo; i < hi; i++) {
        counts[full_line_bucket(&items[i], depth)]++;
    }
    for (b = 0; b < RADIX_BUCKETS; b++) {
        starts[b] = sum;
        next[b] = sum;
        sum += counts[b];
    }
    for (i = lo; i < hi; i++) {
        b = full_line_bucket(&items[i], depth);
        aux[next[b]++] = items[i];
    }
    memcpy(&items[lo], &aux[lo], (hi - lo) * sizeof(items[0]));

    for (b = 1; b < RADIX_BUCKETS; b++) {
        size_t start = starts[b];
        size_t end = start + counts[b];

        if (end - start > 1) {
            line_radix_range(items, aux, start, end, depth + 1U, stats);
        }
    }
}

static void
line_insertion_range(struct rank_line *items, size_t lo, size_t hi, size_t depth)
{
    size_t i;

    for (i = lo + 1U; i < hi; i++) {
        struct rank_line line = items[i];
        size_t left = lo;
        size_t right = i;
        size_t j;

        while (left < right) {
            size_t mid = left + (right - left) / 2U;

            if (compare_line_from_depth(&line, &items[mid], depth) < 0) {
                right = mid;
            } else {
                left = mid + 1U;
            }
        }
        for (j = i; j > left; j--) {
            items[j] = items[j - 1U];
        }
        items[left] = line;
    }
}

static int
compare_line_from_depth(const struct rank_line *a, const struct rank_line *b, size_t depth)
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
full_line_bucket(const struct rank_line *line, size_t depth)
{
    if (depth >= line->len) {
        return 0;
    }
    return (size_t)line->text[depth] + 1U;
}

static void
transformed_radix_range(const struct rank_lines *lines, struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, size_t depth, struct rank_radix_stats *stats)
{
    size_t counts[RADIX_BUCKETS] = {0};
    size_t starts[RADIX_BUCKETS];
    size_t next[RADIX_BUCKETS];
    size_t sum = lo;
    size_t i;
    size_t b;

    if (hi - lo <= RADIX_INSERTION_THRESHOLD || depth >= RADIX_DEPTH_LIMIT) {
        if (stats != NULL) {
            stats->insertion_sorts++;
        }
        transformed_insertion_range(lines, items, lo, hi, depth);
        return;
    }

    if (stats != NULL) {
        stats->passes++;
        stats->classified += hi - lo;
    }
    for (i = lo; i < hi; i++) {
        counts[transformed_bucket(lines, &items[i], depth)]++;
    }
    for (b = 0; b < RADIX_BUCKETS; b++) {
        starts[b] = sum;
        next[b] = sum;
        sum += counts[b];
    }
    for (i = lo; i < hi; i++) {
        b = transformed_bucket(lines, &items[i], depth);
        aux[next[b]++] = items[i];
    }
    memcpy(&items[lo], &aux[lo], (hi - lo) * sizeof(items[0]));

    for (b = 1; b < RADIX_BUCKETS; b++) {
        size_t start = starts[b];
        size_t end = start + counts[b];

        if (end - start > 1) {
            transformed_radix_range(lines, items, aux, start, end, depth + 1U, stats);
        }
    }
}

static void
transformed_insertion_range(const struct rank_lines *lines, struct rank_line *items, size_t lo, size_t hi, size_t depth)
{
    size_t i;

    for (i = lo + 1U; i < hi; i++) {
        struct rank_line line = items[i];
        size_t left = lo;
        size_t right = i;
        size_t j;

        while (left < right) {
            size_t mid = left + (right - left) / 2U;

            if (compare_transformed_from_depth(lines, &line, &items[mid], depth) < 0) {
                right = mid;
            } else {
                left = mid + 1U;
            }
        }
        for (j = i; j > left; j--) {
            items[j] = items[j - 1U];
        }
        items[left] = line;
    }
}

static int
compare_transformed_from_depth(const struct rank_lines *lines, const struct rank_line *a, const struct rank_line *b, size_t depth)
{
    const struct rank_transformed_span *a_span = rank_line_transform(lines, a);
    const struct rank_transformed_span *b_span = rank_line_transform(lines, b);
    size_t a_pos = depth;
    size_t b_pos = depth;

    while (a_pos < a_span->len && b_pos < b_span->len) {
        if (a_span->ptr[a_pos] < b_span->ptr[b_pos]) {
            return -1;
        }
        if (a_span->ptr[a_pos] > b_span->ptr[b_pos]) {
            return 1;
        }
        a_pos++;
        b_pos++;
    }
    if (a_pos == a_span->len && b_pos == b_span->len) {
        return 0;
    }
    return a_pos == a_span->len ? -1 : 1;
}

static size_t
transformed_bucket(const struct rank_lines *lines, const struct rank_line *line, size_t depth)
{
    const struct rank_transformed_span *span = rank_line_transform(lines, line);

    if (depth >= span->len) {
        return 0;
    }
    return (size_t)span->ptr[depth] + 1U;
}

static void
transformed_key_radix_range(const struct rank_lines *lines, struct rank_line *items, struct rank_line *aux, size_t lo, size_t hi, size_t depth, struct rank_radix_stats *stats)
{
    size_t counts[RADIX_BUCKETS] = {0};
    size_t starts[RADIX_BUCKETS];
    size_t next[RADIX_BUCKETS];
    size_t sum = lo;
    size_t i;
    size_t b;

    if (hi - lo <= RADIX_INSERTION_THRESHOLD || depth >= RADIX_DEPTH_LIMIT) {
        if (stats != NULL) {
            stats->insertion_sorts++;
        }
        transformed_key_insertion_range(lines, items, lo, hi, depth);
        return;
    }

    if (stats != NULL) {
        stats->passes++;
        stats->classified += hi - lo;
    }
    for (i = lo; i < hi; i++) {
        counts[transformed_key_bucket(lines, &items[i], depth)]++;
    }
    for (b = 0; b < RADIX_BUCKETS; b++) {
        starts[b] = sum;
        next[b] = sum;
        sum += counts[b];
    }
    for (i = lo; i < hi; i++) {
        b = transformed_key_bucket(lines, &items[i], depth);
        aux[next[b]++] = items[i];
    }
    memcpy(&items[lo], &aux[lo], (hi - lo) * sizeof(items[0]));

    for (b = 1; b < RADIX_BUCKETS; b++) {
        size_t start = starts[b];
        size_t end = start + counts[b];

        if (end - start > 1) {
            transformed_key_radix_range(lines, items, aux, start, end, depth + 1U, stats);
        }
    }
}

static void
transformed_key_insertion_range(const struct rank_lines *lines, struct rank_line *items, size_t lo, size_t hi, size_t depth)
{
    size_t i;

    for (i = lo + 1U; i < hi; i++) {
        struct rank_line line = items[i];
        size_t left = lo;
        size_t right = i;
        size_t j;

        while (left < right) {
            size_t mid = left + (right - left) / 2U;

            if (compare_transformed_key_from_depth(lines, &line, &items[mid], depth) < 0) {
                right = mid;
            } else {
                left = mid + 1U;
            }
        }
        for (j = i; j > left; j--) {
            items[j] = items[j - 1U];
        }
        items[left] = line;
    }
}

static int
compare_transformed_key_from_depth(const struct rank_lines *lines, const struct rank_line *a, const struct rank_line *b, size_t depth)
{
    const struct rank_transformed_span *a_span = rank_line_key_transform(lines, a, 0);
    const struct rank_transformed_span *b_span = rank_line_key_transform(lines, b, 0);
    size_t a_pos = depth;
    size_t b_pos = depth;

    while (a_pos < a_span->len && b_pos < b_span->len) {
        if (a_span->ptr[a_pos] < b_span->ptr[b_pos]) {
            return -1;
        }
        if (a_span->ptr[a_pos] > b_span->ptr[b_pos]) {
            return 1;
        }
        a_pos++;
        b_pos++;
    }
    if (a_pos == a_span->len && b_pos == b_span->len) {
        return 0;
    }
    return a_pos == a_span->len ? -1 : 1;
}

static size_t
transformed_key_bucket(const struct rank_lines *lines, const struct rank_line *line, size_t depth)
{
    const struct rank_transformed_span *span = rank_line_key_transform(lines, line, 0);

    if (depth >= span->len) {
        return 0;
    }
    return (size_t)span->ptr[depth] + 1U;
}

static bool
transformed_key_same(const struct rank_lines *lines, const struct rank_line *a, const struct rank_line *b)
{
    return compare_transformed_key_from_depth(lines, a, b, 0) == 0;
}

static void
sort_transformed_key_equal_groups(struct rank_lines *lines, struct rank_line *aux)
{
    size_t start = 0;
    size_t i;

    for (i = 1; i <= lines->len; i++) {
        if (i < lines->len && transformed_key_same(lines, &lines->items[start], &lines->items[i])) {
            continue;
        }
        if (i - start > 1) {
            transformed_radix_range(lines, lines->items, aux, start, i, 0, NULL);
        }
        start = i;
    }
}

/* Key-local-only text modifiers build no line transforms; GNU's last
   resort there is the raw whole line. */
static void
sort_transformed_key_equal_groups_raw(struct rank_lines *lines, struct rank_line *aux)
{
    size_t start = 0;
    size_t i;

    for (i = 1; i <= lines->len; i++) {
        if (i < lines->len && transformed_key_same(lines, &lines->items[start], &lines->items[i])) {
            continue;
        }
        if (i - start > 1) {
            line_radix_range(lines->items, aux, start, i, 0, NULL);
        }
        start = i;
    }
}

static bool
transformed_line_same(const struct rank_lines *lines, const struct rank_line *a, const struct rank_line *b)
{
    return compare_transformed_from_depth(lines, a, b, 0) == 0;
}

static void
compact_transformed_line_groups(struct rank_lines *lines)
{
    size_t out = 0;
    size_t i;

    for (i = 0; i < lines->len; i++) {
        if (out == 0 || !transformed_line_same(lines, &lines->items[out - 1U], &lines->items[i])) {
            lines->items[out++] = lines->items[i];
        }
    }
    lines->len = out;
}

static void
sort_transformed_line_equal_groups_original(struct rank_lines *lines, struct rank_line *aux)
{
    size_t start = 0;
    size_t i;

    for (i = 1; i <= lines->len; i++) {
        if (i < lines->len && transformed_line_same(lines, &lines->items[start], &lines->items[i])) {
            continue;
        }
        if (i - start > 1) {
            line_radix_range(lines->items, aux, start, i, 0, NULL);
        }
        start = i;
    }
}
