#ifndef RANK_NUMERIC_H
#define RANK_NUMERIC_H

#include <stdbool.h>
#include <stddef.h>

struct rank_numeric_value {
    bool negative;
    const unsigned char *int_digits;
    size_t int_len;
    const unsigned char *frac_digits;
    size_t frac_len;
    bool nonzero;
};

struct rank_general_numeric_value {
    long double value;
    int class;
};

struct rank_human_numeric_value {
    long double value;
};

struct rank_month_value {
    int month;
};

void rank_numeric_module_present(void);
struct rank_numeric_value rank_numeric_parse(const unsigned char *text, size_t len);
int rank_numeric_compare_values(const struct rank_numeric_value *a, const struct rank_numeric_value *b);
int rank_numeric_compare(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len);
struct rank_general_numeric_value rank_general_numeric_parse(const unsigned char *text, size_t len);
int rank_general_numeric_compare_values(const struct rank_general_numeric_value *a, const struct rank_general_numeric_value *b);
struct rank_human_numeric_value rank_human_numeric_parse(const unsigned char *text, size_t len);
int rank_human_numeric_compare_values(const struct rank_human_numeric_value *a, const struct rank_human_numeric_value *b);
struct rank_month_value rank_month_parse(const unsigned char *text, size_t len);
int rank_month_compare_values(const struct rank_month_value *a, const struct rank_month_value *b);
int rank_version_compare(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len);

#endif
