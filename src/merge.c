#include "merge.h"

#include "cmp.h"
#include "line.h"
#include "numeric.h"
#include "output.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#define RANK_MERGE_READ_CHUNK 65536U
#define RANK_MERGE_IOV_MAX 1024

struct merge_run {
    size_t begin;
    size_t end;
    size_t pos;
};

struct merge_stream_run {
    int fd;
    const char *name;
    bool close_fd;
    bool eof;
    unsigned char in[RANK_MERGE_READ_CHUNK];
    size_t in_pos;
    size_t in_len;
    unsigned char *record;
    size_t len;
    size_t cap;
};

struct merge_head_cache {
    struct rank_numeric_value number;
    struct rank_general_numeric_value general_number;
    struct rank_human_numeric_value human_number;
    struct rank_month_value month;
    const unsigned char *key_ptr;
    size_t key_len;
};

enum merge_fast_mode {
    MERGE_FAST_NONE = 0,
    MERGE_FAST_NUMERIC,
    MERGE_FAST_GENERAL_NUMERIC,
    MERGE_FAST_HUMAN_NUMERIC,
    MERGE_FAST_MONTH,
    MERGE_FAST_KEY_NUMERIC,
    MERGE_FAST_KEY_GENERAL_NUMERIC,
    MERGE_FAST_KEY_HUMAN_NUMERIC,
    MERGE_FAST_KEY_MONTH,
    MERGE_FAST_KEY_VERSION,
    MERGE_FAST_VERSION
};

static bool read_merge_inputs(struct rank_lines *lines, struct merge_run **runs_out, size_t *run_count_out, const struct rank_options *options);
static bool append_merge_input(struct rank_lines *lines, struct merge_run *run, const struct rank_options *options, char **operand);
static bool merge_order_lines(struct rank_lines *lines, const struct rank_options *options, struct merge_run *runs, size_t run_count);
static bool merge_direct_output(struct rank_lines *lines, const struct rank_options *options, struct merge_run *runs, size_t run_count, enum merge_fast_mode mode);
static enum merge_fast_mode merge_fast_mode(const struct rank_options *options);
static bool merge_head_cache_init(struct merge_head_cache **caches_out, const struct rank_lines *lines, const struct rank_options *options, const struct merge_run *runs, size_t run_count, enum merge_fast_mode mode);
static void merge_head_cache_update(struct merge_head_cache *caches, const struct rank_lines *lines, const struct rank_options *options, const struct merge_run *runs, size_t run_id, enum merge_fast_mode mode);
static struct rank_key_span merge_extract_simple_key(const struct rank_line *line, const struct rank_options *options);
static size_t merge_blank_field_start(const struct rank_line *line, size_t field, bool ignore_blanks);
static size_t merge_blank_field_end(const struct rank_line *line, size_t field_start);
static bool merge_sort_blank(unsigned char byte);
static size_t merge_heap_build(size_t *heap, const struct merge_run *runs, size_t run_count);
static void merge_heap_sift_down(size_t *heap, size_t heap_len, size_t root, const struct rank_lines *lines, struct rank_cmp_context *cmp, const struct merge_run *runs);
static bool merge_run_less(const struct rank_lines *lines, struct rank_cmp_context *cmp, const struct merge_run *runs, size_t a, size_t b);
static size_t merge_fast_heap_build(size_t *heap, const struct merge_run *runs, size_t run_count);
static void merge_fast_heap_sift_down(size_t *heap, size_t heap_len, size_t root, const struct rank_lines *lines, const struct rank_options *options, const struct merge_run *runs, const struct merge_head_cache *caches, enum merge_fast_mode mode);
static bool merge_fast_run_less(const struct rank_lines *lines, const struct rank_options *options, const struct merge_run *runs, const struct merge_head_cache *caches, enum merge_fast_mode mode, size_t a, size_t b);
static int merge_head_cache_compare(const struct merge_head_cache *a, const struct merge_head_cache *b, enum merge_fast_mode mode);
static int merge_compare_bytes(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len);
static bool merge_fast_append_line(int fd, struct iovec *iov, int *iov_count, const struct rank_lines *lines, const struct rank_line *line, unsigned char delim, const unsigned char *delim_ptr);
static bool merge_fast_flush_iov(int fd, struct iovec *iov, int *iov_count);
static bool merge_can_stream_bytes(const struct rank_options *options);
static bool merge_stream_bytes(const struct rank_options *options);
static bool merge_stream_open_runs(struct merge_stream_run **runs_out, size_t *run_count_out, const struct rank_options *options);
static void merge_stream_close_runs(struct merge_stream_run *runs, size_t run_count);
static bool merge_stream_read_head(struct merge_stream_run *run, const struct rank_options *options, bool *has_record);
static bool merge_stream_append_record(struct merge_stream_run *run, const unsigned char *data, size_t len);
static void merge_stream_consume(struct merge_stream_run *run);
static size_t merge_stream_heap_build(size_t *heap, const struct merge_stream_run *runs, size_t run_count);
static void merge_stream_heap_sift_down(size_t *heap, size_t heap_len, size_t root, const struct merge_stream_run *runs, const struct rank_options *options);
static bool merge_stream_run_less(const struct merge_stream_run *runs, const struct rank_options *options, size_t a, size_t b);
static int merge_stream_compare_bytes(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len);
static bool merge_stream_write_record(FILE *stream, const struct merge_stream_run *run, unsigned char delim);

void
rank_merge_module_present(void)
{
}

bool
rank_merge_all(const struct rank_options *options)
{
    struct rank_lines lines;
    struct merge_run *runs = NULL;
    size_t run_count = 0;
    enum merge_fast_mode fast_mode;
    bool ok = true;

    if (merge_can_stream_bytes(options)) {
        return merge_stream_bytes(options);
    }

    rank_lines_init(&lines);
    fast_mode = merge_fast_mode(options);
    if (!read_merge_inputs(&lines, &runs, &run_count, options)) {
        ok = false;
    } else if (fast_mode != MERGE_FAST_NONE) {
        ok = merge_direct_output(&lines, options, runs, run_count, fast_mode);
    } else if (!rank_lines_prepare_keys(&lines, options)) {
        ok = false;
    } else if (!merge_order_lines(&lines, options, runs, run_count)) {
        ok = false;
    } else if (!rank_output_lines(&lines, options)) {
        ok = false;
    }
    free(runs);
    rank_lines_free(&lines);
    return ok;
}

static enum merge_fast_mode
merge_fast_mode(const struct rank_options *options)
{
    const struct rank_keydef *key;

    if (options->debug || options->has_field_separator) {
        return MERGE_FAST_NONE;
    }
    if (options->key_count == 0) {
        switch (options->sort_mode) {
        case RANK_SORT_NUMERIC:
            return MERGE_FAST_NUMERIC;
        case RANK_SORT_GENERAL_NUMERIC:
            return MERGE_FAST_GENERAL_NUMERIC;
        case RANK_SORT_HUMAN_NUMERIC:
            return MERGE_FAST_HUMAN_NUMERIC;
        case RANK_SORT_MONTH:
            return MERGE_FAST_MONTH;
        case RANK_SORT_VERSION:
            return MERGE_FAST_VERSION;
        default:
            return MERGE_FAST_NONE;
        }
    }
    if (options->key_count != 1) {
        return MERGE_FAST_NONE;
    }
    key = &options->keys[0];
    if ((key->sort_mode == RANK_SORT_NUMERIC || (key->sort_mode == RANK_SORT_BYTE && options->sort_mode == RANK_SORT_NUMERIC))
        && key->start_field > 0 && !key->has_start_char && key->has_end && key->end_field == key->start_field && !key->has_end_char) {
        return MERGE_FAST_KEY_NUMERIC;
    }
    if ((key->sort_mode == RANK_SORT_GENERAL_NUMERIC || (key->sort_mode == RANK_SORT_BYTE && options->sort_mode == RANK_SORT_GENERAL_NUMERIC))
        && key->start_field > 0 && !key->has_start_char && key->has_end && key->end_field == key->start_field && !key->has_end_char) {
        return MERGE_FAST_KEY_GENERAL_NUMERIC;
    }
    if ((key->sort_mode == RANK_SORT_HUMAN_NUMERIC || (key->sort_mode == RANK_SORT_BYTE && options->sort_mode == RANK_SORT_HUMAN_NUMERIC))
        && key->start_field > 0 && !key->has_start_char && key->has_end && key->end_field == key->start_field && !key->has_end_char) {
        return MERGE_FAST_KEY_HUMAN_NUMERIC;
    }
    if ((key->sort_mode == RANK_SORT_MONTH || (key->sort_mode == RANK_SORT_BYTE && options->sort_mode == RANK_SORT_MONTH))
        && key->start_field > 0 && !key->has_start_char && key->has_end && key->end_field == key->start_field && !key->has_end_char) {
        return MERGE_FAST_KEY_MONTH;
    }
    if ((key->sort_mode == RANK_SORT_VERSION || (key->sort_mode == RANK_SORT_BYTE && options->sort_mode == RANK_SORT_VERSION))
        && key->start_field > 0 && !key->has_start_char && key->has_end && key->end_field == key->start_field && !key->has_end_char) {
        return MERGE_FAST_KEY_VERSION;
    }
    return MERGE_FAST_NONE;
}

static bool
merge_direct_output(struct rank_lines *lines, const struct rank_options *options, struct merge_run *runs, size_t run_count, enum merge_fast_mode mode)
{
    struct merge_head_cache *caches = NULL;
    size_t *heap;
    size_t heap_len;
    FILE *stream = stdout;
    int fd;
    struct iovec iov[RANK_MERGE_IOV_MAX];
    int iov_count = 0;
    unsigned char delim = options->zero_terminated ? '\0' : '\n';
    struct merge_head_cache last_cache;
    bool have_last = false;
    bool ok = true;

    if (options->output_file != NULL) {
        stream = fopen(options->output_file, "wb");
        if (stream == NULL) {
            rank_diagf(options, "cannot write: %s: %s", options->output_file, strerror(errno));
            return false;
        }
    }
    fd = fileno(stream);
    heap = rank_xmalloc(run_count * sizeof(heap[0]));
    if (!merge_head_cache_init(&caches, lines, options, runs, run_count, mode)) {
        free(heap);
        if (stream != stdout) {
            (void)fclose(stream);
        }
        return false;
    }
    heap_len = merge_fast_heap_build(heap, runs, run_count);
    if (heap_len > 1) {
        size_t i = heap_len / 2U;

        while (i > 0) {
            i--;
            merge_fast_heap_sift_down(heap, heap_len, i, lines, options, runs, caches, mode);
        }
    }
    while (heap_len > 0) {
        size_t choice = heap[0];
        const struct rank_line *line = &lines->items[runs[choice].pos];
        bool skip = false;

        if (options->unique && have_last) {
            skip = merge_head_cache_compare(&last_cache, &caches[choice], mode) == 0;
        }
        if (!skip && !merge_fast_append_line(fd, iov, &iov_count, lines, line, delim, &delim)) {
            rank_diagf(options, "write failed: %s", strerror(errno));
            ok = false;
            break;
        }
        if (!skip && options->unique) {
            last_cache = caches[choice];
            have_last = true;
        }
        runs[choice].pos++;
        if (runs[choice].pos == runs[choice].end) {
            heap[0] = heap[--heap_len];
        } else {
            merge_head_cache_update(caches, lines, options, runs, choice, mode);
        }
        if (heap_len > 1) {
            merge_fast_heap_sift_down(heap, heap_len, 0, lines, options, runs, caches, mode);
        }
    }
    if (ok && !merge_fast_flush_iov(fd, iov, &iov_count)) {
        rank_diagf(options, "write failed: %s", strerror(errno));
        ok = false;
    }
    if (stream != stdout && fclose(stream) != 0) {
        rank_diagf(options, "error closing %s: %s", options->output_file, strerror(errno));
        ok = false;
    } else if (stream == stdout && fflush(stdout) != 0) {
        rank_diagf(options, "write failed: %s", strerror(errno));
        ok = false;
    }
    free(caches);
    free(heap);
    return ok;
}

static bool
merge_head_cache_init(struct merge_head_cache **caches_out, const struct rank_lines *lines, const struct rank_options *options, const struct merge_run *runs, size_t run_count, enum merge_fast_mode mode)
{
    struct merge_head_cache *caches = rank_xmalloc(run_count * sizeof(caches[0]));
    size_t i;

    for (i = 0; i < run_count; i++) {
        if (runs[i].pos < runs[i].end) {
            merge_head_cache_update(caches, lines, options, runs, i, mode);
        }
    }
    *caches_out = caches;
    return true;
}

static void
merge_head_cache_update(struct merge_head_cache *caches, const struct rank_lines *lines, const struct rank_options *options, const struct merge_run *runs, size_t run_id, enum merge_fast_mode mode)
{
    const struct rank_line *line = &lines->items[runs[run_id].pos];
    struct rank_key_span span;

    if (mode == MERGE_FAST_KEY_NUMERIC || mode == MERGE_FAST_KEY_GENERAL_NUMERIC || mode == MERGE_FAST_KEY_HUMAN_NUMERIC || mode == MERGE_FAST_KEY_MONTH || mode == MERGE_FAST_KEY_VERSION) {
        span = merge_extract_simple_key(line, options);
        caches[run_id].key_ptr = span.ptr;
        caches[run_id].key_len = span.len;
    } else {
        caches[run_id].key_ptr = line->text;
        caches[run_id].key_len = line->len;
    }
    switch (mode) {
    case MERGE_FAST_NUMERIC:
    case MERGE_FAST_KEY_NUMERIC:
        caches[run_id].number = rank_numeric_parse(caches[run_id].key_ptr, caches[run_id].key_len);
        break;
    case MERGE_FAST_GENERAL_NUMERIC:
    case MERGE_FAST_KEY_GENERAL_NUMERIC:
        caches[run_id].general_number = rank_general_numeric_parse(caches[run_id].key_ptr, caches[run_id].key_len);
        break;
    case MERGE_FAST_HUMAN_NUMERIC:
    case MERGE_FAST_KEY_HUMAN_NUMERIC:
        caches[run_id].human_number = rank_human_numeric_parse(caches[run_id].key_ptr, caches[run_id].key_len);
        break;
    case MERGE_FAST_MONTH:
    case MERGE_FAST_KEY_MONTH:
        caches[run_id].month = rank_month_parse(caches[run_id].key_ptr, caches[run_id].key_len);
        break;
    default:
        break;
    }
}

static struct rank_key_span
merge_extract_simple_key(const struct rank_line *line, const struct rank_options *options)
{
    const struct rank_keydef *key = &options->keys[0];
    bool ignore_blanks = options->ignore_leading_blanks || key->ignore_start_blanks;
    size_t start = merge_blank_field_start(line, key->start_field, ignore_blanks);
    size_t end = merge_blank_field_end(line, start);
    struct rank_key_span span;

    span.ptr = line->text + start;
    span.len = end >= start ? end - start : 0;
    return span;
}

static size_t
merge_blank_field_start(const struct rank_line *line, size_t field, bool ignore_blanks)
{
    size_t pos = 0;
    size_t current = 1;

    while (current < field && pos < line->len) {
        while (pos < line->len && merge_sort_blank(line->text[pos])) {
            pos++;
        }
        while (pos < line->len && !merge_sort_blank(line->text[pos])) {
            pos++;
        }
        current++;
    }
    while (pos < line->len && merge_sort_blank(line->text[pos])) {
        pos++;
    }
    if (ignore_blanks) {
        while (pos < line->len && merge_sort_blank(line->text[pos])) {
            pos++;
        }
    }
    return pos;
}

static size_t
merge_blank_field_end(const struct rank_line *line, size_t field_start)
{
    size_t pos = field_start;

    while (pos < line->len && !merge_sort_blank(line->text[pos])) {
        pos++;
    }
    return pos;
}

static bool
merge_sort_blank(unsigned char byte)
{
    return byte == (unsigned char)' ' || byte == (unsigned char)'\t';
}

static size_t
merge_fast_heap_build(size_t *heap, const struct merge_run *runs, size_t run_count)
{
    size_t i;
    size_t heap_len = 0;

    for (i = 0; i < run_count; i++) {
        if (runs[i].pos < runs[i].end) {
            heap[heap_len++] = i;
        }
    }
    return heap_len;
}

static void
merge_fast_heap_sift_down(size_t *heap, size_t heap_len, size_t root, const struct rank_lines *lines, const struct rank_options *options, const struct merge_run *runs, const struct merge_head_cache *caches, enum merge_fast_mode mode)
{
    for (;;) {
        size_t left = root * 2U + 1U;
        size_t right = left + 1U;
        size_t best = root;

        if (left < heap_len && merge_fast_run_less(lines, options, runs, caches, mode, heap[left], heap[best])) {
            best = left;
        }
        if (right < heap_len && merge_fast_run_less(lines, options, runs, caches, mode, heap[right], heap[best])) {
            best = right;
        }
        if (best == root) {
            return;
        }
        {
            size_t tmp = heap[root];

            heap[root] = heap[best];
            heap[best] = tmp;
        }
        root = best;
    }
}

static bool
merge_fast_run_less(const struct rank_lines *lines, const struct rank_options *options, const struct merge_run *runs, const struct merge_head_cache *caches, enum merge_fast_mode mode, size_t a, size_t b)
{
    int result;

    switch (mode) {
    case MERGE_FAST_KEY_NUMERIC:
    case MERGE_FAST_NUMERIC:
        result = rank_numeric_compare_values(&caches[a].number, &caches[b].number);
        break;
    case MERGE_FAST_KEY_VERSION:
    case MERGE_FAST_VERSION:
        result = rank_version_compare(caches[a].key_ptr, caches[a].key_len, caches[b].key_ptr, caches[b].key_len);
        break;
    default:
        result = merge_head_cache_compare(&caches[a], &caches[b], mode);
        break;
    }
    if (mode == MERGE_FAST_KEY_NUMERIC || mode == MERGE_FAST_KEY_GENERAL_NUMERIC || mode == MERGE_FAST_KEY_HUMAN_NUMERIC || mode == MERGE_FAST_KEY_MONTH || mode == MERGE_FAST_KEY_VERSION) {
        if (options->keys[0].reverse) {
            result = -result;
        }
    }
    if (result == 0 && !(options->stable || options->unique)) {
        const struct rank_line *aline = &lines->items[runs[a].pos];
        const struct rank_line *bline = &lines->items[runs[b].pos];

        result = merge_compare_bytes(aline->text, aline->len, bline->text, bline->len);
    }
    if (options->reverse) {
        result = -result;
    }
    if (result != 0) {
        return result < 0;
    }
    return a < b;
}

static int
merge_head_cache_compare(const struct merge_head_cache *a, const struct merge_head_cache *b, enum merge_fast_mode mode)
{
    switch (mode) {
    case MERGE_FAST_NUMERIC:
    case MERGE_FAST_KEY_NUMERIC:
        return rank_numeric_compare_values(&a->number, &b->number);
    case MERGE_FAST_GENERAL_NUMERIC:
    case MERGE_FAST_KEY_GENERAL_NUMERIC:
        return rank_general_numeric_compare_values(&a->general_number, &b->general_number);
    case MERGE_FAST_HUMAN_NUMERIC:
    case MERGE_FAST_KEY_HUMAN_NUMERIC:
        return rank_human_numeric_compare_values(&a->human_number, &b->human_number);
    case MERGE_FAST_MONTH:
    case MERGE_FAST_KEY_MONTH:
        return rank_month_compare_values(&a->month, &b->month);
    default:
        return rank_version_compare(a->key_ptr, a->key_len, b->key_ptr, b->key_len);
    }
}

static int
merge_compare_bytes(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len)
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

static bool
merge_fast_append_line(int fd, struct iovec *iov, int *iov_count, const struct rank_lines *lines, const struct rank_line *line, unsigned char delim, const unsigned char *delim_ptr)
{
    if (*iov_count >= RANK_MERGE_IOV_MAX - 2 && !merge_fast_flush_iov(fd, iov, iov_count)) {
        return false;
    }
    if (line->off < lines->data_len && line->len < lines->data_len - line->off && line->text[line->len] == delim) {
        iov[*iov_count].iov_base = (void *)line->text;
        iov[*iov_count].iov_len = line->len + 1U;
        (*iov_count)++;
        return true;
    }
    if (line->len > 0) {
        iov[*iov_count].iov_base = (void *)line->text;
        iov[*iov_count].iov_len = line->len;
        (*iov_count)++;
    }
    iov[*iov_count].iov_base = (void *)delim_ptr;
    iov[*iov_count].iov_len = 1;
    (*iov_count)++;
    return true;
}

static bool
merge_fast_flush_iov(int fd, struct iovec *iov, int *iov_count)
{
    int start = 0;

    if (*iov_count == 0) {
        return true;
    }
    while (start < *iov_count) {
        ssize_t nwritten = writev(fd, &iov[start], *iov_count - start);

        if (nwritten < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        while (start < *iov_count && (size_t)nwritten >= iov[start].iov_len) {
            nwritten -= (ssize_t)iov[start].iov_len;
            start++;
        }
        if (start < *iov_count && nwritten > 0) {
            iov[start].iov_base = (char *)iov[start].iov_base + nwritten;
            iov[start].iov_len -= (size_t)nwritten;
        }
    }
    *iov_count = 0;
    return true;
}

static bool
merge_can_stream_bytes(const struct rank_options *options)
{
    return options->key_count == 0 && options->sort_mode == RANK_SORT_BYTE && !options->debug;
}

static bool
merge_stream_bytes(const struct rank_options *options)
{
    struct merge_stream_run *runs = NULL;
    size_t run_count = 0;
    size_t *heap = NULL;
    size_t heap_len;
    FILE *stream = stdout;
    unsigned char delim = options->zero_terminated ? '\0' : '\n';
    unsigned char *last = NULL;
    size_t last_len = 0;
    size_t last_cap = 0;
    bool have_last = false;
    bool ok = true;

    if (options->output_file != NULL) {
        stream = fopen(options->output_file, "wb");
        if (stream == NULL) {
            rank_diagf(options, "cannot write: %s: %s", options->output_file, strerror(errno));
            return false;
        }
    }
    if (!merge_stream_open_runs(&runs, &run_count, options)) {
        ok = false;
        goto done;
    }
    heap = rank_xmalloc(run_count * sizeof(heap[0]));
    heap_len = merge_stream_heap_build(heap, runs, run_count);
    if (heap_len > 1) {
        size_t i = heap_len / 2U;

        while (i > 0) {
            i--;
            merge_stream_heap_sift_down(heap, heap_len, i, runs, options);
        }
    }
    while (heap_len > 0) {
        size_t choice = heap[0];
        bool skip = false;
        bool has_record;

        if (options->unique && have_last && merge_stream_compare_bytes(last, last_len, runs[choice].record, runs[choice].len) == 0) {
            skip = true;
        }
        if (!skip && !merge_stream_write_record(stream, &runs[choice], delim)) {
            rank_diagf(options, "write failed: %s", strerror(errno));
            ok = false;
            break;
        }
        if (!skip && options->unique) {
            if (runs[choice].len > last_cap) {
                last = rank_xrealloc(last, runs[choice].len);
                last_cap = runs[choice].len;
            }
            if (runs[choice].len > 0) {
                memcpy(last, runs[choice].record, runs[choice].len);
            }
            last_len = runs[choice].len;
            have_last = true;
        }
        merge_stream_consume(&runs[choice]);
        if (!merge_stream_read_head(&runs[choice], options, &has_record)) {
            ok = false;
            break;
        }
        if (!has_record) {
            heap[0] = heap[--heap_len];
        }
        if (heap_len > 1) {
            merge_stream_heap_sift_down(heap, heap_len, 0, runs, options);
        }
    }

done:
    free(last);
    free(heap);
    merge_stream_close_runs(runs, run_count);
    if (stream != stdout && fclose(stream) != 0) {
        rank_diagf(options, "error closing %s: %s", options->output_file, strerror(errno));
        ok = false;
    } else if (stream == stdout && fflush(stdout) != 0) {
        rank_diagf(options, "write failed: %s", strerror(errno));
        ok = false;
    }
    return ok;
}

static bool
merge_stream_open_runs(struct merge_stream_run **runs_out, size_t *run_count_out, const struct rank_options *options)
{
    size_t run_count = options->operand_count == 0 ? 1U : options->operand_count;
    struct merge_stream_run *runs = rank_xmalloc(run_count * sizeof(runs[0]));
    size_t i;

    memset(runs, 0, run_count * sizeof(runs[0]));
    for (i = 0; i < run_count; i++) {
        const char *name = options->operand_count == 0 ? "-" : options->operands[i];
        bool has_record;

        runs[i].fd = -1;
        runs[i].name = name;
        if (strcmp(name, "-") == 0) {
            runs[i].fd = STDIN_FILENO;
        } else {
            runs[i].fd = open(name, O_RDONLY);
            runs[i].close_fd = true;
            if (runs[i].fd < 0) {
                rank_diagf(options, "cannot read: %s: %s", name, strerror(errno));
                merge_stream_close_runs(runs, run_count);
                return false;
            }
        }
        if (!merge_stream_read_head(&runs[i], options, &has_record)) {
            merge_stream_close_runs(runs, run_count);
            return false;
        }
    }
    *runs_out = runs;
    *run_count_out = run_count;
    return true;
}

static void
merge_stream_close_runs(struct merge_stream_run *runs, size_t run_count)
{
    size_t i;

    if (runs == NULL) {
        return;
    }
    for (i = 0; i < run_count; i++) {
        if (runs[i].close_fd && runs[i].fd >= 0) {
            (void)close(runs[i].fd);
        }
        free(runs[i].record);
    }
    free(runs);
}

static bool
merge_stream_read_head(struct merge_stream_run *run, const struct rank_options *options, bool *has_record)
{
    unsigned char delim = options->zero_terminated ? '\0' : '\n';

    *has_record = false;
    if (run->eof) {
        return true;
    }
    for (;;) {
        if (run->in_pos < run->in_len) {
            unsigned char *base = run->in + run->in_pos;
            unsigned char *hit = memchr(base, delim, run->in_len - run->in_pos);
            size_t take = hit == NULL ? run->in_len - run->in_pos : (size_t)(hit - base);

            if (!merge_stream_append_record(run, base, take)) {
                rank_diag(options, "input is too large");
                return false;
            }
            run->in_pos += take;
            if (hit != NULL) {
                run->in_pos++;
                *has_record = true;
                return true;
            }
        } else {
            ssize_t nread = read(run->fd, run->in, sizeof(run->in));

            if (nread < 0) {
                rank_diagf(options, "read failed: %s: %s", run->name, strerror(errno));
                return false;
            }
            if (nread == 0) {
                run->eof = true;
                if (run->len > 0) {
                    *has_record = true;
                }
                return true;
            }
            run->in_pos = 0;
            run->in_len = (size_t)nread;
        }
    }
}

static bool
merge_stream_append_record(struct merge_stream_run *run, const unsigned char *data, size_t len)
{
    if (len == 0) {
        return true;
    }
    if (SIZE_MAX - run->len < len) {
        return false;
    }
    if (run->len + len > run->cap) {
        size_t cap = run->cap == 0 ? RANK_MERGE_READ_CHUNK : run->cap;

        while (cap < run->len + len) {
            if (cap > SIZE_MAX / 2U) {
                cap = run->len + len;
                break;
            }
            cap *= 2U;
        }
        run->record = rank_xrealloc(run->record, cap);
        run->cap = cap;
    }
    memcpy(run->record + run->len, data, len);
    run->len += len;
    return true;
}

static void
merge_stream_consume(struct merge_stream_run *run)
{
    run->len = 0;
}

static size_t
merge_stream_heap_build(size_t *heap, const struct merge_stream_run *runs, size_t run_count)
{
    size_t i;
    size_t heap_len = 0;

    for (i = 0; i < run_count; i++) {
        if (runs[i].len > 0 || !runs[i].eof) {
            heap[heap_len++] = i;
        }
    }
    return heap_len;
}

static void
merge_stream_heap_sift_down(size_t *heap, size_t heap_len, size_t root, const struct merge_stream_run *runs, const struct rank_options *options)
{
    for (;;) {
        size_t left = root * 2U + 1U;
        size_t right = left + 1U;
        size_t best = root;

        if (left < heap_len && merge_stream_run_less(runs, options, heap[left], heap[best])) {
            best = left;
        }
        if (right < heap_len && merge_stream_run_less(runs, options, heap[right], heap[best])) {
            best = right;
        }
        if (best == root) {
            return;
        }
        {
            size_t tmp = heap[root];

            heap[root] = heap[best];
            heap[best] = tmp;
        }
        root = best;
    }
}

static bool
merge_stream_run_less(const struct merge_stream_run *runs, const struct rank_options *options, size_t a, size_t b)
{
    int result = merge_stream_compare_bytes(runs[a].record, runs[a].len, runs[b].record, runs[b].len);

    if (options->reverse) {
        result = -result;
    }
    if (result != 0) {
        return result < 0;
    }
    return a < b;
}

static int
merge_stream_compare_bytes(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len)
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

static bool
merge_stream_write_record(FILE *stream, const struct merge_stream_run *run, unsigned char delim)
{
    if (run->len > 0 && fwrite(run->record, 1, run->len, stream) != run->len) {
        return false;
    }
    return fputc(delim, stream) != EOF;
}

static bool
read_merge_inputs(struct rank_lines *lines, struct merge_run **runs_out, size_t *run_count_out, const struct rank_options *options)
{
    size_t run_count = options->operand_count == 0 ? 1U : options->operand_count;
    struct merge_run *runs = rank_xmalloc(run_count * sizeof(runs[0]));
    size_t i;
    bool ok = true;

    if (options->operand_count == 0) {
        char *stdin_operand = "-";

        ok = append_merge_input(lines, &runs[0], options, &stdin_operand);
    } else {
        for (i = 0; i < options->operand_count; i++) {
            if (!append_merge_input(lines, &runs[i], options, &options->operands[i])) {
                ok = false;
            }
        }
    }
    if (!ok) {
        free(runs);
        return false;
    }
    *runs_out = runs;
    *run_count_out = run_count;
    return true;
}

static bool
append_merge_input(struct rank_lines *lines, struct merge_run *run, const struct rank_options *options, char **operand)
{
    struct rank_options one = *options;

    run->begin = lines->len;
    one.operands = operand;
    one.operand_count = 1;
    if (one.output_file == NULL) {
        one.output_file = "";
    }
    if (!rank_lines_read_all(lines, &one)) {
        return false;
    }
    run->end = lines->len;
    run->pos = run->begin;
    return true;
}

static bool
merge_order_lines(struct rank_lines *lines, const struct rank_options *options, struct merge_run *runs, size_t run_count)
{
    struct rank_line *merged;
    struct rank_cmp_context cmp;
    size_t *heap;
    size_t heap_len;
    size_t out = 0;

    if (lines->len == 0) {
        return true;
    }
    merged = rank_xmalloc(lines->len * sizeof(merged[0]));
    heap = rank_xmalloc(run_count * sizeof(heap[0]));
    rank_cmp_context_init(&cmp, options, lines);
    heap_len = merge_heap_build(heap, runs, run_count);
    if (heap_len > 1) {
        size_t i = heap_len / 2U;

        while (i > 0) {
            i--;
            merge_heap_sift_down(heap, heap_len, i, lines, &cmp, runs);
        }
    }
    while (heap_len > 0) {
        size_t choice = heap[0];

        merged[out++] = lines->items[runs[choice].pos++];
        if (runs[choice].pos == runs[choice].end) {
            heap[0] = heap[--heap_len];
        }
        if (heap_len > 1) {
            merge_heap_sift_down(heap, heap_len, 0, lines, &cmp, runs);
        }
    }
    free(heap);
    free(lines->items);
    lines->items = merged;
    lines->cap = lines->len;
    lines->output_reversed = false;
    lines->monotonic_valid = false;
    return true;
}

static size_t
merge_heap_build(size_t *heap, const struct merge_run *runs, size_t run_count)
{
    size_t i;
    size_t heap_len = 0;

    for (i = 0; i < run_count; i++) {
        if (runs[i].pos < runs[i].end) {
            heap[heap_len++] = i;
        }
    }
    return heap_len;
}

static void
merge_heap_sift_down(size_t *heap, size_t heap_len, size_t root, const struct rank_lines *lines, struct rank_cmp_context *cmp, const struct merge_run *runs)
{
    for (;;) {
        size_t left = root * 2U + 1U;
        size_t right = left + 1U;
        size_t best = root;

        if (left < heap_len && merge_run_less(lines, cmp, runs, heap[left], heap[best])) {
            best = left;
        }
        if (right < heap_len && merge_run_less(lines, cmp, runs, heap[right], heap[best])) {
            best = right;
        }
        if (best == root) {
            return;
        }
        {
            size_t tmp = heap[root];

            heap[root] = heap[best];
            heap[best] = tmp;
        }
        root = best;
    }
}

static bool
merge_run_less(const struct rank_lines *lines, struct rank_cmp_context *cmp, const struct merge_run *runs, size_t a, size_t b)
{
    int result = rank_compare_lines(cmp, &lines->items[runs[a].pos], &lines->items[runs[b].pos]);

    if (result != 0) {
        return result < 0;
    }
    return a < b;
}
