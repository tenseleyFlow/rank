#ifndef RANK_LINE_H
#define RANK_LINE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "options.h"
#include "key.h"
#include "md5.h"
#include "numeric.h"

struct rank_line {
    const unsigned char *text;
    size_t len;
    size_t ordinal;
    size_t off;
    size_t key_index;
};

struct rank_transformed_span {
    const unsigned char *ptr;
    size_t len;
    size_t off;
    bool valid;
};

struct rank_lines {
    unsigned char *data;
    size_t data_len;
    size_t data_cap;
    bool data_mapped;
    struct rank_line *items;
    size_t len;
    size_t cap;
    bool output_reversed;
    bool monotonic_valid;
    int monotonic_direction;
    struct rank_key_span *key_spans;
    struct rank_transformed_span *line_transforms;
    struct rank_transformed_span *key_transforms;
    unsigned char *transform_data;
    size_t transform_data_len;
    size_t transform_data_cap;
    struct rank_numeric_value *line_numbers;
    struct rank_numeric_value *key_numbers;
    struct rank_general_numeric_value *line_general_numbers;
    struct rank_general_numeric_value *key_general_numbers;
    struct rank_human_numeric_value *line_human_numbers;
    struct rank_human_numeric_value *key_human_numbers;
    struct rank_month_value *line_months;
    struct rank_month_value *key_months;
    struct rank_md5_digest *line_random;
    struct rank_md5_digest *key_random;
    size_t key_span_count;
};

void rank_lines_init(struct rank_lines *lines);
void rank_lines_free(struct rank_lines *lines);
bool rank_lines_read_all(struct rank_lines *lines, const struct rank_options *options);
bool rank_lines_prepare_keys(struct rank_lines *lines, const struct rank_options *options);
const struct rank_key_span *rank_line_key_span(const struct rank_lines *lines, const struct rank_line *line, size_t key_id);
const struct rank_transformed_span *rank_line_transform(const struct rank_lines *lines, const struct rank_line *line);
const struct rank_transformed_span *rank_line_key_transform(const struct rank_lines *lines, const struct rank_line *line, size_t key_id);
const struct rank_numeric_value *rank_line_number(const struct rank_lines *lines, const struct rank_line *line);
const struct rank_numeric_value *rank_line_key_number(const struct rank_lines *lines, const struct rank_line *line, size_t key_id);
const struct rank_general_numeric_value *rank_line_general_number(const struct rank_lines *lines, const struct rank_line *line);
const struct rank_general_numeric_value *rank_line_key_general_number(const struct rank_lines *lines, const struct rank_line *line, size_t key_id);
const struct rank_human_numeric_value *rank_line_human_number(const struct rank_lines *lines, const struct rank_line *line);
const struct rank_human_numeric_value *rank_line_key_human_number(const struct rank_lines *lines, const struct rank_line *line, size_t key_id);
const struct rank_month_value *rank_line_month(const struct rank_lines *lines, const struct rank_line *line);
const struct rank_month_value *rank_line_key_month(const struct rank_lines *lines, const struct rank_line *line, size_t key_id);
const struct rank_md5_digest *rank_line_random(const struct rank_lines *lines, const struct rank_line *line);
const struct rank_md5_digest *rank_line_key_random(const struct rank_lines *lines, const struct rank_line *line, size_t key_id);

#endif
