#ifndef RANK_KEY_H
#define RANK_KEY_H

#include <stddef.h>
#include <stdbool.h>

enum rank_sort_mode {
    RANK_SORT_BYTE = 0,
    RANK_SORT_NUMERIC,
    RANK_SORT_GENERAL_NUMERIC,
    RANK_SORT_HUMAN_NUMERIC,
    RANK_SORT_MONTH,
    RANK_SORT_VERSION,
    RANK_SORT_RANDOM
};

/* Every ordering mode requested, not just the winner, so GNU's
   "options '-XY' are incompatible" diagnostics can be reproduced. */
enum {
    RANK_MODE_FLAG_NUMERIC = 1U << 0,
    RANK_MODE_FLAG_GENERAL = 1U << 1,
    RANK_MODE_FLAG_HUMAN = 1U << 2,
    RANK_MODE_FLAG_MONTH = 1U << 3,
    RANK_MODE_FLAG_VERSION = 1U << 4,
    RANK_MODE_FLAG_RANDOM = 1U << 5
};

struct rank_key_span {
    const unsigned char *ptr;
    size_t len;
};

struct rank_keydef {
    size_t start_field;
    size_t start_char;
    size_t end_field;
    size_t end_char;
    bool has_start_char;
    bool has_end;
    bool has_end_char;
    bool ignore_start_blanks;
    bool ignore_end_blanks;
    bool ignore_case;
    bool dictionary_order;
    bool ignore_nonprinting;
    bool reverse;
    enum rank_sort_mode sort_mode;
    unsigned int mode_flags;
};

bool rank_key_parse(const char *text, struct rank_keydef *key, char *error, size_t error_len);
void rank_key_format(const struct rank_keydef *key, char *buf, size_t buf_len);

#endif
