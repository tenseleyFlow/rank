#ifndef RANK_LINE_H
#define RANK_LINE_H

#include <stddef.h>
#include <stdbool.h>

#include "options.h"

struct rank_line {
    const unsigned char *text;
    size_t len;
    size_t ordinal;
    size_t off;
};

struct rank_lines {
    unsigned char *data;
    size_t data_len;
    size_t data_cap;
    struct rank_line *items;
    size_t len;
    size_t cap;
};

void rank_lines_init(struct rank_lines *lines);
void rank_lines_free(struct rank_lines *lines);
bool rank_lines_read_all(struct rank_lines *lines, const struct rank_options *options);

#endif
