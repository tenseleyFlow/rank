#ifndef RANK_KEY_H
#define RANK_KEY_H

#include <stddef.h>

struct rank_key_span {
    const unsigned char *ptr;
    size_t len;
};

void rank_key_module_present(void);

#endif
