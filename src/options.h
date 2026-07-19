#ifndef RANK_OPTIONS_H
#define RANK_OPTIONS_H

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>

#include "key.h"

enum {
    RANK_MAX_FIELD_SEPARATOR_LEN = 1
};

enum {
    RANK_OPTIONS_OK = 0
};

enum rank_check_mode {
    RANK_CHECK_NONE = 0,
    RANK_CHECK_DIAGNOSE_FIRST,
    RANK_CHECK_QUIET
};

struct rank_options {
    const char *program_name;
    bool show_help;
    bool show_version;
    bool reverse;
    bool stable;
    bool unique;
    bool merge;
    bool zero_terminated;
    bool ignore_leading_blanks;
    bool ignore_case;
    bool dictionary_order;
    bool ignore_nonprinting;
    bool debug;
    enum rank_check_mode check_mode;
    enum rank_sort_mode sort_mode;
    const char *random_source;
    bool has_field_separator;
    unsigned char field_separator;
    const char *output_file;
    size_t buffer_size;
    bool has_buffer_size;
    char **temporary_dirs;
    size_t temporary_dir_count;
    size_t temporary_dir_cap;
    size_t batch_size;
    bool has_batch_size;
    size_t parallel;
    const char *compress_program;
    const char *files0_from;
    unsigned char *files0_buf;
    char **files0_names;
    struct rank_keydef *keys;
    size_t key_count;
    size_t key_cap;
    char **operands;
    size_t operand_count;
};

void rank_options_init(struct rank_options *options, const char *argv0);
int rank_options_parse(struct rank_options *options, int argc, char **argv);
int rank_options_load_files0(struct rank_options *options);
void rank_options_print_help(FILE *stream);
void rank_options_print_version(FILE *stream);

#endif
