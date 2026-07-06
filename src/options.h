#ifndef RANK_OPTIONS_H
#define RANK_OPTIONS_H

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>

enum {
    RANK_OPTIONS_OK = 0
};

struct rank_options {
    const char *program_name;
    bool show_help;
    bool show_version;
    bool reverse;
    bool stable;
    bool unique;
    bool zero_terminated;
    const char *output_file;
    char **operands;
    size_t operand_count;
};

void rank_options_init(struct rank_options *options, const char *argv0);
int rank_options_parse(struct rank_options *options, int argc, char **argv);
void rank_options_print_help(FILE *stream);
void rank_options_print_version(FILE *stream);

#endif
