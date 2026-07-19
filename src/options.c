#include "options.h"

#include "config.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *rank_basename(const char *path);
static int add_key(struct rank_options *options, const char *text);
static int add_temporary_dir(struct rank_options *options, const char *text);
static int set_field_separator(struct rank_options *options, const char *text);
static int set_sort_mode(struct rank_options *options, const char *text);
static int set_check_mode(struct rank_options *options, const char *text);
static int set_buffer_size(struct rank_options *options, const char *text);
static int set_batch_size(struct rank_options *options, const char *text);
static int set_parallel(struct rank_options *options, const char *text);
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
    options->merge = false;
    options->zero_terminated = false;
    options->ignore_leading_blanks = false;
    options->ignore_case = false;
    options->dictionary_order = false;
    options->ignore_nonprinting = false;
    options->debug = false;
    options->check_mode = RANK_CHECK_NONE;
    options->sort_mode = RANK_SORT_BYTE;
    options->mode_flags = 0;
    options->random_source = NULL;
    options->has_field_separator = false;
    options->field_separator = 0;
    options->output_file = NULL;
    options->buffer_size = 0;
    options->has_buffer_size = false;
    options->temporary_dirs = NULL;
    options->temporary_dir_count = 0;
    options->temporary_dir_cap = 0;
    options->batch_size = 0;
    options->has_batch_size = false;
    options->parallel = 1;
    options->compress_program = NULL;
    options->files0_from = NULL;
    options->files0_buf = NULL;
    options->files0_names = NULL;
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
        if (strcmp(arg, "--merge") == 0) {
            options->merge = true;
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
        if (strcmp(arg, "--ignore-case") == 0) {
            options->ignore_case = true;
            continue;
        }
        if (strcmp(arg, "--dictionary-order") == 0) {
            options->dictionary_order = true;
            continue;
        }
        if (strcmp(arg, "--ignore-nonprinting") == 0) {
            options->ignore_nonprinting = true;
            continue;
        }
        if (strcmp(arg, "--debug") == 0) {
            options->debug = true;
            continue;
        }
        if (strcmp(arg, "--check") == 0) {
            options->check_mode = RANK_CHECK_DIAGNOSE_FIRST;
            continue;
        }
        if (strncmp(arg, "--check=", 8) == 0) {
            if (set_check_mode(options, arg + 8) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strcmp(arg, "--numeric-sort") == 0) {
            options->sort_mode = RANK_SORT_NUMERIC;
            options->mode_flags |= RANK_MODE_FLAG_NUMERIC;
            continue;
        }
        if (strcmp(arg, "--general-numeric-sort") == 0) {
            options->sort_mode = RANK_SORT_GENERAL_NUMERIC;
            options->mode_flags |= RANK_MODE_FLAG_GENERAL;
            continue;
        }
        if (strcmp(arg, "--human-numeric-sort") == 0) {
            options->sort_mode = RANK_SORT_HUMAN_NUMERIC;
            options->mode_flags |= RANK_MODE_FLAG_HUMAN;
            continue;
        }
        if (strcmp(arg, "--month-sort") == 0) {
            options->sort_mode = RANK_SORT_MONTH;
            options->mode_flags |= RANK_MODE_FLAG_MONTH;
            continue;
        }
        if (strcmp(arg, "--version-sort") == 0) {
            options->sort_mode = RANK_SORT_VERSION;
            options->mode_flags |= RANK_MODE_FLAG_VERSION;
            continue;
        }
        if (strcmp(arg, "--random-sort") == 0) {
            options->sort_mode = RANK_SORT_RANDOM;
            options->mode_flags |= RANK_MODE_FLAG_RANDOM;
            continue;
        }
        if (strcmp(arg, "--random-source") == 0) {
            if (i + 1 == argc) {
                rank_diag(options, "option '--random-source' requires an argument");
                return RANK_EXIT_SERIOUS;
            }
            options->random_source = argv[++i];
            continue;
        }
        if (strncmp(arg, "--random-source=", 16) == 0) {
            options->random_source = arg + 16;
            continue;
        }
        if (strcmp(arg, "--sort") == 0) {
            if (i + 1 == argc) {
                rank_diag(options, "option '--sort' requires an argument");
                return RANK_EXIT_SERIOUS;
            }
            if (set_sort_mode(options, argv[++i]) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strncmp(arg, "--sort=", 7) == 0) {
            if (set_sort_mode(options, arg + 7) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
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
        if (strcmp(arg, "--buffer-size") == 0 || strcmp(arg, "-S") == 0) {
            if (i + 1 == argc) {
                rank_diagf(options, "option '%s' requires an argument", arg);
                return RANK_EXIT_SERIOUS;
            }
            if (set_buffer_size(options, argv[++i]) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strncmp(arg, "--buffer-size=", 14) == 0) {
            if (set_buffer_size(options, arg + 14) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strcmp(arg, "--temporary-directory") == 0 || strcmp(arg, "-T") == 0) {
            if (i + 1 == argc) {
                rank_diagf(options, "option '%s' requires an argument", arg);
                return RANK_EXIT_SERIOUS;
            }
            if (add_temporary_dir(options, argv[++i]) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strncmp(arg, "--temporary-directory=", 22) == 0) {
            if (add_temporary_dir(options, arg + 22) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strcmp(arg, "--batch-size") == 0) {
            if (i + 1 == argc) {
                rank_diag(options, "option '--batch-size' requires an argument");
                return RANK_EXIT_SERIOUS;
            }
            if (set_batch_size(options, argv[++i]) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strncmp(arg, "--batch-size=", 13) == 0) {
            if (set_batch_size(options, arg + 13) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strcmp(arg, "--files0-from") == 0) {
            if (i + 1 == argc) {
                rank_diag(options, "option '--files0-from' requires an argument");
                return RANK_EXIT_SERIOUS;
            }
            options->files0_from = argv[++i];
            continue;
        }
        if (strncmp(arg, "--files0-from=", 14) == 0) {
            options->files0_from = arg + 14;
            continue;
        }
        if (strcmp(arg, "--parallel") == 0) {
            if (i + 1 == argc) {
                rank_diag(options, "option '--parallel' requires an argument");
                return RANK_EXIT_SERIOUS;
            }
            if (set_parallel(options, argv[++i]) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strncmp(arg, "--parallel=", 11) == 0) {
            if (set_parallel(options, arg + 11) != RANK_OPTIONS_OK) {
                return RANK_EXIT_SERIOUS;
            }
            continue;
        }
        if (strcmp(arg, "--compress-program") == 0) {
            if (i + 1 == argc) {
                rank_diag(options, "option '--compress-program' requires an argument");
                return RANK_EXIT_SERIOUS;
            }
            options->compress_program = argv[++i];
            continue;
        }
        if (strncmp(arg, "--compress-program=", 19) == 0) {
            options->compress_program = arg + 19;
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
                case 'm':
                    options->merge = true;
                    break;
                case 'z':
                    options->zero_terminated = true;
                    break;
                case 'b':
                    options->ignore_leading_blanks = true;
                    break;
                case 'f':
                    options->ignore_case = true;
                    break;
                case 'd':
                    options->dictionary_order = true;
                    break;
                case 'i':
                    options->ignore_nonprinting = true;
                    break;
                case 'c':
                    options->check_mode = RANK_CHECK_DIAGNOSE_FIRST;
                    break;
                case 'C':
                    options->check_mode = RANK_CHECK_QUIET;
                    break;
                case 'n':
                    options->sort_mode = RANK_SORT_NUMERIC;
            options->mode_flags |= RANK_MODE_FLAG_NUMERIC;
                    break;
                case 'g':
                    options->sort_mode = RANK_SORT_GENERAL_NUMERIC;
            options->mode_flags |= RANK_MODE_FLAG_GENERAL;
                    break;
                case 'h':
                    options->sort_mode = RANK_SORT_HUMAN_NUMERIC;
            options->mode_flags |= RANK_MODE_FLAG_HUMAN;
                    break;
                case 'M':
                    options->sort_mode = RANK_SORT_MONTH;
            options->mode_flags |= RANK_MODE_FLAG_MONTH;
                    break;
                case 'V':
                    options->sort_mode = RANK_SORT_VERSION;
            options->mode_flags |= RANK_MODE_FLAG_VERSION;
                    break;
                case 'R':
                    options->sort_mode = RANK_SORT_RANDOM;
            options->mode_flags |= RANK_MODE_FLAG_RANDOM;
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
                case 'S':
                    if (arg[j + 1] != '\0') {
                        if (set_buffer_size(options, &arg[j + 1]) != RANK_OPTIONS_OK) {
                            return RANK_EXIT_SERIOUS;
                        }
                    } else if (i + 1 < argc) {
                        if (set_buffer_size(options, argv[++i]) != RANK_OPTIONS_OK) {
                            return RANK_EXIT_SERIOUS;
                        }
                    } else {
                        rank_diag(options, "option '-S' requires an argument");
                        return RANK_EXIT_SERIOUS;
                    }
                    j = strlen(arg) - 1;
                    break;
                case 'T':
                    if (arg[j + 1] != '\0') {
                        if (add_temporary_dir(options, &arg[j + 1]) != RANK_OPTIONS_OK) {
                            return RANK_EXIT_SERIOUS;
                        }
                    } else if (i + 1 < argc) {
                        if (add_temporary_dir(options, argv[++i]) != RANK_OPTIONS_OK) {
                            return RANK_EXIT_SERIOUS;
                        }
                    } else {
                        rank_diag(options, "option '-T' requires an argument");
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

/* GNU rejects combining ordering modes: more than one of n/g/h/M, or
   any of them alongside V, R, -d, or -i (which share one slot in GNU's
   count). -f never counts but does appear in the diagnostic. Letters
   print in GNU's order; -d wins over -i when both were given. */
static bool
mode_set_compatible(const struct rank_options *options, unsigned int mode_flags, bool dictionary, bool nonprinting, bool fold)
{
    char letters[10];
    size_t n = 0;
    int count = ((mode_flags & RANK_MODE_FLAG_NUMERIC) ? 1 : 0)
        + ((mode_flags & RANK_MODE_FLAG_GENERAL) ? 1 : 0)
        + ((mode_flags & RANK_MODE_FLAG_HUMAN) ? 1 : 0)
        + ((mode_flags & RANK_MODE_FLAG_MONTH) ? 1 : 0)
        + (((mode_flags & (RANK_MODE_FLAG_VERSION | RANK_MODE_FLAG_RANDOM)) != 0 || dictionary || nonprinting) ? 1 : 0);

    if (count <= 1) {
        return true;
    }
    if (dictionary) {
        letters[n++] = 'd';
    } else if (nonprinting) {
        letters[n++] = 'i';
    }
    if (fold) {
        letters[n++] = 'f';
    }
    if (mode_flags & RANK_MODE_FLAG_GENERAL) {
        letters[n++] = 'g';
    }
    if (mode_flags & RANK_MODE_FLAG_HUMAN) {
        letters[n++] = 'h';
    }
    if (mode_flags & RANK_MODE_FLAG_MONTH) {
        letters[n++] = 'M';
    }
    if (mode_flags & RANK_MODE_FLAG_NUMERIC) {
        letters[n++] = 'n';
    }
    if (mode_flags & RANK_MODE_FLAG_RANDOM) {
        letters[n++] = 'R';
    }
    if (mode_flags & RANK_MODE_FLAG_VERSION) {
        letters[n++] = 'V';
    }
    letters[n] = '\0';
    rank_diagf(options, "options '-%s' are incompatible", letters);
    return false;
}

/* GNU inheritance: a key with no ordering options of its own -- no
   mode, no b/d/f/i, no r -- takes every global ordering option,
   including reverse. A key with any option takes nothing. The global
   reverse additionally flips only the keyless comparison and the
   keyed last resort. */
void
rank_options_apply_key_defaults(struct rank_options *options)
{
    size_t i;

    for (i = 0; i < options->key_count; i++) {
        struct rank_keydef *key = &options->keys[i];

        if (key->sort_mode != RANK_SORT_BYTE || key->mode_flags != 0
            || key->ignore_case || key->dictionary_order || key->ignore_nonprinting
            || key->ignore_start_blanks || key->ignore_end_blanks || key->reverse) {
            continue;
        }
        key->sort_mode = options->sort_mode;
        key->mode_flags = options->mode_flags;
        key->ignore_case = options->ignore_case;
        key->dictionary_order = options->dictionary_order;
        key->ignore_nonprinting = options->ignore_nonprinting;
        key->ignore_start_blanks = options->ignore_leading_blanks;
        key->ignore_end_blanks = options->ignore_leading_blanks;
        key->reverse = options->reverse;
    }
}

int
rank_options_check_ordering(const struct rank_options *options)
{
    size_t i;

    if (!mode_set_compatible(options, options->mode_flags, options->dictionary_order, options->ignore_nonprinting, options->ignore_case)) {
        return RANK_EXIT_SERIOUS;
    }
    for (i = 0; i < options->key_count; i++) {
        const struct rank_keydef *key = &options->keys[i];

        if (!mode_set_compatible(options, key->mode_flags, key->dictionary_order, key->ignore_nonprinting, key->ignore_case)) {
            return RANK_EXIT_SERIOUS;
        }
    }
    return RANK_OPTIONS_OK;
}

/* Expand --files0-from into the operand list. GNU semantics pinned
   against coreutils 9.11: a trailing unterminated name still counts,
   '-' entries are rejected with the standard-input message regardless
   of the list's source, and zero-length names report their 1-based
   position. */
int
rank_options_load_files0(struct rank_options *options)
{
    unsigned char *buf = NULL;
    char **names = NULL;
    size_t len = 0;
    size_t cap = 0;
    size_t name_count = 0;
    size_t idx = 0;
    size_t start = 0;
    size_t i;
    int fd = STDIN_FILENO;

    if (options->files0_from == NULL) {
        return RANK_OPTIONS_OK;
    }
    if (options->operand_count > 0) {
        rank_diagf(options, "extra operand '%s'", options->operands[0]);
        fprintf(stderr, "file operands cannot be combined with --files0-from\n");
        fprintf(stderr, "Try '%s --help' for more information.\n", options->program_name);
        return RANK_EXIT_SERIOUS;
    }
    if (strcmp(options->files0_from, "-") != 0) {
        fd = open(options->files0_from, O_RDONLY);
        if (fd < 0) {
            rank_diagf(options, "open failed: %s: %s", options->files0_from, strerror(errno));
            return RANK_EXIT_SERIOUS;
        }
    }
    for (;;) {
        unsigned char chunk[4096];
        ssize_t nread = read(fd, chunk, sizeof(chunk));

        if (nread < 0) {
            rank_diagf(options, "cannot read file names from %s", options->files0_from);
            if (fd != STDIN_FILENO) {
                (void)close(fd);
            }
            free(buf);
            return RANK_EXIT_SERIOUS;
        }
        if (nread == 0) {
            break;
        }
        if (len + (size_t)nread + 1U > cap) {
            cap = cap == 0 ? 4096U : cap * 2U;
            while (cap < len + (size_t)nread + 1U) {
                cap *= 2U;
            }
            buf = rank_xrealloc(buf, cap);
        }
        memcpy(buf + len, chunk, (size_t)nread);
        len += (size_t)nread;
    }
    if (fd != STDIN_FILENO && close(fd) != 0) {
        rank_diagf(options, "cannot read file names from %s", options->files0_from);
        free(buf);
        return RANK_EXIT_SERIOUS;
    }

    for (i = 0; i < len; i++) {
        if (buf[i] == '\0') {
            name_count++;
        }
    }
    if (len > 0 && buf[len - 1U] != '\0') {
        name_count++;
    }
    if (name_count == 0) {
        rank_diagf(options, "no input from '%s'", options->files0_from);
        free(buf);
        return RANK_EXIT_SERIOUS;
    }

    buf[len] = '\0';
    names = rank_xmalloc(name_count * sizeof(names[0]));
    for (i = 0; i <= len && idx < name_count; i++) {
        if (i == len || buf[i] == '\0') {
            char *name = (char *)buf + start;

            if (strcmp(name, "-") == 0) {
                rank_diag(options, "when reading file names from standard input, no file name of '-' allowed");
                free(names);
                free(buf);
                return RANK_EXIT_SERIOUS;
            }
            if (name[0] == '\0') {
                rank_diagf(options, "%s:%lu: invalid zero-length file name", options->files0_from, (unsigned long)(idx + 1U));
                free(names);
                free(buf);
                return RANK_EXIT_SERIOUS;
            }
            names[idx++] = name;
            start = i + 1U;
        }
    }

    options->files0_buf = buf;
    options->files0_names = names;
    options->operands = names;
    options->operand_count = name_count;
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
    fprintf(stream, "  -m, --merge    merge already sorted files; do not sort\n");
    fprintf(stream, "  -z, --zero-terminated  line delimiter is NUL, not newline\n");
    fprintf(stream, "  -c, --check  check whether input is sorted\n");
    fprintf(stream, "  -C, --check=quiet, --check=silent  check without diagnostics\n");
    fprintf(stream, "  -o, --output=FILE  write result to FILE\n");
    fprintf(stream, "  -k, --key=KEYDEF  sort by a key definition\n");
    fprintf(stream, "  -t, --field-separator=SEP  use SEP as field separator\n");
    fprintf(stream, "  -b, --ignore-leading-blanks  ignore leading blanks in key starts\n");
    fprintf(stream, "  -d, --dictionary-order  consider only blanks and alphanumeric characters\n");
    fprintf(stream, "  -f, --ignore-case  fold lower case to upper case characters\n");
    fprintf(stream, "  -i, --ignore-nonprinting  consider only printable characters\n");
    fprintf(stream, "  -n, --numeric-sort  compare according to string numerical value\n");
    fprintf(stream, "  -g, --general-numeric-sort  compare according to general numerical value\n");
    fprintf(stream, "  -h, --human-numeric-sort  compare human readable numbers\n");
    fprintf(stream, "  -M, --month-sort  compare month names\n");
    fprintf(stream, "  -V, --version-sort  compare version strings\n");
    fprintf(stream, "  -R, --random-sort  shuffle, grouping identical keys\n");
    fprintf(stream, "      --random-source=FILE  get random bytes from FILE\n");
    fprintf(stream, "      --sort=WORD  sort according to WORD\n");
    fprintf(stream, "  -S, --buffer-size=SIZE  use SIZE for main memory buffer\n");
    fprintf(stream, "  -T, --temporary-directory=DIR  use DIR for temporaries\n");
    fprintf(stream, "      --batch-size=NMERGE  merge at most NMERGE inputs at once\n");
    fprintf(stream, "      --parallel=N  use up to N concurrent workers for sorting\n");
    fprintf(stream, "      --files0-from=F  read input file names from F, NUL-terminated\n");
    fprintf(stream, "      --compress-program=PROG  compress temporary runs with PROG\n");
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
add_temporary_dir(struct rank_options *options, const char *text)
{
    if (text[0] == '\0') {
        rank_diag(options, "temporary directory name is empty");
        return RANK_EXIT_SERIOUS;
    }
    if (options->temporary_dir_count == options->temporary_dir_cap) {
        size_t cap = options->temporary_dir_cap == 0 ? 2U : options->temporary_dir_cap * 2U;

        options->temporary_dirs = rank_xrealloc(options->temporary_dirs, cap * sizeof(options->temporary_dirs[0]));
        options->temporary_dir_cap = cap;
    }
    options->temporary_dirs[options->temporary_dir_count++] = (char *)text;
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

static int
set_check_mode(struct rank_options *options, const char *text)
{
    if (strcmp(text, "diagnose-first") == 0) {
        options->check_mode = RANK_CHECK_DIAGNOSE_FIRST;
        return RANK_OPTIONS_OK;
    }
    if (strcmp(text, "quiet") == 0 || strcmp(text, "silent") == 0) {
        options->check_mode = RANK_CHECK_QUIET;
        return RANK_OPTIONS_OK;
    }
    rank_diagf(options, "invalid --check argument '%s'", text);
    return RANK_EXIT_SERIOUS;
}

static int
set_buffer_size(struct rank_options *options, const char *text)
{
    char *endptr;
    unsigned long long value;
    unsigned long long scale = 1;

    errno = 0;
    value = strtoull(text, &endptr, 10);
    if (endptr == text || errno == ERANGE) {
        rank_diagf(options, "invalid --buffer-size argument '%s'", text);
        return RANK_EXIT_SERIOUS;
    }
    if (*endptr != '\0') {
        switch (*endptr) {
        case 'K':
        case 'k':
            scale = 1024ULL;
            endptr++;
            break;
        case 'M':
        case 'm':
            scale = 1024ULL * 1024ULL;
            endptr++;
            break;
        case 'G':
        case 'g':
            scale = 1024ULL * 1024ULL * 1024ULL;
            endptr++;
            break;
        default:
            rank_diagf(options, "invalid --buffer-size argument '%s'", text);
            return RANK_EXIT_SERIOUS;
        }
    }
    if (*endptr != '\0') {
        rank_diagf(options, "invalid --buffer-size argument '%s'", text);
        return RANK_EXIT_SERIOUS;
    }
    if (value != 0 && scale > ULLONG_MAX / value) {
        rank_diagf(options, "--buffer-size argument '%s' too large", text);
        return RANK_EXIT_SERIOUS;
    }
    value *= scale;
    if (value > (unsigned long long)SIZE_MAX) {
        rank_diagf(options, "--buffer-size argument '%s' too large", text);
        return RANK_EXIT_SERIOUS;
    }
    options->buffer_size = (size_t)value;
    options->has_buffer_size = true;
    return RANK_OPTIONS_OK;
}

static int
set_parallel(struct rank_options *options, const char *text)
{
    char *endptr;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &endptr, 10);
    if (endptr == text || *endptr != '\0' || errno == ERANGE || value == 0 || value > (unsigned long)SIZE_MAX) {
        rank_diagf(options, "invalid --parallel argument '%s'", text);
        return RANK_EXIT_SERIOUS;
    }
    options->parallel = (size_t)value;
    return RANK_OPTIONS_OK;
}

static int
set_batch_size(struct rank_options *options, const char *text)
{
    char *endptr;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &endptr, 10);
    if (endptr == text || *endptr != '\0' || errno == ERANGE || value == 0 || value > (unsigned long)SIZE_MAX) {
        rank_diagf(options, "invalid --batch-size argument '%s'", text);
        return RANK_EXIT_SERIOUS;
    }
    options->batch_size = (size_t)value;
    options->has_batch_size = true;
    return RANK_OPTIONS_OK;
}

static int
set_sort_mode(struct rank_options *options, const char *text)
{
    if (strcmp(text, "numeric") == 0 || strcmp(text, "n") == 0) {
        options->sort_mode = RANK_SORT_NUMERIC;
            options->mode_flags |= RANK_MODE_FLAG_NUMERIC;
        return RANK_OPTIONS_OK;
    }
    if (strcmp(text, "general-numeric") == 0 || strcmp(text, "g") == 0) {
        options->sort_mode = RANK_SORT_GENERAL_NUMERIC;
            options->mode_flags |= RANK_MODE_FLAG_GENERAL;
        return RANK_OPTIONS_OK;
    }
    if (strcmp(text, "human-numeric") == 0 || strcmp(text, "h") == 0) {
        options->sort_mode = RANK_SORT_HUMAN_NUMERIC;
            options->mode_flags |= RANK_MODE_FLAG_HUMAN;
        return RANK_OPTIONS_OK;
    }
    if (strcmp(text, "month") == 0 || strcmp(text, "M") == 0) {
        options->sort_mode = RANK_SORT_MONTH;
            options->mode_flags |= RANK_MODE_FLAG_MONTH;
        return RANK_OPTIONS_OK;
    }
    if (strcmp(text, "version") == 0 || strcmp(text, "V") == 0) {
        options->sort_mode = RANK_SORT_VERSION;
            options->mode_flags |= RANK_MODE_FLAG_VERSION;
        return RANK_OPTIONS_OK;
    }
    if (strcmp(text, "random") == 0 || strcmp(text, "R") == 0) {
        options->sort_mode = RANK_SORT_RANDOM;
            options->mode_flags |= RANK_MODE_FLAG_RANDOM;
        return RANK_OPTIONS_OK;
    }
    rank_diagf(options, "unsupported sort mode '%s'", text);
    return RANK_EXIT_SERIOUS;
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
    fprintf(stderr, " global-b=%u global-d=%u global-f=%u global-i=%u debug=%u\n",
        options->ignore_leading_blanks ? 1U : 0U,
        options->dictionary_order ? 1U : 0U,
        options->ignore_case ? 1U : 0U,
        options->ignore_nonprinting ? 1U : 0U,
        options->debug ? 1U : 0U);
    for (i = 0; i < options->key_count; i++) {
        char buf[128];

        rank_key_format(&options->keys[i], buf, sizeof(buf));
        fprintf(stderr, "rank: key[%zu] %s\n", i, buf);
    }
}
