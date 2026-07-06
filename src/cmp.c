#include "cmp.h"

#include <string.h>

static int compare_spans(struct rank_cmp_context *ctx, const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len);
static int compare_keys(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b);

void
rank_cmp_context_init(struct rank_cmp_context *ctx, const struct rank_options *options, const struct rank_lines *lines)
{
    ctx->options = options;
    ctx->lines = lines;
    ctx->calls = 0;
    ctx->bytes = 0;
}

int
rank_compare_lines_ascending(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b)
{
    return compare_spans(ctx, a->text, a->len, b->text, b->len);
}

int
rank_compare_lines(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b)
{
    int result = compare_keys(ctx, a, b);

    if (ctx->options->key_count == 0 || (result == 0 && !(ctx->options->stable || ctx->options->unique))) {
        result = rank_compare_lines_ascending(ctx, a, b);
    }

    if (ctx->options->reverse) {
        result = -result;
    }
    return result;
}

int
rank_compare_unique(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b)
{
    if (ctx->options->key_count > 0) {
        return compare_keys(ctx, a, b);
    }
    return rank_compare_lines_ascending(ctx, a, b);
}

static int
compare_keys(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b)
{
    size_t i;

    for (i = 0; i < ctx->options->key_count; i++) {
        const struct rank_key_span *a_key = rank_line_key_span(ctx->lines, a, i);
        const struct rank_key_span *b_key = rank_line_key_span(ctx->lines, b, i);
        int result = compare_spans(ctx, a_key->ptr, a_key->len, b_key->ptr, b_key->len);

        if (ctx->options->keys[i].reverse) {
            result = -result;
        }
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

static int
compare_spans(struct rank_cmp_context *ctx, const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len)
{
    size_t common = a_len < b_len ? a_len : b_len;
    int result = 0;

    ctx->calls++;
    ctx->bytes += common;
    if (common > 0) {
        result = memcmp(a, b, common);
        if (result != 0) {
            return result < 0 ? -1 : 1;
        }
    }
    if (a_len < b_len) {
        return -1;
    }
    if (a_len > b_len) {
        return 1;
    }
    return 0;
}
