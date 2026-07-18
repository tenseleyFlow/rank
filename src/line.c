#include "line.h"

#include "rank_locale.h"
#include "sys/scan.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define RANK_READ_CHUNK 65536U

static bool read_stream(struct rank_lines *lines, const struct rank_options *options, int fd, const char *name);
static bool read_single_regular_mmap(struct rank_lines *lines, const struct rank_options *options, const char *name);
static void parse_records(struct rank_lines *lines, unsigned char delim, size_t start, size_t end, size_t *record_start);
static void reserve_for_file(struct rank_lines *lines, int fd);
static void reserve_data(struct rank_lines *lines, size_t cap);
static void reserve_items(struct rank_lines *lines, size_t cap);
static bool append_bytes(struct rank_lines *lines, const unsigned char *buf, size_t len);
static void add_line(struct rank_lines *lines, size_t off, size_t len);
static void update_monotonic(struct rank_lines *lines, size_t off, size_t len);
static int compare_data_spans(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len);
static void finalize_pointers(struct rank_lines *lines);
static struct rank_key_span extract_key_span(const struct rank_line *line, const struct rank_options *options, const struct rank_keydef *key);
static struct rank_key_span extract_explicit_key_span_cached(const struct rank_line *line, const struct rank_keydef *key, const size_t *field_starts, const size_t *field_ends);
static struct rank_key_span extract_blank_key_span_cached(const struct rank_line *line, const struct rank_options *options, const struct rank_keydef *key, const size_t *blank_starts, const size_t *text_starts, const size_t *field_ends);
static size_t max_blank_key_field(const struct rank_options *options);
static size_t max_explicit_key_field(const struct rank_options *options);
static void fill_explicit_field_cache(const struct rank_line *line, unsigned char sep, size_t max_field, size_t *field_starts, size_t *field_ends);
static void fill_blank_field_cache(const struct rank_line *line, size_t max_field, size_t *blank_starts, size_t *text_starts, size_t *field_ends);
static bool random_state_from_options(const struct rank_options *options, struct rank_md5_ctx *base);
static void compute_random_digest(const struct rank_md5_ctx *base, const struct rank_options *options, const struct rank_keydef *key, const unsigned char *text, size_t len, struct rank_md5_digest *out, unsigned char **scratch, size_t *scratch_cap);
static void ensure_scratch(unsigned char **scratch, size_t *scratch_cap, size_t needed);
static bool prepare_line_transforms(struct rank_lines *lines, const struct rank_options *options);
static bool prepare_key_transform(struct rank_lines *lines, const struct rank_options *options, const struct rank_keydef *key, size_t index, const struct rank_key_span *span);
static bool append_transform_span(struct rank_lines *lines, const unsigned char *text, size_t len, struct rank_transformed_span *out);
static bool append_filtered_span(struct rank_lines *lines, const unsigned char *text, size_t len, bool ignore_case, bool dictionary_order, bool ignore_nonprinting, struct rank_transformed_span *out);
static void finalize_transform_pointers(struct rank_lines *lines, const struct rank_options *options, bool key_transforms);
static bool transform_has_nul(const unsigned char *text, size_t len);
static bool reserve_transform_data(struct rank_lines *lines, size_t needed);
static bool global_text_modifier(const struct rank_options *options);
static bool key_text_modifier(const struct rank_options *options, const struct rank_keydef *key);
static bool filtered_keep(unsigned char byte, bool dictionary_order, bool ignore_nonprinting);
static unsigned char filtered_fold(unsigned char byte, bool ignore_case);
static size_t explicit_field_start(const struct rank_line *line, unsigned char sep, size_t field);
static size_t explicit_field_end(const struct rank_line *line, unsigned char sep, size_t field_start);
static size_t blank_field_start(const struct rank_line *line, size_t field, bool ignore_blanks);
static size_t blank_field_end(const struct rank_line *line, size_t field_start);

void
rank_lines_init(struct rank_lines *lines)
{
    lines->data = NULL;
    lines->data_len = 0;
    lines->data_cap = 0;
    lines->data_mapped = false;
    lines->items = NULL;
    lines->len = 0;
    lines->cap = 0;
    lines->output_reversed = false;
    lines->monotonic_valid = true;
    lines->monotonic_direction = 0;
    lines->key_spans = NULL;
    lines->line_transforms = NULL;
    lines->key_transforms = NULL;
    lines->transform_data = NULL;
    lines->transform_data_len = 0;
    lines->transform_data_cap = 0;
    lines->line_numbers = NULL;
    lines->key_numbers = NULL;
    lines->line_general_numbers = NULL;
    lines->key_general_numbers = NULL;
    lines->line_human_numbers = NULL;
    lines->key_human_numbers = NULL;
    lines->line_months = NULL;
    lines->key_months = NULL;
    lines->line_random = NULL;
    lines->key_random = NULL;
    lines->key_span_count = 0;
}

void
rank_lines_free(struct rank_lines *lines)
{
    if (lines->data_mapped) {
        (void)munmap(lines->data, lines->data_cap);
    } else {
        free(lines->data);
    }
    free(lines->items);
    free(lines->key_spans);
    free(lines->line_transforms);
    free(lines->key_transforms);
    free(lines->transform_data);
    free(lines->line_numbers);
    free(lines->key_numbers);
    free(lines->line_general_numbers);
    free(lines->key_general_numbers);
    free(lines->line_human_numbers);
    free(lines->key_human_numbers);
    free(lines->line_months);
    free(lines->key_months);
    free(lines->line_random);
    free(lines->key_random);
    rank_lines_init(lines);
}

bool
rank_lines_read_all(struct rank_lines *lines, const struct rank_options *options)
{
    size_t i;
    bool ok = true;

    if (options->operand_count == 0) {
        ok = read_stream(lines, options, STDIN_FILENO, "-");
    } else if (options->output_file == NULL && options->operand_count == 1 && strcmp(options->operands[0], "-") != 0
        && read_single_regular_mmap(lines, options, options->operands[0])) {
        ok = true;
    } else {
        for (i = 0; i < options->operand_count; i++) {
            const char *name = options->operands[i];
            int fd;

            if (strcmp(name, "-") == 0) {
                if (!read_stream(lines, options, STDIN_FILENO, name)) {
                    ok = false;
                }
                continue;
            }

            fd = open(name, O_RDONLY);
            if (fd < 0) {
                rank_diagf(options, "cannot read: %s: %s", name, strerror(errno));
                ok = false;
                continue;
            }
            reserve_for_file(lines, fd);
            if (!read_stream(lines, options, fd, name)) {
                ok = false;
            }
            if (close(fd) != 0) {
                rank_diagf(options, "error closing %s: %s", name, strerror(errno));
                ok = false;
            }
        }
    }

    finalize_pointers(lines);
    return ok;
}

bool
rank_lines_prepare_keys(struct rank_lines *lines, const struct rank_options *options)
{
    size_t *blank_starts = NULL;
    size_t *text_starts = NULL;
    size_t *field_ends = NULL;
    size_t *explicit_starts = NULL;
    size_t *explicit_ends = NULL;
    size_t max_blank_field = 0;
    size_t max_explicit_field = 0;
    bool want_key_numbers = options->sort_mode == RANK_SORT_NUMERIC;
    bool want_key_general_numbers = options->sort_mode == RANK_SORT_GENERAL_NUMERIC;
    bool want_key_human_numbers = options->sort_mode == RANK_SORT_HUMAN_NUMERIC;
    bool want_key_months = options->sort_mode == RANK_SORT_MONTH;
    bool want_key_random = options->sort_mode == RANK_SORT_RANDOM;
    bool locale_identity = rank_locale_collation_identity();
    bool want_line_transforms = options->sort_mode == RANK_SORT_BYTE && (!locale_identity || (locale_identity && global_text_modifier(options)));
    bool want_key_transforms = false;
    struct rank_md5_ctx random_base;
    unsigned char *random_scratch = NULL;
    size_t random_scratch_cap = 0;
    size_t i;
    size_t k;

    if (options->key_count == 0) {
        if (options->sort_mode == RANK_SORT_NUMERIC) {
            lines->line_numbers = rank_xrealloc(lines->line_numbers, lines->len * sizeof(lines->line_numbers[0]));
            for (i = 0; i < lines->len; i++) {
                lines->line_numbers[lines->items[i].ordinal] = rank_numeric_parse(lines->items[i].text, lines->items[i].len);
            }
        } else if (options->sort_mode == RANK_SORT_GENERAL_NUMERIC) {
            lines->line_general_numbers = rank_xrealloc(lines->line_general_numbers, lines->len * sizeof(lines->line_general_numbers[0]));
            for (i = 0; i < lines->len; i++) {
                lines->line_general_numbers[lines->items[i].ordinal] = rank_general_numeric_parse(lines->items[i].text, lines->items[i].len);
            }
        } else if (options->sort_mode == RANK_SORT_HUMAN_NUMERIC) {
            lines->line_human_numbers = rank_xrealloc(lines->line_human_numbers, lines->len * sizeof(lines->line_human_numbers[0]));
            for (i = 0; i < lines->len; i++) {
                lines->line_human_numbers[lines->items[i].ordinal] = rank_human_numeric_parse(lines->items[i].text, lines->items[i].len);
            }
        } else if (options->sort_mode == RANK_SORT_MONTH) {
            lines->line_months = rank_xrealloc(lines->line_months, lines->len * sizeof(lines->line_months[0]));
            for (i = 0; i < lines->len; i++) {
                lines->line_months[lines->items[i].ordinal] = rank_month_parse(lines->items[i].text, lines->items[i].len);
            }
        } else if (options->sort_mode == RANK_SORT_RANDOM) {
            if (!random_state_from_options(options, &random_base)) {
                return false;
            }
            lines->line_random = rank_xrealloc(lines->line_random, lines->len * sizeof(lines->line_random[0]));
            for (i = 0; i < lines->len; i++) {
                compute_random_digest(&random_base, options, NULL, lines->items[i].text, lines->items[i].len, &lines->line_random[lines->items[i].ordinal], &random_scratch, &random_scratch_cap);
            }
            free(random_scratch);
            random_scratch = NULL;
        }
        if (want_line_transforms && !prepare_line_transforms(lines, options)) {
            return false;
        }
        finalize_transform_pointers(lines, options, false);
        return true;
    }
    if (lines->len > SIZE_MAX / options->key_count) {
        rank_diag(options, "too many key spans");
        return false;
    }

    lines->key_span_count = lines->len * options->key_count;
    lines->key_spans = rank_xrealloc(lines->key_spans, lines->key_span_count * sizeof(lines->key_spans[0]));
    for (k = 0; k < options->key_count; k++) {
        if (options->keys[k].sort_mode == RANK_SORT_NUMERIC) {
            want_key_numbers = true;
        }
        if (options->keys[k].sort_mode == RANK_SORT_GENERAL_NUMERIC) {
            want_key_general_numbers = true;
        }
        if (options->keys[k].sort_mode == RANK_SORT_HUMAN_NUMERIC) {
            want_key_human_numbers = true;
        }
        if (options->keys[k].sort_mode == RANK_SORT_MONTH) {
            want_key_months = true;
        }
        if (options->keys[k].sort_mode == RANK_SORT_RANDOM) {
            want_key_random = true;
        }
        if ((options->keys[k].sort_mode == RANK_SORT_BYTE && options->sort_mode == RANK_SORT_BYTE)
            && (!locale_identity || (locale_identity && key_text_modifier(options, &options->keys[k])))) {
            want_key_transforms = true;
        }
    }
    if (want_line_transforms) {
        lines->line_transforms = rank_xrealloc(lines->line_transforms, lines->len * sizeof(lines->line_transforms[0]));
    }
    if (want_key_transforms) {
        lines->key_transforms = rank_xrealloc(lines->key_transforms, lines->key_span_count * sizeof(lines->key_transforms[0]));
    }
    if (options->sort_mode == RANK_SORT_NUMERIC) {
        lines->line_numbers = rank_xrealloc(lines->line_numbers, lines->len * sizeof(lines->line_numbers[0]));
    } else if (options->sort_mode == RANK_SORT_GENERAL_NUMERIC) {
        lines->line_general_numbers = rank_xrealloc(lines->line_general_numbers, lines->len * sizeof(lines->line_general_numbers[0]));
    } else if (options->sort_mode == RANK_SORT_HUMAN_NUMERIC) {
        lines->line_human_numbers = rank_xrealloc(lines->line_human_numbers, lines->len * sizeof(lines->line_human_numbers[0]));
    } else if (options->sort_mode == RANK_SORT_MONTH) {
        lines->line_months = rank_xrealloc(lines->line_months, lines->len * sizeof(lines->line_months[0]));
    } else if (options->sort_mode == RANK_SORT_RANDOM) {
        if (!random_state_from_options(options, &random_base)) {
            return false;
        }
        lines->line_random = rank_xrealloc(lines->line_random, lines->len * sizeof(lines->line_random[0]));
    }
    if (want_key_numbers) {
        lines->key_numbers = rank_xrealloc(lines->key_numbers, lines->key_span_count * sizeof(lines->key_numbers[0]));
    }
    if (want_key_general_numbers) {
        lines->key_general_numbers = rank_xrealloc(lines->key_general_numbers, lines->key_span_count * sizeof(lines->key_general_numbers[0]));
    }
    if (want_key_human_numbers) {
        lines->key_human_numbers = rank_xrealloc(lines->key_human_numbers, lines->key_span_count * sizeof(lines->key_human_numbers[0]));
    }
    if (want_key_months) {
        lines->key_months = rank_xrealloc(lines->key_months, lines->key_span_count * sizeof(lines->key_months[0]));
    }
    if (want_key_random) {
        if (options->sort_mode != RANK_SORT_RANDOM && !random_state_from_options(options, &random_base)) {
            return false;
        }
        lines->key_random = rank_xrealloc(lines->key_random, lines->key_span_count * sizeof(lines->key_random[0]));
    }
    if (options->has_field_separator && options->key_count > 1) {
        max_explicit_field = max_explicit_key_field(options);
        if (max_explicit_field < SIZE_MAX / sizeof(explicit_starts[0])) {
            explicit_starts = rank_xmalloc((max_explicit_field + 1U) * sizeof(explicit_starts[0]));
            explicit_ends = rank_xmalloc((max_explicit_field + 1U) * sizeof(explicit_ends[0]));
        }
    } else if (!options->has_field_separator && options->key_count > 1) {
        max_blank_field = max_blank_key_field(options);
        if (max_blank_field < SIZE_MAX / sizeof(blank_starts[0])) {
            blank_starts = rank_xmalloc((max_blank_field + 1U) * sizeof(blank_starts[0]));
            text_starts = rank_xmalloc((max_blank_field + 1U) * sizeof(text_starts[0]));
            field_ends = rank_xmalloc((max_blank_field + 1U) * sizeof(field_ends[0]));
        }
    }
    for (i = 0; i < lines->len; i++) {
        lines->items[i].key_index = i * options->key_count;
        if (options->sort_mode == RANK_SORT_NUMERIC) {
            lines->line_numbers[lines->items[i].ordinal] = rank_numeric_parse(lines->items[i].text, lines->items[i].len);
        } else if (options->sort_mode == RANK_SORT_GENERAL_NUMERIC) {
            lines->line_general_numbers[lines->items[i].ordinal] = rank_general_numeric_parse(lines->items[i].text, lines->items[i].len);
        } else if (options->sort_mode == RANK_SORT_HUMAN_NUMERIC) {
            lines->line_human_numbers[lines->items[i].ordinal] = rank_human_numeric_parse(lines->items[i].text, lines->items[i].len);
        } else if (options->sort_mode == RANK_SORT_MONTH) {
            lines->line_months[lines->items[i].ordinal] = rank_month_parse(lines->items[i].text, lines->items[i].len);
        } else if (options->sort_mode == RANK_SORT_RANDOM) {
            compute_random_digest(&random_base, options, NULL, lines->items[i].text, lines->items[i].len, &lines->line_random[lines->items[i].ordinal], &random_scratch, &random_scratch_cap);
        }
        if (want_line_transforms && !append_transform_span(lines, lines->items[i].text, lines->items[i].len, &lines->line_transforms[lines->items[i].ordinal])) {
            return false;
        }
        if (blank_starts != NULL) {
            fill_blank_field_cache(&lines->items[i], max_blank_field, blank_starts, text_starts, field_ends);
        } else if (explicit_starts != NULL) {
            fill_explicit_field_cache(&lines->items[i], options->field_separator, max_explicit_field, explicit_starts, explicit_ends);
        }
        for (k = 0; k < options->key_count; k++) {
            if (blank_starts != NULL) {
                lines->key_spans[lines->items[i].key_index + k] = extract_blank_key_span_cached(&lines->items[i], options, &options->keys[k], blank_starts, text_starts, field_ends);
            } else if (explicit_starts != NULL) {
                lines->key_spans[lines->items[i].key_index + k] = extract_explicit_key_span_cached(&lines->items[i], &options->keys[k], explicit_starts, explicit_ends);
            } else {
                lines->key_spans[lines->items[i].key_index + k] = extract_key_span(&lines->items[i], options, &options->keys[k]);
            }
            if (want_key_numbers && (options->sort_mode == RANK_SORT_NUMERIC || options->keys[k].sort_mode == RANK_SORT_NUMERIC)) {
                const struct rank_key_span *span = &lines->key_spans[lines->items[i].key_index + k];

                lines->key_numbers[lines->items[i].key_index + k] = rank_numeric_parse(span->ptr, span->len);
            }
            if (want_key_general_numbers && (options->sort_mode == RANK_SORT_GENERAL_NUMERIC || options->keys[k].sort_mode == RANK_SORT_GENERAL_NUMERIC)) {
                const struct rank_key_span *span = &lines->key_spans[lines->items[i].key_index + k];

                lines->key_general_numbers[lines->items[i].key_index + k] = rank_general_numeric_parse(span->ptr, span->len);
            }
            if (want_key_human_numbers && (options->sort_mode == RANK_SORT_HUMAN_NUMERIC || options->keys[k].sort_mode == RANK_SORT_HUMAN_NUMERIC)) {
                const struct rank_key_span *span = &lines->key_spans[lines->items[i].key_index + k];

                lines->key_human_numbers[lines->items[i].key_index + k] = rank_human_numeric_parse(span->ptr, span->len);
            }
            if (want_key_months && (options->sort_mode == RANK_SORT_MONTH || options->keys[k].sort_mode == RANK_SORT_MONTH)) {
                const struct rank_key_span *span = &lines->key_spans[lines->items[i].key_index + k];

                lines->key_months[lines->items[i].key_index + k] = rank_month_parse(span->ptr, span->len);
            }
            if (want_key_random && (options->sort_mode == RANK_SORT_RANDOM || options->keys[k].sort_mode == RANK_SORT_RANDOM)) {
                const struct rank_key_span *span = &lines->key_spans[lines->items[i].key_index + k];

                compute_random_digest(&random_base, options, &options->keys[k], span->ptr, span->len, &lines->key_random[lines->items[i].key_index + k], &random_scratch, &random_scratch_cap);
            }
            if (want_key_transforms && options->keys[k].sort_mode == RANK_SORT_BYTE && options->sort_mode == RANK_SORT_BYTE) {
                const struct rank_key_span *span = &lines->key_spans[lines->items[i].key_index + k];

                if (!prepare_key_transform(lines, options, &options->keys[k], lines->items[i].key_index + k, span)) {
                    return false;
                }
            }
        }
    }
    free(random_scratch);
    free(explicit_ends);
    free(explicit_starts);
    free(field_ends);
    free(text_starts);
    free(blank_starts);
    finalize_transform_pointers(lines, options, want_key_transforms);
    return true;
}

const struct rank_key_span *
rank_line_key_span(const struct rank_lines *lines, const struct rank_line *line, size_t key_id)
{
    return &lines->key_spans[line->key_index + key_id];
}

const struct rank_transformed_span *
rank_line_transform(const struct rank_lines *lines, const struct rank_line *line)
{
    if (lines->line_transforms == NULL) {
        return NULL;
    }
    return &lines->line_transforms[line->ordinal];
}

const struct rank_transformed_span *
rank_line_key_transform(const struct rank_lines *lines, const struct rank_line *line, size_t key_id)
{
    if (lines->key_transforms == NULL) {
        return NULL;
    }
    return &lines->key_transforms[line->key_index + key_id];
}

static bool
prepare_line_transforms(struct rank_lines *lines, const struct rank_options *options)
{
    size_t i;

    lines->line_transforms = rank_xrealloc(lines->line_transforms, lines->len * sizeof(lines->line_transforms[0]));
    for (i = 0; i < lines->len; i++) {
        bool ok = rank_locale_collation_identity() && global_text_modifier(options)
            ? append_filtered_span(lines, lines->items[i].text, lines->items[i].len, options->ignore_case, options->dictionary_order, options->ignore_nonprinting, &lines->line_transforms[lines->items[i].ordinal])
            : append_transform_span(lines, lines->items[i].text, lines->items[i].len, &lines->line_transforms[lines->items[i].ordinal]);

        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool
prepare_key_transform(struct rank_lines *lines, const struct rank_options *options, const struct rank_keydef *key, size_t index, const struct rank_key_span *span)
{
    if (rank_locale_collation_identity() && key_text_modifier(options, key)) {
        return append_filtered_span(lines, span->ptr, span->len,
            options->ignore_case || key->ignore_case,
            options->dictionary_order || key->dictionary_order,
            options->ignore_nonprinting || key->ignore_nonprinting,
            &lines->key_transforms[index]);
    }
    return append_transform_span(lines, span->ptr, span->len, &lines->key_transforms[index]);
}

static bool
append_filtered_span(struct rank_lines *lines, const unsigned char *text, size_t len, bool ignore_case, bool dictionary_order, bool ignore_nonprinting, struct rank_transformed_span *out)
{
    size_t i;
    size_t off;

    out->ptr = NULL;
    out->len = 0;
    out->off = 0;
    out->valid = false;
    if (!reserve_transform_data(lines, len == 0 ? 1U : len)) {
        return false;
    }
    off = lines->transform_data_len;
    if (ignore_case && !dictionary_order && !ignore_nonprinting) {
        rank_fold_upper(lines->transform_data + off, text, len);
        lines->transform_data_len += len;
    } else {
        for (i = 0; i < len; i++) {
            if (filtered_keep(text[i], dictionary_order, ignore_nonprinting)) {
                lines->transform_data[lines->transform_data_len++] = filtered_fold(text[i], ignore_case);
            }
        }
    }
    out->len = lines->transform_data_len - off;
    out->off = off;
    out->valid = true;
    return true;
}

static bool
append_transform_span(struct rank_lines *lines, const unsigned char *text, size_t len, struct rank_transformed_span *out)
{
    char *input;
    size_t needed;
    size_t off;

    out->ptr = NULL;
    out->len = 0;
    out->off = 0;
    out->valid = false;
    if (transform_has_nul(text, len)) {
        return true;
    }
    input = rank_xmalloc(len + 1U);
    memcpy(input, text, len);
    input[len] = '\0';
    needed = strxfrm(NULL, input, 0);
    if (needed == (size_t)-1 || !reserve_transform_data(lines, needed + 1U)) {
        free(input);
        return false;
    }
    off = lines->transform_data_len;
    (void)strxfrm((char *)lines->transform_data + off, input, needed + 1U);
    lines->transform_data_len += needed + 1U;
    out->len = needed;
    out->off = off;
    out->valid = true;
    free(input);
    return true;
}

static void
finalize_transform_pointers(struct rank_lines *lines, const struct rank_options *options, bool key_transforms)
{
    size_t i;
    size_t k;

    if (lines->line_transforms != NULL) {
        for (i = 0; i < lines->len; i++) {
            if (lines->line_transforms[i].valid) {
                lines->line_transforms[i].ptr = lines->transform_data + lines->line_transforms[i].off;
            }
        }
    }
    if (key_transforms && lines->key_transforms != NULL) {
        for (i = 0; i < lines->len; i++) {
            for (k = 0; k < options->key_count; k++) {
                struct rank_transformed_span *span = &lines->key_transforms[lines->items[i].key_index + k];

                if (span->valid) {
                    span->ptr = lines->transform_data + span->off;
                }
            }
        }
    }
}

static bool
transform_has_nul(const unsigned char *text, size_t len)
{
    return len > 0 && memchr(text, '\0', len) != NULL;
}

static bool
reserve_transform_data(struct rank_lines *lines, size_t needed)
{
    size_t total;
    size_t cap;

    if (SIZE_MAX - lines->transform_data_len < needed) {
        return false;
    }
    total = lines->transform_data_len + needed;
    if (total <= lines->transform_data_cap) {
        return true;
    }
    cap = lines->transform_data_cap == 0 ? 1024U : lines->transform_data_cap;
    while (cap < total) {
        if (cap > SIZE_MAX / 2U) {
            cap = total;
            break;
        }
        cap *= 2U;
    }
    lines->transform_data = rank_xrealloc(lines->transform_data, cap);
    lines->transform_data_cap = cap;
    return true;
}

static bool
global_text_modifier(const struct rank_options *options)
{
    return options->ignore_case || options->dictionary_order || options->ignore_nonprinting;
}

static bool
key_text_modifier(const struct rank_options *options, const struct rank_keydef *key)
{
    return global_text_modifier(options) || key->ignore_case || key->dictionary_order || key->ignore_nonprinting;
}

static bool
filtered_keep(unsigned char byte, bool dictionary_order, bool ignore_nonprinting)
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
filtered_fold(unsigned char byte, bool ignore_case)
{
    if (ignore_case && byte >= 'a' && byte <= 'z') {
        return (unsigned char)(byte - ('a' - 'A'));
    }
    return byte;
}

const struct rank_numeric_value *
rank_line_number(const struct rank_lines *lines, const struct rank_line *line)
{
    return &lines->line_numbers[line->ordinal];
}

const struct rank_numeric_value *
rank_line_key_number(const struct rank_lines *lines, const struct rank_line *line, size_t key_id)
{
    return &lines->key_numbers[line->key_index + key_id];
}

const struct rank_general_numeric_value *
rank_line_general_number(const struct rank_lines *lines, const struct rank_line *line)
{
    return &lines->line_general_numbers[line->ordinal];
}

const struct rank_general_numeric_value *
rank_line_key_general_number(const struct rank_lines *lines, const struct rank_line *line, size_t key_id)
{
    return &lines->key_general_numbers[line->key_index + key_id];
}

const struct rank_human_numeric_value *
rank_line_human_number(const struct rank_lines *lines, const struct rank_line *line)
{
    return &lines->line_human_numbers[line->ordinal];
}

const struct rank_human_numeric_value *
rank_line_key_human_number(const struct rank_lines *lines, const struct rank_line *line, size_t key_id)
{
    return &lines->key_human_numbers[line->key_index + key_id];
}

const struct rank_month_value *
rank_line_month(const struct rank_lines *lines, const struct rank_line *line)
{
    return &lines->line_months[line->ordinal];
}

const struct rank_month_value *
rank_line_key_month(const struct rank_lines *lines, const struct rank_line *line, size_t key_id)
{
    return &lines->key_months[line->key_index + key_id];
}

const struct rank_md5_digest *
rank_line_random(const struct rank_lines *lines, const struct rank_line *line)
{
    return &lines->line_random[line->ordinal];
}

const struct rank_md5_digest *
rank_line_key_random(const struct rank_lines *lines, const struct rank_line *line, size_t key_id)
{
    return &lines->key_random[line->key_index + key_id];
}

static bool
read_stream(struct rank_lines *lines, const struct rank_options *options, int fd, const char *name)
{
    unsigned char buf[RANK_READ_CHUNK];
    size_t record_start = lines->data_len;
    unsigned char delim = options->zero_terminated ? '\0' : '\n';

    for (;;) {
        ssize_t nread = read(fd, buf, sizeof(buf));
        size_t old_len;

        if (nread < 0) {
            rank_diagf(options, "read failed: %s: %s", name, strerror(errno));
            return false;
        }
        if (nread == 0) {
            break;
        }

        old_len = lines->data_len;
        if (!append_bytes(lines, buf, (size_t)nread)) {
            rank_diag(options, "input is too large");
            return false;
        }

        parse_records(lines, delim, old_len, lines->data_len, &record_start);
    }

    if (record_start < lines->data_len) {
        add_line(lines, record_start, lines->data_len - record_start);
    }

    return true;
}

static bool
read_single_regular_mmap(struct rank_lines *lines, const struct rank_options *options, const char *name)
{
    unsigned char delim = options->zero_terminated ? '\0' : '\n';
    size_t record_start = 0;
    struct stat st;
    void *mapped;
    size_t size;
    int fd;

    fd = open(name, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        (void)close(fd);
        return false;
    }

    size = (size_t)st.st_size;
    mapped = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        (void)close(fd);
        return false;
    }
    (void)close(fd);

    lines->data = mapped;
    lines->data_len = size;
    lines->data_cap = size;
    lines->data_mapped = true;
    size = size / 8U + 1U;
    if (size > 1024U * 1024U) {
        size = 1024U * 1024U;
    }
    reserve_items(lines, size);
    parse_records(lines, delim, 0, lines->data_len, &record_start);
    if (record_start < lines->data_len) {
        add_line(lines, record_start, lines->data_len - record_start);
    }
    return true;
}

static void
parse_records(struct rank_lines *lines, unsigned char delim, size_t start, size_t end, size_t *record_start)
{
    size_t i = start;

    while (i < end) {
        unsigned char *hit = memchr(lines->data + i, delim, end - i);

        if (hit == NULL) {
            break;
        }
        i = (size_t)(hit - lines->data);
        add_line(lines, *record_start, i - *record_start);
        *record_start = i + 1U;
        i++;
    }
}

static void
reserve_for_file(struct rank_lines *lines, int fd)
{
    struct stat st;
    size_t size;
    size_t estimated_records;

    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        return;
    }
    size = (size_t)st.st_size;
    if (SIZE_MAX - lines->data_len >= size) {
        reserve_data(lines, lines->data_len + size);
    }

    estimated_records = size / 8U + 1U;
    if (estimated_records > 1024U * 1024U) {
        estimated_records = 1024U * 1024U;
    }
    if (SIZE_MAX - lines->len >= estimated_records) {
        reserve_items(lines, lines->len + estimated_records);
    }
}

static void
reserve_data(struct rank_lines *lines, size_t cap)
{
    if (cap <= lines->data_cap) {
        return;
    }
    lines->data = rank_xrealloc(lines->data, cap);
    lines->data_cap = cap;
}

static void
reserve_items(struct rank_lines *lines, size_t cap)
{
    if (cap <= lines->cap) {
        return;
    }
    if (cap > SIZE_MAX / sizeof(lines->items[0])) {
        fprintf(stderr, "rank: too many input records\n");
        exit(RANK_EXIT_SERIOUS);
    }
    lines->items = rank_xrealloc(lines->items, cap * sizeof(lines->items[0]));
    lines->cap = cap;
}

static bool
append_bytes(struct rank_lines *lines, const unsigned char *buf, size_t len)
{
    if (SIZE_MAX - lines->data_len < len) {
        return false;
    }
    if (lines->data_len + len > lines->data_cap) {
        size_t cap = lines->data_cap == 0 ? RANK_READ_CHUNK : lines->data_cap;

        while (cap < lines->data_len + len) {
            if (cap > SIZE_MAX / 2U) {
                cap = lines->data_len + len;
                break;
            }
            cap *= 2U;
        }
        reserve_data(lines, cap);
    }

    memcpy(lines->data + lines->data_len, buf, len);
    lines->data_len += len;
    return true;
}

static void
add_line(struct rank_lines *lines, size_t off, size_t len)
{
    struct rank_line *line;

    update_monotonic(lines, off, len);

    if (lines->len == lines->cap) {
        size_t cap;

        if (lines->cap > SIZE_MAX / (2U * sizeof(lines->items[0]))) {
            fprintf(stderr, "rank: too many input records\n");
            exit(RANK_EXIT_SERIOUS);
        }
        cap = lines->cap == 0 ? 1024U : lines->cap * 2U;
        reserve_items(lines, cap);
    }

    line = &lines->items[lines->len];
    line->text = NULL;
    line->len = len;
    line->ordinal = lines->len;
    line->off = off;
    line->key_index = 0;
    lines->len++;
}

static void
update_monotonic(struct rank_lines *lines, size_t off, size_t len)
{
    const struct rank_line *previous;
    int cmp;

    if (!lines->monotonic_valid || lines->len == 0) {
        return;
    }

    previous = &lines->items[lines->len - 1U];
    cmp = compare_data_spans(lines->data + previous->off, previous->len, lines->data + off, len);
    if (cmp == 0) {
        return;
    }
    if (lines->monotonic_direction == 0) {
        lines->monotonic_direction = cmp < 0 ? -1 : 1;
        return;
    }
    if ((lines->monotonic_direction < 0 && cmp > 0) || (lines->monotonic_direction > 0 && cmp < 0)) {
        lines->monotonic_valid = false;
    }
}

static int
compare_data_spans(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len)
{
    size_t common = a_len < b_len ? a_len : b_len;
    int result = 0;

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
finalize_pointers(struct rank_lines *lines)
{
    size_t i;

    for (i = 0; i < lines->len; i++) {
        lines->items[i].text = lines->data + lines->items[i].off;
    }
}

static struct rank_key_span
extract_key_span(const struct rank_line *line, const struct rank_options *options, const struct rank_keydef *key)
{
    size_t start;
    size_t limit;
    bool ignore_start_blanks = options->ignore_leading_blanks || key->ignore_start_blanks;
    bool ignore_end_blanks = options->ignore_leading_blanks || key->ignore_end_blanks;
    struct rank_key_span span;

    if (options->has_field_separator) {
        start = explicit_field_start(line, options->field_separator, key->start_field);
        limit = key->has_end ? explicit_field_start(line, options->field_separator, key->end_field) : line->len;
        if (key->has_end) {
            limit = explicit_field_end(line, options->field_separator, limit);
        }
    } else {
        start = blank_field_start(line, key->start_field, ignore_start_blanks);
        limit = key->has_end ? blank_field_start(line, key->end_field, ignore_end_blanks) : line->len;
        if (key->has_end) {
            limit = blank_field_end(line, limit);
        }
    }

    if (key->has_start_char) {
        size_t add = key->start_char == 0 ? 0 : key->start_char - 1U;

        start = add > line->len - start ? line->len : start + add;
    }
    if (key->has_end && key->has_end_char && key->end_char > 0) {
        size_t field_start;
        size_t add = key->end_char;

        if (options->has_field_separator) {
            field_start = explicit_field_start(line, options->field_separator, key->end_field);
        } else {
            field_start = blank_field_start(line, key->end_field, ignore_end_blanks);
        }
        limit = add > line->len - field_start ? line->len : field_start + add;
    }
    if (start > line->len) {
        start = line->len;
    }
    if (limit > line->len) {
        limit = line->len;
    }
    if (limit < start) {
        limit = start;
    }

    span.ptr = line->text + start;
    span.len = limit - start;
    return span;
}

static struct rank_key_span
extract_explicit_key_span_cached(const struct rank_line *line, const struct rank_keydef *key, const size_t *field_starts, const size_t *field_ends)
{
    size_t start = field_starts[key->start_field];
    size_t limit = key->has_end ? field_ends[key->end_field] : line->len;
    struct rank_key_span span;

    if (key->has_start_char) {
        size_t add = key->start_char == 0 ? 0 : key->start_char - 1U;

        start = add > line->len - start ? line->len : start + add;
    }
    if (key->has_end && key->has_end_char && key->end_char > 0) {
        size_t field_start = field_starts[key->end_field];
        size_t add = key->end_char;

        limit = add > line->len - field_start ? line->len : field_start + add;
    }
    if (start > line->len) {
        start = line->len;
    }
    if (limit > line->len) {
        limit = line->len;
    }
    if (limit < start) {
        limit = start;
    }

    span.ptr = line->text + start;
    span.len = limit - start;
    return span;
}

static struct rank_key_span
extract_blank_key_span_cached(const struct rank_line *line, const struct rank_options *options, const struct rank_keydef *key, const size_t *blank_starts, const size_t *text_starts, const size_t *field_ends)
{
    bool ignore_start_blanks = options->ignore_leading_blanks || key->ignore_start_blanks;
    bool ignore_end_blanks = options->ignore_leading_blanks || key->ignore_end_blanks;
    size_t start = ignore_start_blanks ? text_starts[key->start_field] : blank_starts[key->start_field];
    size_t limit = key->has_end ? field_ends[key->end_field] : line->len;
    struct rank_key_span span;

    if (key->has_start_char) {
        size_t add = key->start_char == 0 ? 0 : key->start_char - 1U;

        start = add > line->len - start ? line->len : start + add;
    }
    if (key->has_end && key->has_end_char && key->end_char > 0) {
        size_t field_start = ignore_end_blanks ? text_starts[key->end_field] : blank_starts[key->end_field];
        size_t add = key->end_char;

        limit = add > line->len - field_start ? line->len : field_start + add;
    } else {
        (void)ignore_end_blanks;
    }
    if (start > line->len) {
        start = line->len;
    }
    if (limit > line->len) {
        limit = line->len;
    }
    if (limit < start) {
        limit = start;
    }

    span.ptr = line->text + start;
    span.len = limit - start;
    return span;
}

static size_t
max_blank_key_field(const struct rank_options *options)
{
    size_t max_field = 0;
    size_t i;

    for (i = 0; i < options->key_count; i++) {
        const struct rank_keydef *key = &options->keys[i];

        if (key->start_field > max_field) {
            max_field = key->start_field;
        }
        if (key->has_end && key->end_field > max_field) {
            max_field = key->end_field;
        }
    }
    return max_field;
}

static size_t
max_explicit_key_field(const struct rank_options *options)
{
    return max_blank_key_field(options);
}

static void
fill_explicit_field_cache(const struct rank_line *line, unsigned char sep, size_t max_field, size_t *field_starts, size_t *field_ends)
{
    size_t field;
    size_t i = 0;

    for (field = 0; field <= max_field; field++) {
        field_starts[field] = line->len;
        field_ends[field] = line->len;
    }
    if (max_field == 0) {
        return;
    }

    field = 1;
    field_starts[field] = 0;
    while (i < line->len && field <= max_field) {
        const unsigned char *hit = memchr(line->text + i, sep, line->len - i);
        size_t pos;

        if (hit == NULL) {
            break;
        }
        pos = (size_t)(hit - line->text);
        field_ends[field] = pos;
        field++;
        if (field <= max_field) {
            field_starts[field] = pos + 1U;
        }
        i = pos + 1U;
    }
    if (field <= max_field) {
        field_ends[field] = line->len;
    }
}

static void
fill_blank_field_cache(const struct rank_line *line, size_t max_field, size_t *blank_starts, size_t *text_starts, size_t *field_ends)
{
    size_t i = 0;
    size_t field;

    for (field = 0; field <= max_field; field++) {
        blank_starts[field] = line->len;
        text_starts[field] = line->len;
        field_ends[field] = line->len;
    }

    field = 0;
    while (i < line->len && field < max_field) {
        size_t blanks = i;

        i += rank_scan_nonblank(line->text + i, line->len - i);
        if (i == line->len) {
            return;
        }
        field++;
        blank_starts[field] = blanks;
        text_starts[field] = i;
        i += rank_scan_blank(line->text + i, line->len - i);
        field_ends[field] = i;
    }
}

/* GNU seeds a shared MD5 state with 16 bytes: from --random-source when
   given (only the first 16 bytes; fewer is a fatal end-of-file), else
   from system randomness. Each key's ordering hash continues from a
   copy of that state. */
static bool
random_state_from_options(const struct rank_options *options, struct rank_md5_ctx *base)
{
    unsigned char seed[16];
    const char *name = options->random_source;
    size_t got = 0;
    int fd;

    fd = open(name != NULL ? name : "/dev/urandom", O_RDONLY);
    if (fd < 0) {
        rank_diagf(options, "open failed: %s: %s", name != NULL ? name : "getrandom", strerror(errno));
        return false;
    }
    while (got < sizeof(seed)) {
        ssize_t nread = read(fd, seed + got, sizeof(seed) - got);

        if (nread < 0) {
            rank_diagf(options, "read failed: %s: %s", name != NULL ? name : "getrandom", strerror(errno));
            (void)close(fd);
            return false;
        }
        if (nread == 0) {
            rank_diagf(options, "'%s': end of file", name != NULL ? name : "getrandom");
            (void)close(fd);
            return false;
        }
        got += (size_t)nread;
    }
    if (close(fd) != 0) {
        rank_diagf(options, "close failed: %s: %s", name != NULL ? name : "getrandom", strerror(errno));
        return false;
    }
    rank_md5_init(base);
    rank_md5_update(base, seed, sizeof(seed));
    return true;
}

static void
ensure_scratch(unsigned char **scratch, size_t *scratch_cap, size_t needed)
{
    if (needed <= *scratch_cap) {
        return;
    }
    *scratch = rank_xrealloc(*scratch, needed);
    *scratch_cap = needed;
}

/* GNU compare_random hashes the comparison basis of the key: raw bytes
   in identity collation, translated bytes when f/d/i modifiers apply,
   and in hard locales the strxfrm of each NUL-terminated segment plus
   its terminating NUL. */
static void
compute_random_digest(const struct rank_md5_ctx *base, const struct rank_options *options, const struct rank_keydef *key, const unsigned char *text, size_t len, struct rank_md5_digest *out, unsigned char **scratch, size_t *scratch_cap)
{
    struct rank_md5_ctx ctx = *base;
    bool fold_case = options->ignore_case || (key != NULL && key->ignore_case);
    bool dictionary = options->dictionary_order || (key != NULL && key->dictionary_order);
    bool nonprinting = options->ignore_nonprinting || (key != NULL && key->ignore_nonprinting);

    if (!rank_locale_collation_identity()) {
        size_t copy_len = len + 1U;
        size_t pos = 0;

        ensure_scratch(scratch, scratch_cap, copy_len);
        memcpy(*scratch, text, len);
        (*scratch)[len] = '\0';
        while (pos < copy_len) {
            const char *seg = (const char *)*scratch + pos;
            size_t seg_len = strlen(seg);
            size_t needed = strxfrm(NULL, seg, 0);

            ensure_scratch(scratch, scratch_cap, copy_len + needed + 1U);
            seg = (const char *)*scratch + pos;
            (void)strxfrm((char *)*scratch + copy_len, seg, needed + 1U);
            rank_md5_update(&ctx, *scratch + copy_len, needed + 1U);
            pos += seg_len + 1U;
        }
    } else if (fold_case || dictionary || nonprinting) {
        unsigned char chunk[256];
        size_t n = 0;
        size_t i;

        for (i = 0; i < len; i++) {
            if (filtered_keep(text[i], dictionary, nonprinting)) {
                chunk[n++] = filtered_fold(text[i], fold_case);
                if (n == sizeof(chunk)) {
                    rank_md5_update(&ctx, chunk, n);
                    n = 0;
                }
            }
        }
        if (n > 0) {
            rank_md5_update(&ctx, chunk, n);
        }
    } else {
        rank_md5_update(&ctx, text, len);
    }
    rank_md5_final(&ctx, out->bytes);
}

static size_t
explicit_field_start(const struct rank_line *line, unsigned char sep, size_t field)
{
    size_t current = 1;
    size_t i = 0;

    if (field == 1) {
        return 0;
    }
    while (i < line->len) {
        const unsigned char *hit = memchr(line->text + i, sep, line->len - i);

        if (hit == NULL) {
            return line->len;
        }
        i = (size_t)(hit - line->text) + 1U;
        current++;
        if (current == field) {
            return i;
        }
    }
    return line->len;
}

static size_t
explicit_field_end(const struct rank_line *line, unsigned char sep, size_t field_start)
{
    const unsigned char *hit;

    if (field_start >= line->len) {
        return line->len;
    }
    hit = memchr(line->text + field_start, sep, line->len - field_start);
    return hit == NULL ? line->len : (size_t)(hit - line->text);
}

static size_t
blank_field_start(const struct rank_line *line, size_t field, bool ignore_blanks)
{
    size_t i = 0;
    size_t current = 0;

    while (i < line->len) {
        size_t blanks = i;

        i += rank_scan_nonblank(line->text + i, line->len - i);
        if (i == line->len) {
            return line->len;
        }
        current++;
        if (current == field) {
            return ignore_blanks ? i : blanks;
        }
        i += rank_scan_blank(line->text + i, line->len - i);
    }
    return line->len;
}

static size_t
blank_field_end(const struct rank_line *line, size_t field_start)
{
    size_t i = field_start;

    i += rank_scan_nonblank(line->text + i, line->len - i);
    i += rank_scan_blank(line->text + i, line->len - i);
    return i;
}
