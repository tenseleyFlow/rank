#ifndef RANK_OUTPUT_H
#define RANK_OUTPUT_H

#include <stdbool.h>

#include "line.h"
#include "options.h"

bool rank_output_lines(const struct rank_lines *lines, const struct rank_options *options);

#endif
