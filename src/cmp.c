#include "cmp.h"

#include "rank_locale.h"
#include "numeric.h"

#include <string.h>

static int compare_spans(struct rank_cmp_context *ctx, const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len, enum rank_sort_mode mode);
static int compare_modified_spans(struct rank_cmp_context *ctx, const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len, const struct rank_keydef *key);
static bool span_modifier_keep(unsigned char byte, bool dictionary_order, bool ignore_nonprinting);
static unsigned char span_modifier_fold(unsigned char byte, bool ignore_case);
static int compare_transformed_spans(const struct rank_transformed_span *a, const struct rank_transformed_span *b);
static int compare_random(const struct rank_md5_digest *a, const struct rank_md5_digest *b);
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
    if (ctx->options->sort_mode == RANK_SORT_NUMERIC) {
        ctx->calls++;
        return rank_numeric_compare_values(rank_line_number(ctx->lines, a), rank_line_number(ctx->lines, b));
    }
    if (ctx->options->sort_mode == RANK_SORT_GENERAL_NUMERIC) {
        ctx->calls++;
        return rank_general_numeric_compare_values(rank_line_general_number(ctx->lines, a), rank_line_general_number(ctx->lines, b));
    }
    if (ctx->options->sort_mode == RANK_SORT_HUMAN_NUMERIC) {
        ctx->calls++;
        return rank_human_numeric_compare_values(rank_line_human_number(ctx->lines, a), rank_line_human_number(ctx->lines, b));
    }
    if (ctx->options->sort_mode == RANK_SORT_MONTH) {
        ctx->calls++;
        return rank_month_compare_values(rank_line_month(ctx->lines, a), rank_line_month(ctx->lines, b));
    }
    if (ctx->options->sort_mode == RANK_SORT_VERSION) {
        ctx->calls++;
        return rank_version_compare(a->text, a->len, b->text, b->len);
    }
    if (ctx->options->sort_mode == RANK_SORT_RANDOM) {
        int result;

        ctx->calls++;
        result = compare_random(rank_line_random(ctx->lines, a), rank_line_random(ctx->lines, b));
        if (result != 0) {
            return result;
        }
    }
    if (ctx->options->sort_mode == RANK_SORT_BYTE) {
        const struct rank_transformed_span *a_transform = rank_line_transform(ctx->lines, a);
        const struct rank_transformed_span *b_transform = rank_line_transform(ctx->lines, b);

        if (a_transform != NULL && b_transform != NULL && a_transform->valid && b_transform->valid) {
            ctx->calls++;
            ctx->bytes += a_transform->len < b_transform->len ? a_transform->len : b_transform->len;
            return compare_transformed_spans(a_transform, b_transform);
        }
    }
    return compare_modified_spans(ctx, a->text, a->len, b->text, b->len, NULL);
}

int
rank_compare_lines(struct rank_cmp_context *ctx, const struct rank_line *a, const struct rank_line *b)
{
    int result = compare_keys(ctx, a, b);

    if (ctx->options->key_count == 0) {
        result = rank_compare_lines_ascending(ctx, a, b);
        if (result == 0 && ctx->options->sort_mode != RANK_SORT_BYTE && !(ctx->options->stable || ctx->options->unique)) {
            result = compare_spans(ctx, a->text, a->len, b->text, b->len, RANK_SORT_BYTE);
        }
    } else if (result == 0 && !(ctx->options->stable || ctx->options->unique)) {
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
        enum rank_sort_mode mode = ctx->options->keys[i].sort_mode == RANK_SORT_BYTE ? ctx->options->sort_mode : ctx->options->keys[i].sort_mode;
        int result;

        if (mode == RANK_SORT_NUMERIC) {
            ctx->calls++;
            result = rank_numeric_compare_values(rank_line_key_number(ctx->lines, a, i), rank_line_key_number(ctx->lines, b, i));
        } else if (mode == RANK_SORT_GENERAL_NUMERIC) {
            ctx->calls++;
            result = rank_general_numeric_compare_values(rank_line_key_general_number(ctx->lines, a, i), rank_line_key_general_number(ctx->lines, b, i));
        } else if (mode == RANK_SORT_HUMAN_NUMERIC) {
            ctx->calls++;
            result = rank_human_numeric_compare_values(rank_line_key_human_number(ctx->lines, a, i), rank_line_key_human_number(ctx->lines, b, i));
        } else if (mode == RANK_SORT_MONTH) {
            ctx->calls++;
            result = rank_month_compare_values(rank_line_key_month(ctx->lines, a, i), rank_line_key_month(ctx->lines, b, i));
        } else if (mode == RANK_SORT_VERSION) {
            ctx->calls++;
            result = rank_version_compare(a_key->ptr, a_key->len, b_key->ptr, b_key->len);
        } else if (mode == RANK_SORT_RANDOM) {
            ctx->calls++;
            result = compare_random(rank_line_key_random(ctx->lines, a, i), rank_line_key_random(ctx->lines, b, i));
            if (result == 0) {
                result = compare_modified_spans(ctx, a_key->ptr, a_key->len, b_key->ptr, b_key->len, &ctx->options->keys[i]);
            }
        } else {
            const struct rank_transformed_span *a_transform = rank_line_key_transform(ctx->lines, a, i);
            const struct rank_transformed_span *b_transform = rank_line_key_transform(ctx->lines, b, i);

            if (mode == RANK_SORT_BYTE && a_transform != NULL && b_transform != NULL && a_transform->valid && b_transform->valid) {
                ctx->calls++;
                ctx->bytes += a_transform->len < b_transform->len ? a_transform->len : b_transform->len;
                result = compare_transformed_spans(a_transform, b_transform);
            } else {
                result = compare_modified_spans(ctx, a_key->ptr, a_key->len, b_key->ptr, b_key->len, &ctx->options->keys[i]);
            }
        }

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
compare_modified_spans(struct rank_cmp_context *ctx, const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len, const struct rank_keydef *key)
{
    bool ignore_case = ctx->options->ignore_case || (key != NULL && key->ignore_case);
    bool dictionary_order = ctx->options->dictionary_order || (key != NULL && key->dictionary_order);
    bool ignore_nonprinting = ctx->options->ignore_nonprinting || (key != NULL && key->ignore_nonprinting);
    size_t a_pos = 0;
    size_t b_pos = 0;

    if (!ignore_case && !dictionary_order && !ignore_nonprinting) {
        return compare_spans(ctx, a, a_len, b, b_len, RANK_SORT_BYTE);
    }
    ctx->calls++;
    while (true) {
        unsigned char a_byte;
        unsigned char b_byte;

        while (a_pos < a_len && !span_modifier_keep(a[a_pos], dictionary_order, ignore_nonprinting)) {
            a_pos++;
        }
        while (b_pos < b_len && !span_modifier_keep(b[b_pos], dictionary_order, ignore_nonprinting)) {
            b_pos++;
        }
        if (a_pos == a_len || b_pos == b_len) {
            break;
        }
        a_byte = span_modifier_fold(a[a_pos], ignore_case);
        b_byte = span_modifier_fold(b[b_pos], ignore_case);
        ctx->bytes++;
        if (a_byte < b_byte) {
            return -1;
        }
        if (a_byte > b_byte) {
            return 1;
        }
        a_pos++;
        b_pos++;
    }
    while (a_pos < a_len && !span_modifier_keep(a[a_pos], dictionary_order, ignore_nonprinting)) {
        a_pos++;
    }
    while (b_pos < b_len && !span_modifier_keep(b[b_pos], dictionary_order, ignore_nonprinting)) {
        b_pos++;
    }
    if (a_pos == a_len && b_pos == b_len) {
        return 0;
    }
    return a_pos == a_len ? -1 : 1;
}

static bool
span_modifier_keep(unsigned char byte, bool dictionary_order, bool ignore_nonprinting)
{
    if (dictionary_order && !(byte == ' ' || byte == '\t' || (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z'))) {
        return false;
    }
    if (ignore_nonprinting && !(byte >= 0x20U && byte <= 0x7eU)) {
        return false;
    }
    return true;
}

static unsigned char
span_modifier_fold(unsigned char byte, bool ignore_case)
{
    if (ignore_case && byte >= 'a' && byte <= 'z') {
        return (unsigned char)(byte - ('a' - 'A'));
    }
    return byte;
}

static int
compare_transformed_spans(const struct rank_transformed_span *a, const struct rank_transformed_span *b)
{
    size_t common = a->len < b->len ? a->len : b->len;
    int result = 0;

    if (common > 0) {
        result = memcmp(a->ptr, b->ptr, common);
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

static int
compare_random(const struct rank_md5_digest *a, const struct rank_md5_digest *b)
{
    int result = memcmp(a->bytes, b->bytes, sizeof(a->bytes));

    if (result != 0) {
        return result < 0 ? -1 : 1;
    }
    return 0;
}

static int
compare_spans(struct rank_cmp_context *ctx, const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len, enum rank_sort_mode mode)
{
    size_t common = a_len < b_len ? a_len : b_len;

    ctx->calls++;
    ctx->bytes += common;
    (void)mode;
    return rank_locale_compare(a, a_len, b, b_len);
}
