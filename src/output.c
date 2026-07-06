#include "output.h"

#include "cmp.h"
#include "util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool write_all(FILE *stream, const unsigned char *buf, size_t len);

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
