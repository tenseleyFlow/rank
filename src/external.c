#include "external.h"

#include "line.h"
#include "merge.h"
#include "output.h"
#include "plan.h"
#include "sort.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define RANK_EXTERNAL_READ_CHUNK 65536U

struct external_runs {
    char **paths;
    size_t len;
    size_t cap;
};

static char **active_temp_paths;
static size_t active_temp_len;
static size_t active_temp_cap;
static volatile sig_atomic_t cleaning_temp_paths;

static bool sort_in_memory(const struct rank_options *options, struct rank_lines *lines);
static bool write_temp_runs_bounded(const struct rank_options *options, struct external_runs *runs);
static bool read_stream_runs(const struct rank_options *options, int fd, const char *name, struct rank_lines *run, size_t *record_start, struct external_runs *runs);
static bool flush_run_if_needed(const struct rank_options *options, struct rank_lines *run, size_t *record_start, struct external_runs *runs);
static bool flush_run(const struct rank_options *options, struct rank_lines *run, size_t *record_start, struct external_runs *runs);
static bool append_run_bytes(struct rank_lines *run, const unsigned char *buf, size_t len);
static void parse_run_records(struct rank_lines *run, unsigned char delim, size_t start, size_t end, size_t *record_start);
static void add_run_line(struct rank_lines *run, size_t off, size_t len);
static void finalize_run_pointers(struct rank_lines *run);
static bool write_one_run(const struct rank_options *options, const struct rank_lines *lines, size_t begin, size_t end, const char *path);
static char *make_temp_path(const struct rank_options *options);
static void register_temp_path(char *path);
static void unregister_temp_path(const char *path);
static void remove_temp_path(char *path);
static void install_temp_signal_handlers(void);
static void temp_signal_handler(int signo);
static bool write_temp_run(const struct rank_options *options, const struct rank_lines *run, const char *path);
static bool filter_temp_file(const struct rank_options *options, const char *input_path, const char *output_path, bool decompress);
static bool prepare_merge_inputs(const struct rank_options *options, char **paths, size_t count, struct external_runs *prepared);
static void cleanup_prepared_inputs(const struct rank_options *options, struct external_runs *prepared);
static const char *temp_dir_at(const struct rank_options *options, size_t index);
static bool runs_push(struct external_runs *runs, char *path);
static bool merge_temp_runs(const struct rank_options *options, const struct external_runs *runs);
static bool reduce_temp_runs(const struct rank_options *options, struct external_runs *runs);
static bool merge_run_group_to_temp(const struct rank_options *options, char **paths, size_t count, char **out_path);
static bool write_empty_output(const struct rank_options *options);
static void cleanup_runs(struct external_runs *runs);
static void free_prepared_subset(struct rank_lines *lines);

void
rank_external_module_present(void)
{
}

bool
rank_external_sort(const struct rank_options *options)
{
    struct rank_lines lines;
    struct external_runs runs = {0};
    bool ok = true;

    if (options->buffer_size != 0 && !options->debug) {
        install_temp_signal_handlers();
        if (!write_temp_runs_bounded(options, &runs)) {
            ok = false;
        } else if (!reduce_temp_runs(options, &runs)) {
            ok = false;
        } else if (runs.len == 0) {
            ok = write_empty_output(options);
        } else if (!merge_temp_runs(options, &runs)) {
            ok = false;
        }
        cleanup_runs(&runs);
        return ok;
    }

    rank_lines_init(&lines);
    if (!rank_lines_read_all(&lines, options)) {
        rank_lines_free(&lines);
        return false;
    }
    if (options->buffer_size == 0 || lines.data_len <= options->buffer_size || options->debug) {
        ok = sort_in_memory(options, &lines);
        rank_lines_free(&lines);
        return ok;
    }

    rank_lines_free(&lines);
    return ok;
}

static bool
sort_in_memory(const struct rank_options *options, struct rank_lines *lines)
{
    struct rank_plan plan;

    if (!rank_lines_prepare_keys(lines, options)) {
        return false;
    }
    plan = rank_plan_from_options(options);
    if (!rank_sort_lines(lines, options, &plan)) {
        return false;
    }
    return rank_output_lines(lines, options);
}

static bool
write_temp_runs_bounded(const struct rank_options *options, struct external_runs *runs)
{
    struct rank_lines run;
    size_t record_start = 0;
    size_t i;
    bool ok = true;

    rank_lines_init(&run);
    if (options->operand_count == 0) {
        ok = read_stream_runs(options, STDIN_FILENO, "-", &run, &record_start, runs);
    } else {
        for (i = 0; i < options->operand_count; i++) {
            const char *name = options->operands[i];
            int fd;

            if (strcmp(name, "-") == 0) {
                if (!read_stream_runs(options, STDIN_FILENO, name, &run, &record_start, runs)) {
                    ok = false;
                    break;
                }
                continue;
            }
            fd = open(name, O_RDONLY);
            if (fd < 0) {
                rank_diagf(options, "cannot read: %s: %s", name, strerror(errno));
                ok = false;
                break;
            }
            if (!read_stream_runs(options, fd, name, &run, &record_start, runs)) {
                ok = false;
            }
            if (close(fd) != 0) {
                rank_diagf(options, "error closing %s: %s", name, strerror(errno));
                ok = false;
            }
            if (!ok) {
                break;
            }
        }
    }
    if (ok && record_start < run.data_len) {
        add_run_line(&run, record_start, run.data_len - record_start);
        record_start = run.data_len;
    }
    if (ok && run.len > 0 && !flush_run(options, &run, &record_start, runs)) {
        ok = false;
    }
    rank_lines_free(&run);
    return ok;
}

static bool
read_stream_runs(const struct rank_options *options, int fd, const char *name, struct rank_lines *run, size_t *record_start, struct external_runs *runs)
{
    unsigned char buf[RANK_EXTERNAL_READ_CHUNK];
    unsigned char delim = options->zero_terminated ? '\0' : '\n';

    for (;;) {
        ssize_t nread = read(fd, buf, sizeof(buf));
        size_t old_len;

        if (nread < 0) {
            rank_diagf(options, "read failed: %s: %s", name, strerror(errno));
            return false;
        }
        if (nread == 0) {
            break;
        }
        old_len = run->data_len;
        if (!append_run_bytes(run, buf, (size_t)nread)) {
            rank_diag(options, "input is too large");
            return false;
        }
        parse_run_records(run, delim, old_len, run->data_len, record_start);
        if (!flush_run_if_needed(options, run, record_start, runs)) {
            return false;
        }
    }
    if (*record_start < run->data_len) {
        add_run_line(run, *record_start, run->data_len - *record_start);
        *record_start = run->data_len;
        if (!flush_run_if_needed(options, run, record_start, runs)) {
            return false;
        }
    }
    return true;
}

static bool
flush_run_if_needed(const struct rank_options *options, struct rank_lines *run, size_t *record_start, struct external_runs *runs)
{
    if (run->len > 0 && *record_start == run->data_len && run->data_len >= options->buffer_size) {
        return flush_run(options, run, record_start, runs);
    }
    return true;
}

static bool
flush_run(const struct rank_options *options, struct rank_lines *run, size_t *record_start, struct external_runs *runs)
{
    char *path;

    finalize_run_pointers(run);
    path = make_temp_path(options);
    if (path == NULL) {
        return false;
    }
    if (!write_temp_run(options, run, path)) {
        remove_temp_path(path);
        free(path);
        return false;
    }
    if (!runs_push(runs, path)) {
        remove_temp_path(path);
        free(path);
        return false;
    }
    rank_lines_free(run);
    rank_lines_init(run);
    *record_start = 0;
    return true;
}

static bool
write_temp_run(const struct rank_options *options, const struct rank_lines *run, const char *path)
{
    char *plain_path;
    bool ok;

    if (options->compress_program == NULL) {
        return write_one_run(options, run, 0, run->len, path);
    }
    plain_path = make_temp_path(options);
    if (plain_path == NULL) {
        return false;
    }
    ok = write_one_run(options, run, 0, run->len, plain_path);
    if (ok) {
        ok = filter_temp_file(options, plain_path, path, false);
    }
    remove_temp_path(plain_path);
    free(plain_path);
    return ok;
}

static bool
append_run_bytes(struct rank_lines *run, const unsigned char *buf, size_t len)
{
    if (SIZE_MAX - run->data_len < len) {
        return false;
    }
    if (run->data_len + len > run->data_cap) {
        size_t cap = run->data_cap == 0 ? RANK_EXTERNAL_READ_CHUNK : run->data_cap;

        while (cap < run->data_len + len) {
            if (cap > SIZE_MAX / 2U) {
                cap = run->data_len + len;
                break;
            }
            cap *= 2U;
        }
        run->data = rank_xrealloc(run->data, cap);
        run->data_cap = cap;
    }
    memcpy(run->data + run->data_len, buf, len);
    run->data_len += len;
    return true;
}

static void
parse_run_records(struct rank_lines *run, unsigned char delim, size_t start, size_t end, size_t *record_start)
{
    size_t i = start;

    while (i < end) {
        unsigned char *hit = memchr(run->data + i, delim, end - i);

        if (hit == NULL) {
            break;
        }
        i = (size_t)(hit - run->data);
        add_run_line(run, *record_start, i - *record_start);
        *record_start = i + 1U;
        i++;
    }
}

static void
add_run_line(struct rank_lines *run, size_t off, size_t len)
{
    struct rank_line *line;

    if (run->len == run->cap) {
        size_t cap = run->cap == 0 ? 1024U : run->cap * 2U;

        run->items = rank_xrealloc(run->items, cap * sizeof(run->items[0]));
        run->cap = cap;
    }
    line = &run->items[run->len];
    line->text = NULL;
    line->len = len;
    line->ordinal = run->len;
    line->off = off;
    line->key_index = 0;
    run->len++;
}

static void
finalize_run_pointers(struct rank_lines *run)
{
    size_t i;

    for (i = 0; i < run->len; i++) {
        run->items[i].text = run->data + run->items[i].off;
    }
}

static bool
write_one_run(const struct rank_options *options, const struct rank_lines *lines, size_t begin, size_t end, const char *path)
{
    struct rank_lines run;
    struct rank_options sort_options = *options;
    struct rank_options output_options = *options;
    struct rank_plan plan;
    size_t i;
    bool ok = true;

    rank_lines_init(&run);
    run.data = lines->data;
    run.data_len = lines->data_len;
    run.items = rank_xmalloc((end - begin) * sizeof(run.items[0]));
    run.len = end - begin;
    run.cap = run.len;
    run.monotonic_valid = false;
    for (i = 0; i < run.len; i++) {
        run.items[i] = lines->items[begin + i];
        run.items[i].ordinal = i;
        run.items[i].key_index = 0;
    }

    sort_options.output_file = NULL;
    if (!rank_lines_prepare_keys(&run, &sort_options)) {
        ok = false;
    } else {
        plan = rank_plan_from_options(&sort_options);
        if (!rank_sort_lines(&run, &sort_options, &plan)) {
            ok = false;
        }
    }
    if (ok) {
        output_options.output_file = path;
        output_options.unique = false;
        output_options.debug = false;
        ok = rank_output_lines(&run, &output_options);
    }
    free_prepared_subset(&run);
    return ok;
}

static char *
make_temp_path(const struct rank_options *options)
{
    static size_t next_dir;
    const char *dir = temp_dir_at(options, next_dir++);
    const char *suffix = "/rank-run-XXXXXX";
    size_t len = strlen(dir) + strlen(suffix) + 1U;
    char *path = rank_xmalloc(len);
    int fd;

    (void)snprintf(path, len, "%s%s", dir, suffix);
    fd = mkstemp(path);
    if (fd < 0) {
        rank_diagf(options, "cannot create temporary file in %s: %s", dir, strerror(errno));
        free(path);
        return NULL;
    }
    if (close(fd) != 0) {
        rank_diagf(options, "cannot close temporary file %s: %s", path, strerror(errno));
        unlink(path);
        free(path);
        return NULL;
    }
    register_temp_path(path);
    return path;
}

static void
register_temp_path(char *path)
{
    if (active_temp_len == active_temp_cap) {
        size_t cap = active_temp_cap == 0 ? 16U : active_temp_cap * 2U;

        active_temp_paths = rank_xrealloc(active_temp_paths, cap * sizeof(active_temp_paths[0]));
        active_temp_cap = cap;
    }
    active_temp_paths[active_temp_len++] = path;
}

static void
unregister_temp_path(const char *path)
{
    size_t i;

    for (i = 0; i < active_temp_len; i++) {
        if (active_temp_paths[i] == path) {
            active_temp_paths[i] = active_temp_paths[active_temp_len - 1U];
            active_temp_len--;
            if (active_temp_len == 0) {
                free(active_temp_paths);
                active_temp_paths = NULL;
                active_temp_cap = 0;
            }
            return;
        }
    }
}

static void
remove_temp_path(char *path)
{
    unregister_temp_path(path);
    unlink(path);
}

static void
install_temp_signal_handlers(void)
{
    static bool installed;

    if (installed) {
        return;
    }
    installed = true;
    signal(SIGHUP, temp_signal_handler);
    signal(SIGINT, temp_signal_handler);
    signal(SIGTERM, temp_signal_handler);
#ifdef SIGQUIT
    signal(SIGQUIT, temp_signal_handler);
#endif
}

static void
temp_signal_handler(int signo)
{
    size_t i;

    if (!cleaning_temp_paths) {
        cleaning_temp_paths = 1;
        for (i = 0; i < active_temp_len; i++) {
            unlink(active_temp_paths[i]);
        }
    }
    signal(signo, SIG_DFL);
    raise(signo);
}

static bool
filter_temp_file(const struct rank_options *options, const char *input_path, const char *output_path, bool decompress)
{
    int in_fd = -1;
    int out_fd = -1;
    pid_t pid;
    int status;

    in_fd = open(input_path, O_RDONLY);
    if (in_fd < 0) {
        rank_diagf(options, "cannot read: %s: %s", input_path, strerror(errno));
        return false;
    }
    out_fd = open(output_path, O_WRONLY | O_TRUNC);
    if (out_fd < 0) {
        rank_diagf(options, "cannot write: %s: %s", output_path, strerror(errno));
        close(in_fd);
        return false;
    }
    pid = fork();
    if (pid < 0) {
        rank_diagf(options, "cannot run %s: %s", options->compress_program, strerror(errno));
        close(in_fd);
        close(out_fd);
        return false;
    }
    if (pid == 0) {
        if (dup2(in_fd, STDIN_FILENO) < 0 || dup2(out_fd, STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(in_fd);
        close(out_fd);
        if (decompress) {
            execlp(options->compress_program, options->compress_program, "-d", (char *)NULL);
        } else {
            execlp(options->compress_program, options->compress_program, (char *)NULL);
        }
        _exit(127);
    }
    close(in_fd);
    close(out_fd);
    for (;;) {
        if (waitpid(pid, &status, 0) >= 0) {
            break;
        }
        if (errno != EINTR) {
            rank_diagf(options, "error waiting for %s: %s", options->compress_program, strerror(errno));
            return false;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        rank_diagf(options, "%s failed", options->compress_program);
        return false;
    }
    return true;
}

static bool
prepare_merge_inputs(const struct rank_options *options, char **paths, size_t count, struct external_runs *prepared)
{
    size_t i;

    if (options->compress_program == NULL) {
        prepared->paths = paths;
        prepared->len = count;
        prepared->cap = count;
        return true;
    }
    for (i = 0; i < count; i++) {
        char *path = make_temp_path(options);

        if (path == NULL) {
            cleanup_prepared_inputs(options, prepared);
            return false;
        }
        if (!filter_temp_file(options, paths[i], path, true)) {
            remove_temp_path(path);
            free(path);
            cleanup_prepared_inputs(options, prepared);
            return false;
        }
        if (!runs_push(prepared, path)) {
            remove_temp_path(path);
            free(path);
            cleanup_prepared_inputs(options, prepared);
            return false;
        }
    }
    return true;
}

static void
cleanup_prepared_inputs(const struct rank_options *options, struct external_runs *prepared)
{
    if (options->compress_program == NULL) {
        prepared->paths = NULL;
        prepared->len = 0;
        prepared->cap = 0;
        return;
    }
    cleanup_runs(prepared);
}

static const char *
temp_dir_at(const struct rank_options *options, size_t index)
{
    const char *env;

    if (options->temporary_dir_count > 0) {
        return options->temporary_dirs[index % options->temporary_dir_count];
    }
    env = getenv("TMPDIR");
    return env != NULL && env[0] != '\0' ? env : "/tmp";
}

static bool
runs_push(struct external_runs *runs, char *path)
{
    if (runs->len == runs->cap) {
        size_t cap = runs->cap == 0 ? 4U : runs->cap * 2U;

        runs->paths = rank_xrealloc(runs->paths, cap * sizeof(runs->paths[0]));
        runs->cap = cap;
    }
    runs->paths[runs->len++] = path;
    return true;
}

static bool
merge_temp_runs(const struct rank_options *options, const struct external_runs *runs)
{
    struct rank_options merge_options = *options;
    struct external_runs prepared = {0};
    bool ok;

    if (!prepare_merge_inputs(options, runs->paths, runs->len, &prepared)) {
        return false;
    }

    merge_options.merge = true;
    merge_options.operands = prepared.paths;
    merge_options.operand_count = prepared.len;
    merge_options.has_buffer_size = false;
    merge_options.buffer_size = 0;
    merge_options.compress_program = NULL;
    ok = rank_merge_all(&merge_options);
    cleanup_prepared_inputs(options, &prepared);
    return ok;
}

static bool
reduce_temp_runs(const struct rank_options *options, struct external_runs *runs)
{
    size_t batch = options->has_batch_size ? options->batch_size : 0;

    if (batch == 0 || runs->len <= batch) {
        return true;
    }
    while (runs->len > batch) {
        struct external_runs next = {0};
        size_t i = 0;

        while (i < runs->len) {
            size_t count = runs->len - i < batch ? runs->len - i : batch;
            char *path = NULL;
            size_t j;

            if (!merge_run_group_to_temp(options, &runs->paths[i], count, &path)) {
                cleanup_runs(&next);
                return false;
            }
            if (!runs_push(&next, path)) {
                remove_temp_path(path);
                free(path);
                cleanup_runs(&next);
                return false;
            }
            for (j = 0; j < count; j++) {
                remove_temp_path(runs->paths[i + j]);
                free(runs->paths[i + j]);
            }
            i += count;
        }
        free(runs->paths);
        *runs = next;
    }
    return true;
}

static bool
merge_run_group_to_temp(const struct rank_options *options, char **paths, size_t count, char **out_path)
{
    struct rank_options merge_options = *options;
    char *path = make_temp_path(options);
    char *plain_path = NULL;
    struct external_runs prepared = {0};
    bool ok;

    if (path == NULL) {
        return false;
    }
    if (!prepare_merge_inputs(options, paths, count, &prepared)) {
        remove_temp_path(path);
        free(path);
        return false;
    }
    if (options->compress_program != NULL) {
        plain_path = make_temp_path(options);
        if (plain_path == NULL) {
            cleanup_prepared_inputs(options, &prepared);
            remove_temp_path(path);
            free(path);
            return false;
        }
    }
    merge_options.merge = true;
    merge_options.operands = prepared.paths;
    merge_options.operand_count = prepared.len;
    merge_options.has_buffer_size = false;
    merge_options.buffer_size = 0;
    merge_options.output_file = plain_path != NULL ? plain_path : path;
    merge_options.compress_program = NULL;
    ok = rank_merge_all(&merge_options);
    if (ok && plain_path != NULL) {
        ok = filter_temp_file(options, plain_path, path, false);
    }
    if (plain_path != NULL) {
        remove_temp_path(plain_path);
        free(plain_path);
    }
    cleanup_prepared_inputs(options, &prepared);
    if (!ok) {
        remove_temp_path(path);
        free(path);
        return false;
    }
    *out_path = path;
    return true;
}

static bool
write_empty_output(const struct rank_options *options)
{
    FILE *stream;

    if (options->output_file == NULL) {
        return fflush(stdout) == 0;
    }
    stream = fopen(options->output_file, "wb");
    if (stream == NULL) {
        rank_diagf(options, "cannot write: %s: %s", options->output_file, strerror(errno));
        return false;
    }
    if (fclose(stream) != 0) {
        rank_diagf(options, "error closing %s: %s", options->output_file, strerror(errno));
        return false;
    }
    return true;
}

static void
cleanup_runs(struct external_runs *runs)
{
    size_t i;

    for (i = 0; i < runs->len; i++) {
        remove_temp_path(runs->paths[i]);
        free(runs->paths[i]);
    }
    free(runs->paths);
}

static void
free_prepared_subset(struct rank_lines *lines)
{
    free(lines->items);
    free(lines->key_spans);
    free(lines->line_transforms);
    free(lines->key_transforms);
    free(lines->transform_data);
    free(lines->line_numbers);
    free(lines->key_numbers);
    free(lines->line_general_numbers);
    free(lines->key_general_numbers);
    free(lines->line_human_numbers);
    free(lines->key_human_numbers);
    free(lines->line_months);
    free(lines->key_months);
    free(lines->line_random);
    free(lines->key_random);
}
