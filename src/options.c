#include "options.h"

#include "config.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

static const char *rank_basename(const char *path);

void
rank_options_init(struct rank_options *options, const char *argv0)
{
    options->program_name = rank_basename(argv0);
    options->show_help = false;
    options->show_version = false;
    options->reverse = false;
    options->stable = false;
    options->unique = false;
    options->zero_terminated = false;
    options->output_file = NULL;
    options->operands = NULL;
    options->operand_count = 0;
}

int
rank_options_parse(struct rank_options *options, int argc, char **argv)
{
    int i;

    options->operands = argc > 1 ? &argv[1] : NULL;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            i++;
            while (i < argc) {
                options->operands[options->operand_count++] = argv[i++];
            }
            break;
        }
        if (strcmp(arg, "--help") == 0) {
            options->show_help = true;
            continue;
        }
        if (strcmp(arg, "--version") == 0) {
            options->show_version = true;
            continue;
        }
        if (strcmp(arg, "--reverse") == 0) {
            options->reverse = true;
            continue;
        }
        if (strcmp(arg, "--stable") == 0) {
            options->stable = true;
            continue;
        }
        if (strcmp(arg, "--unique") == 0) {
            options->unique = true;
            continue;
        }
        if (strcmp(arg, "--zero-terminated") == 0) {
            options->zero_terminated = true;
            continue;
        }
        if (strcmp(arg, "--output") == 0 || strcmp(arg, "-o") == 0) {
            if (i + 1 == argc) {
                rank_diagf(options, "option '%s' requires an argument", arg);
                return RANK_EXIT_SERIOUS;
            }
            options->output_file = argv[++i];
            continue;
        }
        if (strncmp(arg, "--output=", 9) == 0) {
            options->output_file = arg + 9;
            continue;
        }
        if (strncmp(arg, "--", 2) == 0) {
            rank_diagf(options, "option '%s' is not implemented yet", arg);
            return RANK_EXIT_SERIOUS;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            size_t j;

            for (j = 1; arg[j] != '\0'; j++) {
                switch (arg[j]) {
                case 'r':
                    options->reverse = true;
                    break;
                case 's':
                    options->stable = true;
                    break;
                case 'u':
                    options->unique = true;
                    break;
                case 'z':
                    options->zero_terminated = true;
                    break;
                case 'o':
                    if (arg[j + 1] != '\0') {
                        options->output_file = &arg[j + 1];
                    } else if (i + 1 < argc) {
                        options->output_file = argv[++i];
                    } else {
                        rank_diag(options, "option '-o' requires an argument");
                        return RANK_EXIT_SERIOUS;
                    }
                    j = strlen(arg) - 1;
                    break;
                default:
                    rank_diagf(options, "option '-%c' is not implemented yet", arg[j]);
                    return RANK_EXIT_SERIOUS;
                }
            }
            continue;
        }
        options->operands[options->operand_count++] = argv[i];
    }

    return RANK_OPTIONS_OK;
}

void
rank_options_print_help(FILE *stream)
{
    fprintf(stream, "Usage: rank [OPTION]... [FILE]...\n");
    fprintf(stream, "Sort FILE(s), or standard input, to standard output.\n\n");
    fprintf(stream, "      --help     display this help and exit\n");
    fprintf(stream, "      --version  output version information and exit\n");
    fprintf(stream, "  -r, --reverse  reverse the result of comparisons\n");
    fprintf(stream, "  -s, --stable   stabilize sort by disabling last-resort comparison\n");
    fprintf(stream, "  -u, --unique   output only the first of an equal run\n");
    fprintf(stream, "  -z, --zero-terminated  line delimiter is NUL, not newline\n");
    fprintf(stream, "  -o, --output=FILE  write result to FILE\n");
}

void
rank_options_print_version(FILE *stream)
{
    fprintf(stream, "rank %s\n", RANK_VERSION);
}

static const char *
rank_basename(const char *path)
{
    const char *last = path;
    const char *p;

    if (path == NULL || path[0] == '\0') {
        return "rank";
    }

    for (p = path; *p != '\0'; p++) {
        if (*p == '/') {
            last = p + 1;
        }
    }

    return last[0] == '\0' ? "rank" : last;
}
