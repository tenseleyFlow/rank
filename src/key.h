#ifndef RANK_KEY_H
#define RANK_KEY_H

#include <stddef.h>
#include <stdbool.h>

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
    bool reverse;
};

bool rank_key_parse(const char *text, struct rank_keydef *key, char *error, size_t error_len);
void rank_key_format(const struct rank_keydef *key, char *buf, size_t buf_len);

#endif
