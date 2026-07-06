#include "line.h"

#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RANK_READ_CHUNK 65536U

static bool read_stream(struct rank_lines *lines, const struct rank_options *options, int fd, const char *name);
static bool append_bytes(struct rank_lines *lines, const unsigned char *buf, size_t len);
static void add_line(struct rank_lines *lines, size_t off, size_t len);
static void finalize_pointers(struct rank_lines *lines);
static struct rank_key_span extract_key_span(const struct rank_line *line, const struct rank_options *options, const struct rank_keydef *key);
static size_t explicit_field_start(const struct rank_line *line, unsigned char sep, size_t field);
static size_t explicit_field_end(const struct rank_line *line, unsigned char sep, size_t field_start);
static size_t blank_field_start(const struct rank_line *line, size_t field, bool ignore_blanks);
static size_t blank_field_end(const struct rank_line *line, size_t field_start);
static bool sort_blank(unsigned char byte);

void
rank_lines_init(struct rank_lines *lines)
{
    lines->data = NULL;
    lines->data_len = 0;
    lines->data_cap = 0;
    lines->items = NULL;
    lines->len = 0;
    lines->cap = 0;
    lines->key_spans = NULL;
    lines->key_span_count = 0;
}

void
rank_lines_free(struct rank_lines *lines)
{
    free(lines->data);
    free(lines->items);
    free(lines->key_spans);
    rank_lines_init(lines);
}

bool
rank_lines_read_all(struct rank_lines *lines, const struct rank_options *options)
{
    size_t i;
    bool ok = true;

    if (options->operand_count == 0) {
        ok = read_stream(lines, options, STDIN_FILENO, "-");
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
    size_t i;
    size_t k;

    if (options->key_count == 0) {
        return true;
    }
    if (lines->len > SIZE_MAX / options->key_count) {
        rank_diag(options, "too many key spans");
        return false;
    }

    lines->key_span_count = lines->len * options->key_count;
    lines->key_spans = rank_xrealloc(lines->key_spans, lines->key_span_count * sizeof(lines->key_spans[0]));
    for (i = 0; i < lines->len; i++) {
        lines->items[i].key_index = i * options->key_count;
        for (k = 0; k < options->key_count; k++) {
            lines->key_spans[lines->items[i].key_index + k] = extract_key_span(&lines->items[i], options, &options->keys[k]);
        }
    }
    return true;
}

const struct rank_key_span *
rank_line_key_span(const struct rank_lines *lines, const struct rank_line *line, size_t key_id)
{
    return &lines->key_spans[line->key_index + key_id];
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
        size_t i;

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

        for (i = old_len; i < lines->data_len; i++) {
            if (lines->data[i] == delim) {
                add_line(lines, record_start, i - record_start);
                record_start = i + 1;
            }
        }
    }

    if (record_start < lines->data_len) {
        add_line(lines, record_start, lines->data_len - record_start);
    }

    return true;
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
        lines->data = rank_xrealloc(lines->data, cap);
        lines->data_cap = cap;
    }

    memcpy(lines->data + lines->data_len, buf, len);
    lines->data_len += len;
    return true;
}

static void
add_line(struct rank_lines *lines, size_t off, size_t len)
{
    struct rank_line *line;

    if (lines->len == lines->cap) {
        size_t cap;

        if (lines->cap > SIZE_MAX / (2U * sizeof(lines->items[0]))) {
            fprintf(stderr, "rank: too many input records\n");
            exit(RANK_EXIT_SERIOUS);
        }
        cap = lines->cap == 0 ? 1024U : lines->cap * 2U;
        lines->items = rank_xrealloc(lines->items, cap * sizeof(lines->items[0]));
        lines->cap = cap;
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
    struct rank_key_span span;

    if (options->has_field_separator) {
        start = explicit_field_start(line, options->field_separator, key->start_field);
        limit = key->has_end ? explicit_field_start(line, options->field_separator, key->end_field) : line->len;
        if (key->has_end) {
            limit = explicit_field_end(line, options->field_separator, limit);
        }
    } else {
        start = blank_field_start(line, key->start_field, key->ignore_start_blanks);
        limit = key->has_end ? blank_field_start(line, key->end_field, key->ignore_end_blanks) : line->len;
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
            field_start = blank_field_start(line, key->end_field, key->ignore_end_blanks);
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

static size_t
explicit_field_start(const struct rank_line *line, unsigned char sep, size_t field)
{
    size_t current = 1;
    size_t i;

    if (field == 1) {
        return 0;
    }
    for (i = 0; i < line->len; i++) {
        if (line->text[i] == sep) {
            current++;
            if (current == field) {
                return i + 1U;
            }
        }
    }
    return line->len;
}

static size_t
explicit_field_end(const struct rank_line *line, unsigned char sep, size_t field_start)
{
    size_t i;

    for (i = field_start; i < line->len; i++) {
        if (line->text[i] == sep) {
            return i;
        }
    }
    return line->len;
}

static size_t
blank_field_start(const struct rank_line *line, size_t field, bool ignore_blanks)
{
    size_t i = 0;
    size_t current = 0;

    while (i < line->len) {
        size_t blanks = i;

        while (i < line->len && sort_blank(line->text[i])) {
            i++;
        }
        if (i == line->len) {
            return line->len;
        }
        current++;
        if (current == field) {
            return ignore_blanks ? i : blanks;
        }
        while (i < line->len && !sort_blank(line->text[i])) {
            i++;
        }
    }
    return line->len;
}

static size_t
blank_field_end(const struct rank_line *line, size_t field_start)
{
    size_t i = field_start;

    while (i < line->len && sort_blank(line->text[i])) {
        i++;
    }
    while (i < line->len && !sort_blank(line->text[i])) {
        i++;
    }
    return i;
}

static bool
sort_blank(unsigned char byte)
{
    return byte == (unsigned char)' ' || byte == (unsigned char)'\t';
}
