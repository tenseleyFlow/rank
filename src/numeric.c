#include "numeric.h"

#include "util.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int compare_magnitude(const struct rank_numeric_value *a, const struct rank_numeric_value *b);
static int human_suffix_power(unsigned char byte);
static unsigned char ascii_lower(unsigned char byte);
static bool ascii_alpha(unsigned char byte);
static bool ascii_alnum(unsigned char byte);
static bool digit(unsigned char byte);
static size_t version_file_prefix_len(const unsigned char *text, size_t len);
static int version_order(const unsigned char *text, size_t pos, size_t len);
static int version_reverse_compare(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len);

void
rank_numeric_module_present(void)
{
}

int
rank_numeric_compare(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len)
{
    struct rank_numeric_value an = rank_numeric_parse(a, a_len);
    struct rank_numeric_value bn = rank_numeric_parse(b, b_len);

    return rank_numeric_compare_values(&an, &bn);
}

int
rank_numeric_compare_values(const struct rank_numeric_value *a, const struct rank_numeric_value *b)
{
    int result;

    if (!a->nonzero && !b->nonzero) {
        return 0;
    }
    if (a->negative != b->negative) {
        return a->negative ? -1 : 1;
    }
    result = compare_magnitude(a, b);
    return a->negative ? -result : result;
}

struct rank_numeric_value
rank_numeric_parse(const unsigned char *text, size_t len)
{
    struct rank_numeric_value span;
    size_t pos = 0;

    span.negative = false;
    span.int_digits = text;
    span.int_len = 0;
    span.frac_digits = text;
    span.frac_len = 0;
    span.nonzero = false;

    while (pos < len && (text[pos] == (unsigned char)' ' || text[pos] == (unsigned char)'\t')) {
        pos++;
    }
    if (pos < len && (text[pos] == (unsigned char)'-' || text[pos] == (unsigned char)'+')) {
        span.negative = text[pos] == (unsigned char)'-';
        pos++;
    }

    while (pos < len && text[pos] == (unsigned char)'0') {
        pos++;
    }
    span.int_digits = text + pos;
    while (pos < len && digit(text[pos])) {
        span.nonzero = true;
        pos++;
    }
    span.int_len = (size_t)(text + pos - span.int_digits);

    if (pos < len && text[pos] == (unsigned char)'.') {
        pos++;
        span.frac_digits = text + pos;
        while (pos < len && digit(text[pos])) {
            if (text[pos] != (unsigned char)'0') {
                span.nonzero = true;
            }
            pos++;
        }
        span.frac_len = (size_t)(text + pos - span.frac_digits);
    } else {
        span.frac_digits = text + pos;
    }
    return span;
}

static int
compare_magnitude(const struct rank_numeric_value *a, const struct rank_numeric_value *b)
{
    size_t i;
    size_t frac_len;

    if (a->int_len < b->int_len) {
        return -1;
    }
    if (a->int_len > b->int_len) {
        return 1;
    }
    for (i = 0; i < a->int_len; i++) {
        if (a->int_digits[i] < b->int_digits[i]) {
            return -1;
        }
        if (a->int_digits[i] > b->int_digits[i]) {
            return 1;
        }
    }

    frac_len = a->frac_len > b->frac_len ? a->frac_len : b->frac_len;
    for (i = 0; i < frac_len; i++) {
        unsigned char ad = i < a->frac_len ? a->frac_digits[i] : (unsigned char)'0';
        unsigned char bd = i < b->frac_len ? b->frac_digits[i] : (unsigned char)'0';

        if (ad < bd) {
            return -1;
        }
        if (ad > bd) {
            return 1;
        }
    }
    return 0;
}

static bool
digit(unsigned char byte)
{
    return byte >= (unsigned char)'0' && byte <= (unsigned char)'9';
}

struct rank_general_numeric_value
rank_general_numeric_parse(const unsigned char *text, size_t len)
{
    struct rank_general_numeric_value value;
    char *buf = rank_xmalloc(len + 1U);
    char *endptr;

    memcpy(buf, text, len);
    buf[len] = '\0';
    value.value = strtold(buf, &endptr);
    if (endptr == buf) {
        value.class = 0;
        value.value = 0.0L;
    } else if (isnan(value.value)) {
        value.class = 1;
    } else {
        value.class = 2;
    }
    free(buf);
    return value;
}

int
rank_general_numeric_compare_values(const struct rank_general_numeric_value *a, const struct rank_general_numeric_value *b)
{
    if (a->class < b->class) {
        return -1;
    }
    if (a->class > b->class) {
        return 1;
    }
    if (a->class != 2) {
        return 0;
    }
    if (a->value < b->value) {
        return -1;
    }
    if (a->value > b->value) {
        return 1;
    }
    return 0;
}

struct rank_human_numeric_value
rank_human_numeric_parse(const unsigned char *text, size_t len)
{
    struct rank_human_numeric_value value;
    char *buf = rank_xmalloc(len + 1U);
    char *endptr;
    int power;

    memcpy(buf, text, len);
    buf[len] = '\0';
    value.value = strtold(buf, &endptr);
    if (endptr == buf || isnan(value.value)) {
        value.value = 0.0L;
        free(buf);
        return value;
    }

    power = human_suffix_power((unsigned char)*endptr);
    while (power > 0) {
        value.value *= 1024.0L;
        power--;
    }
    free(buf);
    return value;
}

int
rank_human_numeric_compare_values(const struct rank_human_numeric_value *a, const struct rank_human_numeric_value *b)
{
    if (a->value < b->value) {
        return -1;
    }
    if (a->value > b->value) {
        return 1;
    }
    return 0;
}

static int
human_suffix_power(unsigned char byte)
{
    switch (byte) {
    case 'K':
    case 'k':
        return 1;
    case 'M':
        return 2;
    case 'G':
        return 3;
    case 'T':
        return 4;
    case 'P':
        return 5;
    case 'E':
        return 6;
    case 'Z':
        return 7;
    case 'Y':
        return 8;
    case 'R':
        return 9;
    case 'Q':
        return 10;
    default:
        return 0;
    }
}

struct rank_month_value
rank_month_parse(const unsigned char *text, size_t len)
{
    static const char names[12][3] = {
        {'j', 'a', 'n'}, {'f', 'e', 'b'}, {'m', 'a', 'r'}, {'a', 'p', 'r'},
        {'m', 'a', 'y'}, {'j', 'u', 'n'}, {'j', 'u', 'l'}, {'a', 'u', 'g'},
        {'s', 'e', 'p'}, {'o', 'c', 't'}, {'n', 'o', 'v'}, {'d', 'e', 'c'}
    };
    struct rank_month_value value;
    unsigned char key[3];
    size_t pos = 0;
    size_t i;

    value.month = 0;
    while (pos < len && (text[pos] == (unsigned char)' ' || text[pos] == (unsigned char)'\t')) {
        pos++;
    }
    if (len - pos < 3U) {
        return value;
    }
    key[0] = ascii_lower(text[pos]);
    key[1] = ascii_lower(text[pos + 1U]);
    key[2] = ascii_lower(text[pos + 2U]);
    for (i = 0; i < 12U; i++) {
        if (key[0] == (unsigned char)names[i][0] && key[1] == (unsigned char)names[i][1] && key[2] == (unsigned char)names[i][2]) {
            value.month = (int)i + 1;
            return value;
        }
    }
    return value;
}

int
rank_month_compare_values(const struct rank_month_value *a, const struct rank_month_value *b)
{
    if (a->month < b->month) {
        return -1;
    }
    if (a->month > b->month) {
        return 1;
    }
    return 0;
}

int
rank_version_compare(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len)
{
    size_t a_prefix_len;
    size_t b_prefix_len;
    bool one_pass_only;
    int result;

    if (a_len == 0) {
        return b_len == 0 ? 0 : -1;
    }
    if (b_len == 0) {
        return 1;
    }

    if (a[0] == (unsigned char)'.') {
        bool a_dot;
        bool b_dot;
        bool a_dotdot;
        bool b_dotdot;

        if (b[0] != (unsigned char)'.') {
            return -1;
        }
        a_dot = a_len == 1U;
        b_dot = b_len == 1U;
        if (a_dot) {
            return b_dot ? 0 : -1;
        }
        if (b_dot) {
            return 1;
        }
        a_dotdot = a_len == 2U && a[1] == (unsigned char)'.';
        b_dotdot = b_len == 2U && b[1] == (unsigned char)'.';
        if (a_dotdot) {
            return b_dotdot ? 0 : -1;
        }
        if (b_dotdot) {
            return 1;
        }
    } else if (b[0] == (unsigned char)'.') {
        return 1;
    }

    a_prefix_len = version_file_prefix_len(a, a_len);
    b_prefix_len = version_file_prefix_len(b, b_len);
    one_pass_only = a_prefix_len == a_len && b_prefix_len == b_len;
    result = version_reverse_compare(a, a_prefix_len, b, b_prefix_len);
    if (result != 0 || one_pass_only) {
        return result;
    }
    return version_reverse_compare(a, a_len, b, b_len);
}

static size_t
version_file_prefix_len(const unsigned char *text, size_t len)
{
    size_t prefix_len = 0;
    size_t i = 0;

    for (;;) {
        if (i == len) {
            return prefix_len;
        }
        i++;
        prefix_len = i;
        while (i + 1U < len && text[i] == (unsigned char)'.' && (ascii_alpha(text[i + 1U]) || text[i + 1U] == (unsigned char)'~')) {
            i += 2U;
            while (i < len && (ascii_alnum(text[i]) || text[i] == (unsigned char)'~')) {
                i++;
            }
        }
    }
}

static int
version_order(const unsigned char *text, size_t pos, size_t len)
{
    unsigned char byte;

    if (pos == len) {
        return -1;
    }
    byte = text[pos];
    if (digit(byte)) {
        return 0;
    }
    if (ascii_alpha(byte)) {
        return (int)byte;
    }
    if (byte == (unsigned char)'~') {
        return -2;
    }
    return (int)byte + 256;
}

static int
version_reverse_compare(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len)
{
    size_t ai = 0;
    size_t bi = 0;

    while (ai < a_len || bi < b_len) {
        int first_diff = 0;

        while ((ai < a_len && !digit(a[ai])) || (bi < b_len && !digit(b[bi]))) {
            int ao = version_order(a, ai, a_len);
            int bo = version_order(b, bi, b_len);

            if (ao != bo) {
                return ao - bo;
            }
            ai++;
            bi++;
        }
        while (ai < a_len && a[ai] == (unsigned char)'0') {
            ai++;
        }
        while (bi < b_len && b[bi] == (unsigned char)'0') {
            bi++;
        }
        while (ai < a_len && bi < b_len && digit(a[ai]) && digit(b[bi])) {
            if (first_diff == 0) {
                first_diff = (int)a[ai] - (int)b[bi];
            }
            ai++;
            bi++;
        }
        if (ai < a_len && digit(a[ai])) {
            return 1;
        }
        if (bi < b_len && digit(b[bi])) {
            return -1;
        }
        if (first_diff != 0) {
            return first_diff;
        }
    }
    return 0;
}

static bool
ascii_alpha(unsigned char byte)
{
    return (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z') || (byte >= (unsigned char)'a' && byte <= (unsigned char)'z');
}

static bool
ascii_alnum(unsigned char byte)
{
    return ascii_alpha(byte) || digit(byte);
}

static unsigned char
ascii_lower(unsigned char byte)
{
    if (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z') {
        return (unsigned char)(byte - (unsigned char)'A' + (unsigned char)'a');
    }
    return byte;
}
