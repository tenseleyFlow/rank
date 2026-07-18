#ifndef RANK_LOCALE_H
#define RANK_LOCALE_H

#include "options.h"

#include <stdbool.h>
#include <stddef.h>

void rank_locale_module_present(void);
bool rank_locale_init(const struct rank_options *options);
bool rank_locale_collation_identity(void);
int rank_locale_compare(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len);

#endif
