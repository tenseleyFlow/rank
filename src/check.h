#ifndef RANK_CHECK_H
#define RANK_CHECK_H

#include "line.h"
#include "options.h"

void rank_check_module_present(void);
bool rank_check_options_valid(const struct rank_options *options);
int rank_check_lines(const struct rank_lines *lines, const struct rank_options *options);
int rank_check_stream(const struct rank_options *options);

#endif
