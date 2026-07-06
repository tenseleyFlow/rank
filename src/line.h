#ifndef RANK_LINE_H
#define RANK_LINE_H

#include <stddef.h>
#include <stdbool.h>

#include "options.h"
#include "key.h"

struct rank_line {
    const unsigned char *text;
    size_t len;
    size_t ordinal;
    size_t off;
    size_t key_index;
};

struct rank_lines {
    unsigned char *data;
    size_t data_len;
    size_t data_cap;
    struct rank_line *items;
    size_t len;
    size_t cap;
    struct rank_key_span *key_spans;
    size_t key_span_count;
};

void rank_lines_init(struct rank_lines *lines);
void rank_lines_free(struct rank_lines *lines);
bool rank_lines_read_all(struct rank_lines *lines, const struct rank_options *options);
bool rank_lines_prepare_keys(struct rank_lines *lines, const struct rank_options *options);
const struct rank_key_span *rank_line_key_span(const struct rank_lines *lines, const struct rank_line *line, size_t key_id);

#endif
