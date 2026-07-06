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

void
rank_lines_init(struct rank_lines *lines)
{
    lines->data = NULL;
    lines->data_len = 0;
    lines->data_cap = 0;
    lines->items = NULL;
    lines->len = 0;
    lines->cap = 0;
}

void
rank_lines_free(struct rank_lines *lines)
{
    free(lines->data);
    free(lines->items);
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
