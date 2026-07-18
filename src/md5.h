#ifndef RANK_MD5_H
#define RANK_MD5_H

#include <stddef.h>
#include <stdint.h>

struct rank_md5_ctx {
    uint32_t state[4];
    uint64_t count;
    unsigned char buffer[64];
};

struct rank_md5_digest {
    unsigned char bytes[16];
};

void rank_md5_init(struct rank_md5_ctx *ctx);
void rank_md5_update(struct rank_md5_ctx *ctx, const unsigned char *data, size_t len);
void rank_md5_final(struct rank_md5_ctx *ctx, unsigned char digest[16]);

#endif
