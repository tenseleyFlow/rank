#include "options.h"

#include "config.h"
#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char *rank_basename(const char *path);
static int add_key(struct rank_options *options, const char *text);
static int set_field_separator(struct rank_options *options, const char *text);
static void debug_dump_keys(const struct rank_options *options);
static bool obsolete_key_start(const char *arg);
static bool obsolete_key_end(const char *arg);
static int add_obsolete_key(struct rank_options *options, const char *start, const char *end);
static bool translate_obsolete_pos(const char *arg, bool end_pos, char *buf, size_t buf_len);

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
    options->ignore_leading_blanks = false;
    options->debug = false;
    options->has_field_separator = false;
    options->field_separator = 0;
    options->output_file = NULL;
    options->keys = NULL;
    options->key_count = 0;
    options->key_cap = 0;
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

        if (obsolete_key_start(arg)) {
            const char *end = NULL;

            if (i + 1 < argc && obsolete_key_end(argv[i + 1])) {
                end = argv[++i];
            }
            if (add_obsolete_key(options, arg, end) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
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
        if (strcmp(arg, "--ignore-leading-blanks") == 0) {
            options->ignore_leading_blanks = true;
            continue;
        }
        if (strcmp(arg, "--debug") == 0) {
            options->debug = true;
            continue;
        }
        if (strcmp(arg, "--key") == 0 || strcmp(arg, "-k") == 0) {
            if (i + 1 == argc) {
                rank_diagf(options, "option '%s' requires an argument", arg);
                return RANK_EXIT_SERIOUS;
            }
            if (add_key(options, argv[++i]) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strncmp(arg, "--key=", 6) == 0) {
            if (add_key(options, arg + 6) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strcmp(arg, "--field-separator") == 0 || strcmp(arg, "-t") == 0) {
            if (i + 1 == argc) {
                rank_diagf(options, "option '%s' requires an argument", arg);
                return RANK_EXIT_SERIOUS;
            }
            if (set_field_separator(options, argv[++i]) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strncmp(arg, "--field-separator=", 18) == 0) {
            if (set_field_separator(options, arg + 18) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
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
                case 'b':
                    options->ignore_leading_blanks = true;
                    break;
                case 'k':
                    if (arg[j + 1] != '\0') {
                        if (add_key(options, &arg[j + 1]) != RANK_OPTIONS_OK) {
                            return RANK_EXIT_SERIOUS;
                        }
                    } else if (i + 1 < argc) {
                        if (add_key(options, argv[++i]) != RANK_OPTIONS_OK) {
                            return RANK_EXIT_SERIOUS;
                        }
                    } else {
                        rank_diag(options, "option '-k' requires an argument");
                        return RANK_EXIT_SERIOUS;
                    }
                    j = strlen(arg) - 1;
                    break;
                case 't':
                    if (arg[j + 1] != '\0') {
                        if (set_field_separator(options, &arg[j + 1]) != RANK_OPTIONS_OK) {
                            return RANK_EXIT_SERIOUS;
                        }
                    } else if (i + 1 < argc) {
                        if (set_field_separator(options, argv[++i]) != RANK_OPTIONS_OK) {
                            return RANK_EXIT_SERIOUS;
                        }
                    } else {
                        rank_diag(options, "option '-t' requires an argument");
                        return RANK_EXIT_SERIOUS;
                    }
                    j = strlen(arg) - 1;
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

    debug_dump_keys(options);

    return RANK_OPTIONS_OK;
}

static bool
obsolete_key_start(const char *arg)
{
    return arg[0] == '+' && isdigit((unsigned char)arg[1]) != 0;
}

static bool
obsolete_key_end(const char *arg)
{
    return arg[0] == '-' && isdigit((unsigned char)arg[1]) != 0;
}

static int
add_obsolete_key(struct rank_options *options, const char *start, const char *end)
{
    char start_buf[64];
    char end_buf[64];
    char key_buf[160];

    if (!translate_obsolete_pos(start, false, start_buf, sizeof(start_buf))) {
        rank_diagf(options, "invalid obsolete key '%s'", start);
        return RANK_EXIT_SERIOUS;
    }
    if (end != NULL) {
        if (!translate_obsolete_pos(end, true, end_buf, sizeof(end_buf))) {
            rank_diagf(options, "invalid obsolete key '%s'", end);
            return RANK_EXIT_SERIOUS;
        }
        (void)snprintf(key_buf, sizeof(key_buf), "%s,%s", start_buf, end_buf);
    } else {
        (void)snprintf(key_buf, sizeof(key_buf), "%s", start_buf);
    }
    return add_key(options, key_buf);
}

static bool
translate_obsolete_pos(const char *arg, bool end_pos, char *buf, size_t buf_len)
{
    const char *p = arg + 1;
    unsigned long field = 0;
    unsigned long character = 0;
    bool has_character = false;
    char *endptr;

    field = strtoul(p, &endptr, 10);
    if (endptr == p) {
        return false;
    }
    p = endptr;
    if (*p == '.') {
        const char *char_start = p + 1;

        character = strtoul(char_start, &endptr, 10);
        if (endptr == char_start) {
            return false;
        }
        has_character = true;
        p = endptr;
    }
    if (*p != '\0') {
        return false;
    }

    if (end_pos) {
        if (field == 0) {
            return false;
        }
        if (has_character) {
            (void)snprintf(buf, buf_len, "%lu.%lu", field, character + 1UL);
        } else {
            (void)snprintf(buf, buf_len, "%lu", field);
        }
    } else if (has_character) {
        (void)snprintf(buf, buf_len, "%lu.%lu", field + 1UL, character + 1UL);
    } else {
        (void)snprintf(buf, buf_len, "%lu", field + 1UL);
    }
    return true;
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
    fprintf(stream, "  -k, --key=KEYDEF  sort by a key definition\n");
    fprintf(stream, "  -t, --field-separator=SEP  use SEP as field separator\n");
    fprintf(stream, "  -b, --ignore-leading-blanks  ignore leading blanks in key starts\n");
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

static int
add_key(struct rank_options *options, const char *text)
{
    struct rank_keydef key;
    char error[128];

    if (!rank_key_parse(text, &key, error, sizeof(error))) {
        rank_diagf(options, "invalid key '%s': %s", text, error);
        return RANK_EXIT_SERIOUS;
    }
    if (options->key_count == options->key_cap) {
        size_t cap = options->key_cap == 0 ? 2U : options->key_cap * 2U;

        options->keys = rank_xrealloc(options->keys, cap * sizeof(options->keys[0]));
        options->key_cap = cap;
    }
    options->keys[options->key_count++] = key;
    return RANK_OPTIONS_OK;
}

static int
set_field_separator(struct rank_options *options, const char *text)
{
    if (text[0] == '\0' || text[1] != '\0') {
        rank_diagf(options, "field separator must be a single character: '%s'", text);
        return RANK_EXIT_SERIOUS;
    }
    options->has_field_separator = true;
    options->field_separator = (unsigned char)text[0];
    return RANK_OPTIONS_OK;
}

static void
debug_dump_keys(const struct rank_options *options)
{
    size_t i;

    if (getenv("RANK_DEBUG_KEYS") == NULL) {
        return;
    }
    fprintf(stderr, "rank: keys=%zu field-separator=", options->key_count);
    if (options->has_field_separator) {
        fprintf(stderr, "%u", (unsigned int)options->field_separator);
    } else {
        fprintf(stderr, "default");
    }
    fprintf(stderr, " global-b=%u debug=%u\n",
        options->ignore_leading_blanks ? 1U : 0U,
        options->debug ? 1U : 0U);
    for (i = 0; i < options->key_count; i++) {
        char buf[128];

        rank_key_format(&options->keys[i], buf, sizeof(buf));
        fprintf(stderr, "rank: key[%zu] %s\n", i, buf);
    }
}
