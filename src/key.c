#include "key.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool parse_pos(const char **text, size_t *field, bool *has_char, size_t *character, char *error, size_t error_len);
static bool parse_modifiers(const char **text, struct rank_keydef *key, bool end_pos, char *error, size_t error_len);
static bool parse_size(const char **text, size_t *value);
static void set_error(char *error, size_t error_len, const char *message);

bool
rank_key_parse(const char *text, struct rank_keydef *key, char *error, size_t error_len)
{
    const char *p = text;

    memset(key, 0, sizeof(*key));
    key->start_field = 1;
    key->end_field = 0;

    if (!parse_pos(&p, &key->start_field, &key->has_start_char, &key->start_char, error, error_len)) {
        return false;
    }
    if (!parse_modifiers(&p, key, false, error, error_len)) {
        return false;
    }
    if (*p == ',') {
        p++;
        key->has_end = true;
        if (!parse_pos(&p, &key->end_field, &key->has_end_char, &key->end_char, error, error_len)) {
            return false;
        }
        if (!parse_modifiers(&p, key, true, error, error_len)) {
            return false;
        }
    }
    if (*p != '\0') {
        set_error(error, error_len, "invalid key definition");
        return false;
    }
    return true;
}

void
rank_key_format(const struct rank_keydef *key, char *buf, size_t buf_len)
{
    (void)snprintf(buf, buf_len,
        "start=%zu.%zu end=%s%zu.%zu start_b=%u end_b=%u reverse=%u",
        key->start_field,
        key->has_start_char ? key->start_char : 0,
        key->has_end ? "" : "none:",
        key->has_end ? key->end_field : 0,
        key->has_end_char ? key->end_char : 0,
        key->ignore_start_blanks ? 1U : 0U,
        key->ignore_end_blanks ? 1U : 0U,
        key->reverse ? 1U : 0U);
}

static bool
parse_pos(const char **text, size_t *field, bool *has_char, size_t *character, char *error, size_t error_len)
{
    const char *p = *text;

    if (!parse_size(&p, field) || *field == 0) {
        set_error(error, error_len, "invalid field number in key definition");
        return false;
    }
    *has_char = false;
    *character = 0;
    if (*p == '.') {
        p++;
        if (!parse_size(&p, character)) {
            set_error(error, error_len, "invalid character offset in key definition");
            return false;
        }
        *has_char = true;
    }
    *text = p;
    return true;
}

static bool
parse_modifiers(const char **text, struct rank_keydef *key, bool end_pos, char *error, size_t error_len)
{
    const char *p = *text;

    while (*p != '\0' && *p != ',') {
        switch (*p) {
        case 'b':
            if (end_pos) {
                key->ignore_end_blanks = true;
            } else {
                key->ignore_start_blanks = true;
            }
            break;
        case 'r':
            key->reverse = true;
            break;
        default:
            set_error(error, error_len, "unsupported key modifier");
            return false;
        }
        p++;
    }
    *text = p;
    return true;
}

static bool
parse_size(const char **text, size_t *value)
{
    const unsigned char *p = (const unsigned char *)*text;
    size_t n = 0;
    bool any = false;

    while (isdigit(*p) != 0) {
        size_t digit = (size_t)(*p - (unsigned char)'0');

        if (n > (SIZE_MAX - digit) / 10U) {
            return false;
        }
        n = n * 10U + digit;
        p++;
        any = true;
    }
    if (!any) {
        return false;
    }
    *value = n;
    *text = (const char *)p;
    return true;
}

static void
set_error(char *error, size_t error_len, const char *message)
{
    if (error_len == 0) {
        return;
    }
    (void)snprintf(error, error_len, "%s", message);
}
