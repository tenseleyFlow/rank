#include "md5.h"

#include <string.h>

static void md5_transform(uint32_t state[4], const unsigned char block[64]);
static uint32_t rotl(uint32_t x, uint32_t c);

void
rank_md5_init(struct rank_md5_ctx *ctx)
{
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xefcdab89U;
    ctx->state[2] = 0x98badcfeU;
    ctx->state[3] = 0x10325476U;
    ctx->count = 0;
}

void
rank_md5_update(struct rank_md5_ctx *ctx, const unsigned char *data, size_t len)
{
    size_t rem = (size_t)(ctx->count & 63U);

    ctx->count += len;
    if (rem > 0) {
        size_t fill = 64U - rem;

        if (len < fill) {
            memcpy(ctx->buffer + rem, data, len);
            return;
        }
        memcpy(ctx->buffer + rem, data, fill);
        md5_transform(ctx->state, ctx->buffer);
        data += fill;
        len -= fill;
    }
    while (len >= 64U) {
        md5_transform(ctx->state, data);
        data += 64U;
        len -= 64U;
    }
    if (len > 0) {
        memcpy(ctx->buffer, data, len);
    }
}

void
rank_md5_final(struct rank_md5_ctx *ctx, unsigned char digest[16])
{
    static const unsigned char pad[64] = {0x80};
    unsigned char lenbuf[8];
    uint64_t bits = ctx->count << 3;
    size_t rem = (size_t)(ctx->count & 63U);
    size_t padlen = rem < 56U ? 56U - rem : 120U - rem;
    unsigned int i;

    for (i = 0; i < 8U; i++) {
        lenbuf[i] = (unsigned char)(bits >> (8U * i));
    }
    rank_md5_update(ctx, pad, padlen);
    rank_md5_update(ctx, lenbuf, 8U);
    for (i = 0; i < 4U; i++) {
        digest[i * 4U] = (unsigned char)ctx->state[i];
        digest[i * 4U + 1U] = (unsigned char)(ctx->state[i] >> 8);
        digest[i * 4U + 2U] = (unsigned char)(ctx->state[i] >> 16);
        digest[i * 4U + 3U] = (unsigned char)(ctx->state[i] >> 24);
    }
}

static uint32_t
rotl(uint32_t x, uint32_t c)
{
    return (x << c) | (x >> (32U - c));
}

static void
md5_transform(uint32_t state[4], const unsigned char block[64])
{
    static const uint32_t k[64] = {
        0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
        0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
        0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
        0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
        0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
        0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
        0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
        0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
        0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
        0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
        0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
        0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
        0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
        0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
        0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
        0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U
    };
    static const unsigned char r[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };
    uint32_t m[16];
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    unsigned int i;

    for (i = 0; i < 16U; i++) {
        m[i] = (uint32_t)block[i * 4U]
            | ((uint32_t)block[i * 4U + 1U] << 8)
            | ((uint32_t)block[i * 4U + 2U] << 16)
            | ((uint32_t)block[i * 4U + 3U] << 24);
    }
    for (i = 0; i < 64U; i++) {
        uint32_t f;
        uint32_t g;
        uint32_t tmp;

        if (i < 16U) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32U) {
            f = (d & b) | (~d & c);
            g = (5U * i + 1U) % 16U;
        } else if (i < 48U) {
            f = b ^ c ^ d;
            g = (3U * i + 5U) % 16U;
        } else {
            f = c ^ (b | ~d);
            g = (7U * i) % 16U;
        }
        tmp = d;
        d = c;
        c = b;
        b = b + rotl(a + f + k[i] + m[g], r[i]);
        a = tmp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}
