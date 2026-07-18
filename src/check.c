#include "check.h"

#include "cmp.h"
#include "numeric.h"
#include "rank_locale.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RANK_CHECK_READ_CHUNK 65536U

struct check_record {
    unsigned char *data;
    size_t len;
    size_t cap;
    size_t number;
};

enum check_fast_kind {
    CHECK_FAST_NONE = 0,
    CHECK_FAST_BYTES,
    CHECK_FAST_KEY
};

struct check_key_cache {
    struct rank_key_span span;
    struct rank_numeric_value number;
    struct rank_general_numeric_value general_number;
    struct rank_human_numeric_value human_number;
    struct rank_month_value month;
};

struct check_compare {
    struct rank_lines lines;
    enum check_fast_kind fast;
    enum rank_sort_mode key_mode;
    struct check_key_cache prev_key;
    bool prev_key_valid;
};

static const char *check_input_name(const struct rank_options *options);
static void check_disorder_diag(const struct rank_options *options, const char *name, const unsigned char *text, size_t len, size_t record_number);
static int check_fd(const struct rank_options *options, int fd, const char *name);
static void check_compare_init(struct check_compare *compare, const struct rank_options *options);
static void check_compare_free(struct check_compare *compare);
static enum check_fast_kind check_fast_kind_from_options(const struct rank_options *options, enum rank_sort_mode *key_mode);
static bool check_key_records_disordered(const struct rank_options *options, struct check_compare *compare, const struct check_record *previous, const struct check_record *current);
static void check_key_cache_fill(const struct rank_options *options, enum rank_sort_mode mode, const struct check_record *record, struct check_key_cache *cache);
static bool check_record_append(struct check_record *record, const unsigned char *data, size_t len);
static bool check_process_record(const struct rank_options *options, const char *name, struct check_compare *compare, struct check_record *previous, struct check_record *current, bool *have_previous, int *status);
static bool check_records_disordered(const struct rank_options *options, struct check_compare *compare, const struct check_record *previous, const struct check_record *current, int *status);
static int check_compare_bytes(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len);
static void check_record_free(struct check_record *record);
static void check_record_promote(struct check_record *previous, struct check_record *current);
static void check_record_reset(struct check_record *record);

void
rank_check_module_present(void)
{
}

bool
rank_check_options_valid(const struct rank_options *options)
{
    if (options->check_mode != RANK_CHECK_NONE && options->operand_count > 1) {
        rank_diagf(options, "extra operand '%s' not allowed with -c", options->operands[1]);
        return false;
    }
    return true;
}

int
rank_check_lines(const struct rank_lines *lines, const struct rank_options *options)
{
    struct rank_cmp_context cmp;
    size_t i;

    if (lines->len < 2) {
        return RANK_EXIT_SUCCESS;
    }
    rank_cmp_context_init(&cmp, options, lines);
    for (i = 1; i < lines->len; i++) {
        int result = rank_compare_lines(&cmp, &lines->items[i - 1U], &lines->items[i]);

        if (result > 0 || (options->unique && rank_compare_unique(&cmp, &lines->items[i - 1U], &lines->items[i]) == 0)) {
            if (options->check_mode == RANK_CHECK_DIAGNOSE_FIRST) {
                check_disorder_diag(options, check_input_name(options), lines->items[i].text, lines->items[i].len, i + 1U);
            }
            return RANK_EXIT_DISORDER;
        }
    }
    return RANK_EXIT_SUCCESS;
}

int
rank_check_stream(const struct rank_options *options)
{
    int status;
    int fd;

    if (options->operand_count == 0 || strcmp(options->operands[0], "-") == 0) {
        return check_fd(options, STDIN_FILENO, "-");
    }

    fd = open(options->operands[0], O_RDONLY);
    if (fd < 0) {
        rank_diagf(options, "cannot read: %s: %s", options->operands[0], strerror(errno));
        return RANK_EXIT_SERIOUS;
    }
    status = check_fd(options, fd, options->operands[0]);
    if (close(fd) != 0 && status == RANK_EXIT_SUCCESS) {
        rank_diagf(options, "error closing %s: %s", options->operands[0], strerror(errno));
        status = RANK_EXIT_SERIOUS;
    }
    return status;
}

static int
check_fd(const struct rank_options *options, int fd, const char *name)
{
    unsigned char buf[RANK_CHECK_READ_CHUNK];
    unsigned char delim = options->zero_terminated ? '\0' : '\n';
    struct check_record previous = {0};
    struct check_record current = {0};
    struct check_compare compare;
    bool have_previous = false;
    int status = RANK_EXIT_SUCCESS;

    check_compare_init(&compare, options);

    for (;;) {
        ssize_t nread = read(fd, buf, sizeof(buf));
        size_t pos = 0;

        if (nread < 0) {
            rank_diagf(options, "read failed: %s: %s", name, strerror(errno));
            status = RANK_EXIT_SERIOUS;
            break;
        }
        if (nread == 0) {
            break;
        }
        while (pos < (size_t)nread) {
            unsigned char *hit = memchr(buf + pos, delim, (size_t)nread - pos);
            size_t take = hit == NULL ? (size_t)nread - pos : (size_t)(hit - (buf + pos));

            if (!check_record_append(&current, buf + pos, take)) {
                rank_diag(options, "input is too large");
                status = RANK_EXIT_SERIOUS;
                goto done;
            }
            pos += take;
            if (hit != NULL) {
                current.number++;
                if (!check_process_record(options, name, &compare, &previous, &current, &have_previous, &status)) {
                    goto done;
                }
                pos++;
            }
        }
    }
    if (status == RANK_EXIT_SUCCESS && current.len > 0) {
        current.number++;
        (void)check_process_record(options, name, &compare, &previous, &current, &have_previous, &status);
    }

done:
    check_compare_free(&compare);
    check_record_free(&current);
    check_record_free(&previous);
    return status;
}

static const char *
check_input_name(const struct rank_options *options)
{
    if (options->operand_count == 1) {
        return options->operands[0];
    }
    return "-";
}

static bool
check_record_append(struct check_record *record, const unsigned char *data, size_t len)
{
    if (len == 0) {
        return true;
    }
    if (SIZE_MAX - record->len < len) {
        return false;
    }
    if (record->len + len > record->cap) {
        size_t cap = record->cap == 0 ? RANK_CHECK_READ_CHUNK : record->cap;

        while (cap < record->len + len) {
            if (cap > SIZE_MAX / 2U) {
                cap = record->len + len;
                break;
            }
            cap *= 2U;
        }
        record->data = rank_xrealloc(record->data, cap);
        record->cap = cap;
    }
    memcpy(record->data + record->len, data, len);
    record->len += len;
    return true;
}

static bool
check_process_record(const struct rank_options *options, const char *name, struct check_compare *compare, struct check_record *previous, struct check_record *current, bool *have_previous, int *status)
{
    bool disordered = false;

    if (*have_previous) {
        disordered = check_records_disordered(options, compare, previous, current, status);
        if (*status != RANK_EXIT_SUCCESS) {
            return false;
        }
    }
    if (disordered) {
        if (options->check_mode == RANK_CHECK_DIAGNOSE_FIRST) {
            check_disorder_diag(options, name, current->data, current->len, current->number);
        }
        *status = RANK_EXIT_DISORDER;
        return false;
    }

    check_record_promote(previous, current);
    *have_previous = true;
    return true;
}

static enum check_fast_kind
check_fast_kind_from_options(const struct rank_options *options, enum rank_sort_mode *key_mode)
{
    const struct rank_keydef *key;
    enum rank_sort_mode mode;

    if (!rank_locale_collation_identity()) {
        return CHECK_FAST_NONE;
    }
    if (options->key_count == 0) {
        if (options->sort_mode == RANK_SORT_BYTE
            && !options->ignore_case && !options->dictionary_order && !options->ignore_nonprinting) {
            return CHECK_FAST_BYTES;
        }
        return CHECK_FAST_NONE;
    }
    if (options->key_count != 1) {
        return CHECK_FAST_NONE;
    }
    key = &options->keys[0];
    if (!(key->start_field > 0 && !key->has_start_char && key->has_end && key->end_field == key->start_field && !key->has_end_char)) {
        return CHECK_FAST_NONE;
    }
    if (options->has_field_separator && (options->ignore_leading_blanks || key->ignore_start_blanks || key->ignore_end_blanks)) {
        return CHECK_FAST_NONE;
    }
    mode = key->sort_mode != RANK_SORT_BYTE ? key->sort_mode : options->sort_mode;
    switch (mode) {
    case RANK_SORT_BYTE:
        if (options->ignore_case || options->dictionary_order || options->ignore_nonprinting
            || key->ignore_case || key->dictionary_order || key->ignore_nonprinting) {
            return CHECK_FAST_NONE;
        }
        break;
    case RANK_SORT_NUMERIC:
    case RANK_SORT_GENERAL_NUMERIC:
    case RANK_SORT_HUMAN_NUMERIC:
    case RANK_SORT_MONTH:
    case RANK_SORT_VERSION:
        break;
    default:
        return CHECK_FAST_NONE;
    }
    *key_mode = mode;
    return CHECK_FAST_KEY;
}

static void
check_key_cache_fill(const struct rank_options *options, enum rank_sort_mode mode, const struct check_record *record, struct check_key_cache *cache)
{
    cache->span = rank_simple_key_span(record->data, record->len, options);
    switch (mode) {
    case RANK_SORT_NUMERIC:
        cache->number = rank_numeric_parse(cache->span.ptr, cache->span.len);
        break;
    case RANK_SORT_GENERAL_NUMERIC:
        cache->general_number = rank_general_numeric_parse(cache->span.ptr, cache->span.len);
        break;
    case RANK_SORT_HUMAN_NUMERIC:
        cache->human_number = rank_human_numeric_parse(cache->span.ptr, cache->span.len);
        break;
    case RANK_SORT_MONTH:
        cache->month = rank_month_parse(cache->span.ptr, cache->span.len);
        break;
    default:
        break;
    }
}

/* The record buffers swap roles on promote, so the cached spans and
   parse results for the current record stay valid when it becomes the
   previous record. */
static bool
check_key_records_disordered(const struct rank_options *options, struct check_compare *compare, const struct check_record *previous, const struct check_record *current)
{
    struct check_key_cache cur;
    bool key_equal;
    int result;

    if (!compare->prev_key_valid) {
        check_key_cache_fill(options, compare->key_mode, previous, &compare->prev_key);
        compare->prev_key_valid = true;
    }
    check_key_cache_fill(options, compare->key_mode, current, &cur);

    switch (compare->key_mode) {
    case RANK_SORT_NUMERIC:
        result = rank_numeric_compare_values(&compare->prev_key.number, &cur.number);
        break;
    case RANK_SORT_GENERAL_NUMERIC:
        result = rank_general_numeric_compare_values(&compare->prev_key.general_number, &cur.general_number);
        break;
    case RANK_SORT_HUMAN_NUMERIC:
        result = rank_human_numeric_compare_values(&compare->prev_key.human_number, &cur.human_number);
        break;
    case RANK_SORT_MONTH:
        result = rank_month_compare_values(&compare->prev_key.month, &cur.month);
        break;
    case RANK_SORT_VERSION:
        result = rank_version_compare(compare->prev_key.span.ptr, compare->prev_key.span.len, cur.span.ptr, cur.span.len);
        break;
    default:
        result = check_compare_bytes(compare->prev_key.span.ptr, compare->prev_key.span.len, cur.span.ptr, cur.span.len);
        break;
    }
    if (options->keys[0].reverse) {
        result = -result;
    }
    key_equal = result == 0;
    if (result == 0 && !(options->stable || options->unique)) {
        result = check_compare_bytes(previous->data, previous->len, current->data, current->len);
    }
    if (options->reverse) {
        result = -result;
    }
    compare->prev_key = cur;
    return result > 0 || (options->unique && key_equal);
}

static bool
check_records_disordered(const struct rank_options *options, struct check_compare *compare, const struct check_record *previous, const struct check_record *current, int *status)
{
    struct rank_cmp_context cmp;
    int result;
    bool disordered;

    if (compare->fast == CHECK_FAST_BYTES) {
        result = check_compare_bytes(previous->data, previous->len, current->data, current->len);
        if (options->reverse) {
            result = -result;
        }
        return result > 0 || (options->unique && result == 0);
    }
    if (compare->fast == CHECK_FAST_KEY) {
        return check_key_records_disordered(options, compare, previous, current);
    }

    compare->lines.items[0].text = previous->data;
    compare->lines.items[0].len = previous->len;
    compare->lines.items[0].key_index = 0;
    compare->lines.items[1].text = current->data;
    compare->lines.items[1].len = current->len;
    compare->lines.items[1].key_index = 0;
    if (!rank_lines_prepare_keys(&compare->lines, options)) {
        *status = RANK_EXIT_SERIOUS;
        return false;
    }
    rank_cmp_context_init(&cmp, options, &compare->lines);
    result = rank_compare_lines(&cmp, &compare->lines.items[0], &compare->lines.items[1]);
    disordered = result > 0 || (options->unique && rank_compare_unique(&cmp, &compare->lines.items[0], &compare->lines.items[1]) == 0);
    return disordered;
}

static void
check_compare_init(struct check_compare *compare, const struct rank_options *options)
{
    compare->key_mode = RANK_SORT_BYTE;
    compare->fast = check_fast_kind_from_options(options, &compare->key_mode);
    compare->prev_key_valid = false;
    rank_lines_init(&compare->lines);
    compare->lines.items = rank_xmalloc(2U * sizeof(compare->lines.items[0]));
    compare->lines.len = 2;
    compare->lines.cap = 2;
    compare->lines.items[0].text = NULL;
    compare->lines.items[0].len = 0;
    compare->lines.items[0].ordinal = 0;
    compare->lines.items[0].off = 0;
    compare->lines.items[0].key_index = 0;
    compare->lines.items[1].text = NULL;
    compare->lines.items[1].len = 0;
    compare->lines.items[1].ordinal = 1;
    compare->lines.items[1].off = 0;
    compare->lines.items[1].key_index = 0;
}

static void
check_compare_free(struct check_compare *compare)
{
    rank_lines_free(&compare->lines);
}

static int
check_compare_bytes(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len)
{
    size_t common = a_len < b_len ? a_len : b_len;
    int result;

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

static void
check_record_free(struct check_record *record)
{
    free(record->data);
    check_record_reset(record);
}

static void
check_record_promote(struct check_record *previous, struct check_record *current)
{
    unsigned char *old_previous_data = previous->data;
    size_t old_previous_cap = previous->cap;

    *previous = *current;
    current->data = old_previous_data;
    current->len = 0;
    current->cap = old_previous_cap;
}

static void
check_record_reset(struct check_record *record)
{
    record->data = NULL;
    record->len = 0;
    record->cap = 0;
}

static void
check_disorder_diag(const struct rank_options *options, const char *name, const unsigned char *text, size_t len, size_t record_number)
{
    fprintf(stderr, "%s: %s:%zu: disorder: ", options->program_name, name, record_number);
    (void)fwrite(text, 1, len, stderr);
    fputc(options->zero_terminated ? '\0' : '\n', stderr);
}
