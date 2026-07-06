#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void
rank_diag(const struct rank_options *options, const char *message)
{
    fprintf(stderr, "%s: %s\n", options->program_name, message);
}

void
rank_diagf(const struct rank_options *options, const char *format, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", options->program_name);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void *
rank_xmalloc(size_t size)
{
    void *ptr;

    ptr = malloc(size == 0 ? 1 : size);
    if (ptr == NULL) {
        fprintf(stderr, "rank: memory exhausted\n");
        exit(RANK_EXIT_SERIOUS);
    }
    return ptr;
}

void *
rank_xrealloc(void *ptr, size_t size)
{
    void *new_ptr;

    new_ptr = realloc(ptr, size == 0 ? 1 : size);
    if (new_ptr == NULL) {
        fprintf(stderr, "rank: memory exhausted\n");
        exit(RANK_EXIT_SERIOUS);
    }
    return new_ptr;
}
