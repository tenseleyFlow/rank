#include "output.h"

#include "cmp.h"
#include "util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

enum {
    RANK_OUTPUT_BUFFER_SIZE = 1024U * 1024U,
    RANK_IOV_MAX = 1024
};

struct rank_writer {
    FILE *stream;
    int fd;
    unsigned char *buf;
    size_t len;
    size_t cap;
};

static void writer_init(struct rank_writer *writer, FILE *stream);
static void writer_free(struct rank_writer *writer);
static bool writer_flush(struct rank_writer *writer);
static bool writer_write_fd(struct rank_writer *writer, const unsigned char *buf, size_t len);
static bool writer_write(struct rank_writer *writer, const unsigned char *buf, size_t len);
static bool writer_byte(struct rank_writer *writer, unsigned char byte);
static bool write_plain_lines(FILE *stream, const struct rank_lines *lines, unsigned char delim);
static bool append_plain_line_iov(const struct rank_lines *lines, const struct rank_line *line, unsigned char delim, const unsigned char *delim_ptr, struct iovec *iov, int *count);
static bool append_contiguous_run_iov(const struct rank_lines *lines, size_t *index, unsigned char delim, struct iovec *iov, int *count);
static bool flush_iov(int fd, struct iovec *iov, int count);
static bool write_line(struct rank_writer *writer, const struct rank_lines *lines, const struct rank_line *line, unsigned char delim);
static bool write_debug_annotations(struct rank_writer *writer, const struct rank_lines *lines, const struct rank_options *options, const struct rank_line *line);
static bool write_underline(struct rank_writer *writer, size_t off, size_t len);

bool
rank_output_lines(const struct rank_lines *lines, const struct rank_options *options)
{
    FILE *stream = stdout;
    unsigned char delim = options->zero_terminated ? '\0' : '\n';
    struct rank_cmp_context cmp;
    struct rank_writer writer;
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
    (void)setvbuf(stream, NULL, _IONBF, 0);
    if (!options->unique && !options->debug) {
        ok = write_plain_lines(stream, lines, delim);
        if (!ok) {
            rank_diagf(options, "write failed: %s", strerror(errno));
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

    writer_init(&writer, stream);
    if (options->debug) {
        fprintf(stderr, "%s: text ordering performed using simple byte comparison\n", options->program_name);
    }

    rank_cmp_context_init(&cmp, options, lines);
    for (i = 0; i < lines->len; i++) {
        size_t index = lines->output_reversed ? lines->len - 1U - i : i;
        const struct rank_line *line = &lines->items[index];

        if (options->unique && previous != NULL && rank_compare_unique(&cmp, previous, line) == 0) {
            continue;
        }
        if (!write_line(&writer, lines, line, delim)) {
            rank_diagf(options, "write failed: %s", strerror(errno));
            ok = false;
            break;
        }
        if (options->debug && !write_debug_annotations(&writer, lines, options, line)) {
            rank_diagf(options, "write failed: %s", strerror(errno));
            ok = false;
            break;
        }
        previous = line;
    }

    if (ok && !writer_flush(&writer)) {
        rank_diagf(options, "write failed: %s", strerror(errno));
        ok = false;
    }
    writer_free(&writer);

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
write_plain_lines(FILE *stream, const struct rank_lines *lines, unsigned char delim)
{
    struct iovec iov[RANK_IOV_MAX];
    int fd = fileno(stream);
    int count = 0;
    size_t i;
    bool allow_contiguous = !lines->output_reversed && lines->monotonic_valid && lines->monotonic_direction <= 0;

    for (i = 0; i < lines->len; i++) {
        size_t index = lines->output_reversed ? lines->len - 1U - i : i;
        const struct rank_line *line = &lines->items[index];

        if (count >= RANK_IOV_MAX - 2) {
            if (!flush_iov(fd, iov, count)) {
                return false;
            }
            count = 0;
        }
        if (allow_contiguous && append_contiguous_run_iov(lines, &i, delim, iov, &count)) {
            continue;
        }
        append_plain_line_iov(lines, line, delim, &delim, iov, &count);
    }
    return flush_iov(fd, iov, count);
}

static bool
append_plain_line_iov(const struct rank_lines *lines, const struct rank_line *line, unsigned char delim, const unsigned char *delim_ptr, struct iovec *iov, int *count)
{
    if (line->off < lines->data_len && line->len < lines->data_len - line->off && line->text[line->len] == delim) {
        iov[*count].iov_base = (void *)line->text;
        iov[*count].iov_len = line->len + 1U;
        (*count)++;
        return true;
    }
    if (line->len > 0) {
        iov[*count].iov_base = (void *)line->text;
        iov[*count].iov_len = line->len;
        (*count)++;
    }
    iov[*count].iov_base = (void *)delim_ptr;
    iov[*count].iov_len = 1;
    (*count)++;
    return true;
}

static bool
append_contiguous_run_iov(const struct rank_lines *lines, size_t *index, unsigned char delim, struct iovec *iov, int *count)
{
    const struct rank_line *line = &lines->items[*index];
    size_t start = line->off;
    size_t end;
    size_t i = *index;

    if (!(line->off < lines->data_len && line->len < lines->data_len - line->off && line->text[line->len] == delim)) {
        return false;
    }
    end = line->off + line->len + 1U;
    while (i + 1U < lines->len) {
        const struct rank_line *next = &lines->items[i + 1U];

        if (next->off != end
            || !(next->off < lines->data_len && next->len < lines->data_len - next->off && next->text[next->len] == delim)) {
            break;
        }
        end = next->off + next->len + 1U;
        i++;
    }
    if (i == *index) {
        return false;
    }
    iov[*count].iov_base = lines->data + start;
    iov[*count].iov_len = end - start;
    (*count)++;
    *index = i;
    return true;
}

static bool
flush_iov(int fd, struct iovec *iov, int count)
{
    int start = 0;

    while (start < count) {
        ssize_t nwrite = writev(fd, &iov[start], count - start);

        if (nwrite < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (nwrite == 0) {
            return false;
        }
        while (start < count && (size_t)nwrite >= iov[start].iov_len) {
            nwrite -= (ssize_t)iov[start].iov_len;
            start++;
        }
        if (start < count && nwrite > 0) {
            iov[start].iov_base = (unsigned char *)iov[start].iov_base + nwrite;
            iov[start].iov_len -= (size_t)nwrite;
        }
    }
    return true;
}

static void
writer_init(struct rank_writer *writer, FILE *stream)
{
    writer->stream = stream;
    writer->fd = fileno(stream);
    writer->buf = rank_xmalloc(RANK_OUTPUT_BUFFER_SIZE);
    writer->len = 0;
    writer->cap = RANK_OUTPUT_BUFFER_SIZE;
}

static void
writer_free(struct rank_writer *writer)
{
    free(writer->buf);
    writer->buf = NULL;
    writer->len = 0;
    writer->cap = 0;
}

static bool
writer_flush(struct rank_writer *writer)
{
    if (writer->len == 0) {
        return true;
    }
    if (!writer_write_fd(writer, writer->buf, writer->len)) {
        return false;
    }
    writer->len = 0;
    return true;
}

static bool
writer_write_fd(struct rank_writer *writer, const unsigned char *buf, size_t len)
{
    size_t written = 0;

    while (written < len) {
        ssize_t nwrite = write(writer->fd, buf + written, len - written);

        if (nwrite < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (nwrite == 0) {
            return false;
        }
        written += (size_t)nwrite;
    }
    return true;
}

static bool
writer_write(struct rank_writer *writer, const unsigned char *buf, size_t len)
{
    if (len == 0) {
        return true;
    }
    if (len > writer->cap) {
        return writer_flush(writer) && writer_write_fd(writer, buf, len);
    }
    if (writer->cap - writer->len < len && !writer_flush(writer)) {
        return false;
    }
    memcpy(writer->buf + writer->len, buf, len);
    writer->len += len;
    return true;
}

static bool
writer_byte(struct rank_writer *writer, unsigned char byte)
{
    if (writer->len == writer->cap && !writer_flush(writer)) {
        return false;
    }
    writer->buf[writer->len++] = byte;
    return true;
}

static bool
write_line(struct rank_writer *writer, const struct rank_lines *lines, const struct rank_line *line, unsigned char delim)
{
    if (line->off < lines->data_len && line->len < lines->data_len - line->off && line->text[line->len] == delim) {
        return writer_write(writer, line->text, line->len + 1U);
    }
    return writer_write(writer, line->text, line->len) && writer_byte(writer, delim);
}

static bool
write_debug_annotations(struct rank_writer *writer, const struct rank_lines *lines, const struct rank_options *options, const struct rank_line *line)
{
    size_t k;

    if (options->key_count == 0) {
        return true;
    }
    for (k = 0; k < options->key_count; k++) {
        const struct rank_key_span *span = rank_line_key_span(lines, line, k);
        size_t off = (size_t)(span->ptr - line->text);

        if (span->len == 0) {
            fprintf(stderr, "%s: debug: key %zu is empty for record %zu\n", options->program_name, k + 1U, line->ordinal + 1U);
        }
        if (!write_underline(writer, off, span->len)) {
            return false;
        }
    }
    if (!(options->stable || options->unique)) {
        if (!write_underline(writer, 0, line->len)) {
            return false;
        }
    }
    return true;
}

static bool
write_underline(struct rank_writer *writer, size_t off, size_t len)
{
    size_t i;

    for (i = 0; i < off; i++) {
        if (!writer_byte(writer, ' ')) {
            return false;
        }
    }
    if (len == 0) {
        if (!writer_byte(writer, '^')) {
            return false;
        }
    } else {
        for (i = 0; i < len; i++) {
            if (!writer_byte(writer, '_')) {
                return false;
            }
        }
    }
    return writer_byte(writer, '\n');
}
