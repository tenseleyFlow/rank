#include "cmp.h"

#include <string.h>

void
rank_cmp_context_init(struct rank_cmp_context *ctx, const struct rank_options *options)
{
    ctx->options = options;
    ctx->calls = 0;
    ctx->bytes = 0;
}

int
rank_compare_lines_ascending(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b)
{
    size_t common = a->len < b->len ? a->len : b->len;
    int result = 0;

    ctx->calls++;
    ctx->bytes += common;
    if (common > 0) {
        result = memcmp(a->text, b->text, common);
        if (result != 0) {
            return result < 0 ? -1 : 1;
        }
    }
    if (a->len < b->len) {
        return -1;
    }
    if (a->len > b->len) {
        return 1;
    }
    return 0;
}

int
rank_compare_lines(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b)
{
    int result = rank_compare_lines_ascending(ctx, a, b);

    if (ctx->options->reverse) {
        result = -result;
    }
    return result;
}
