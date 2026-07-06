#include "output.h"

#include "cmp.h"
#include "util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool write_all(FILE *stream, const unsigned char *buf, size_t len);
static bool write_debug_annotations(FILE *stream, const struct rank_lines *lines, const struct rank_options *options, const struct rank_line *line);

bool
rank_output_lines(const struct rank_lines *lines, const struct rank_options *options)
{
    FILE *stream = stdout;
    unsigned char delim = options->zero_terminated ? '\0' : '\n';
    struct rank_cmp_context cmp;
    const struct rank_line *previous = NULL;
    size_t i;
    bool ok = true;

    if (options->output_file != NULL) {
        stream = fopen(options->output_file, "wb");
        if (stream == NULL) {
            rank_diagf(options, "cannot write: %s: %s", options->output_file, strerror(errno));
            return false;
        }
    }
    if (options->debug) {
        fprintf(stderr, "%s: debug: key annotations enabled\n", options->program_name);
    }

    rank_cmp_context_init(&cmp, options, lines);
    for (i = 0; i < lines->len; i++) {
        const struct rank_line *line = &lines->items[i];

        if (options->unique && previous != NULL && rank_compare_unique(&cmp, previous, line) == 0) {
            continue;
        }
        if (!write_all(stream, line->text, line->len) || fputc(delim, stream) == EOF) {
            rank_diagf(options, "write failed: %s", strerror(errno));
            ok = false;
            break;
        }
        if (options->debug && !write_debug_annotations(stream, lines, options, line)) {
            rank_diagf(options, "write failed: %s", strerror(errno));
            ok = false;
            break;
        }
        previous = line;
    }

    if (stream != stdout && fclose(stream) != 0) {
        rank_diagf(options, "error closing %s: %s", options->output_file, strerror(errno));
        ok = false;
    } else if (stream == stdout && fflush(stdout) != 0) {
        rank_diagf(options, "write failed: %s", strerror(errno));
        ok = false;
    }

    return ok;
}

static bool
write_all(FILE *stream, const unsigned char *buf, size_t len)
{
    return len == 0 || fwrite(buf, 1, len, stream) == len;
}

static bool
write_debug_annotations(FILE *stream, const struct rank_lines *lines, const struct rank_options *options, const struct rank_line *line)
{
    size_t k;

    if (options->key_count == 0) {
        return true;
    }
    for (k = 0; k < options->key_count; k++) {
        const struct rank_key_span *span = rank_line_key_span(lines, line, k);
        size_t off = (size_t)(span->ptr - line->text);
        size_t i;

        for (i = 0; i < off; i++) {
            if (fputc(' ', stream) == EOF) {
                return false;
            }
        }
        if (span->len == 0) {
            fprintf(stderr, "%s: debug: key %zu is empty for record %zu\n", options->program_name, k + 1U, line->ordinal + 1U);
            if (fputc('^', stream) == EOF) {
                return false;
            }
        } else {
            for (i = 0; i < span->len; i++) {
                if (fputc('_', stream) == EOF) {
                    return false;
                }
            }
        }
        if (fputc('\n', stream) == EOF) {
            return false;
        }
    }
    return true;
}
