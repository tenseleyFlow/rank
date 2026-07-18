#ifndef RANK_SYS_SCAN_H
#define RANK_SYS_SCAN_H

#include <stddef.h>

#if defined(__SSE2__)
#include <emmintrin.h>
#define RANK_SCAN_SSE2 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define RANK_SCAN_NEON 1
#endif

/* Scalar reference implementations; the oracle for fuzzing the
   accelerated inline paths below. */
size_t rank_scan_blank_scalar(const unsigned char *text, size_t len);
size_t rank_scan_nonblank_scalar(const unsigned char *text, size_t len);
void rank_fold_upper_scalar(unsigned char *dst, const unsigned char *src, size_t len);

/* The hot callers walk many short fields, so these stay inline: short
   spans never leave the scalar tail and pay no call overhead, long
   spans hit the 16-byte kernels. */

/* Index of the first blank (space or tab) in text, or len. */
static inline size_t
rank_scan_blank(const unsigned char *text, size_t len)
{
    size_t i = 0;

#if defined(RANK_SCAN_SSE2)
    if (len >= 16U) {
        const __m128i space = _mm_set1_epi8(' ');
        const __m128i tab = _mm_set1_epi8('\t');

        while (i + 16U <= len) {
            __m128i chunk = _mm_loadu_si128((const __m128i *)(const void *)(text + i));
            __m128i hit = _mm_or_si128(_mm_cmpeq_epi8(chunk, space), _mm_cmpeq_epi8(chunk, tab));
            unsigned int mask = (unsigned int)_mm_movemask_epi8(hit);

            if (mask != 0) {
                return i + (size_t)__builtin_ctz(mask);
            }
            i += 16U;
        }
    }
#elif defined(RANK_SCAN_NEON)
    const uint8x16_t space = vdupq_n_u8(' ');
    const uint8x16_t tab = vdupq_n_u8('\t');

    while (i + 16U <= len) {
        uint8x16_t chunk = vld1q_u8(text + i);
        uint8x16_t hit = vorrq_u8(vceqq_u8(chunk, space), vceqq_u8(chunk, tab));

        if (vmaxvq_u8(hit) != 0) {
            break;
        }
        i += 16U;
    }
#endif
    for (; i < len; i++) {
        if (text[i] == (unsigned char)' ' || text[i] == (unsigned char)'\t') {
            return i;
        }
    }
    return len;
}

/* Index of the first non-blank byte in text, or len. */
static inline size_t
rank_scan_nonblank(const unsigned char *text, size_t len)
{
    size_t i = 0;

#if defined(RANK_SCAN_SSE2)
    if (len >= 16U) {
        const __m128i space = _mm_set1_epi8(' ');
        const __m128i tab = _mm_set1_epi8('\t');

        while (i + 16U <= len) {
            __m128i chunk = _mm_loadu_si128((const __m128i *)(const void *)(text + i));
            __m128i blank = _mm_or_si128(_mm_cmpeq_epi8(chunk, space), _mm_cmpeq_epi8(chunk, tab));
            unsigned int mask = (unsigned int)_mm_movemask_epi8(blank) ^ 0xffffU;

            if (mask != 0) {
                return i + (size_t)__builtin_ctz(mask);
            }
            i += 16U;
        }
    }
#elif defined(RANK_SCAN_NEON)
    const uint8x16_t space = vdupq_n_u8(' ');
    const uint8x16_t tab = vdupq_n_u8('\t');

    while (i + 16U <= len) {
        uint8x16_t chunk = vld1q_u8(text + i);
        uint8x16_t blank = vorrq_u8(vceqq_u8(chunk, space), vceqq_u8(chunk, tab));

        if (vminvq_u8(blank) == 0) {
            break;
        }
        i += 16U;
    }
#endif
    for (; i < len; i++) {
        if (text[i] != (unsigned char)' ' && text[i] != (unsigned char)'\t') {
            return i;
        }
    }
    return len;
}

/* Copy src to dst folding ASCII a-z to A-Z. Regions must not overlap. */
static inline void
rank_fold_upper(unsigned char *dst, const unsigned char *src, size_t len)
{
    size_t i = 0;

#if defined(RANK_SCAN_SSE2)
    /* Signed compares are safe: a-z sit below 0x80, and bytes >= 0x80
       compare negative so the low bound rejects them. */
    const __m128i low = _mm_set1_epi8('a' - 1);
    const __m128i high = _mm_set1_epi8('z' + 1);
    const __m128i delta = _mm_set1_epi8('a' - 'A');

    while (i + 16U <= len) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(const void *)(src + i));
        __m128i lower = _mm_and_si128(_mm_cmpgt_epi8(chunk, low), _mm_cmplt_epi8(chunk, high));
        __m128i folded = _mm_sub_epi8(chunk, _mm_and_si128(lower, delta));

        _mm_storeu_si128((__m128i *)(void *)(dst + i), folded);
        i += 16U;
    }
#elif defined(RANK_SCAN_NEON)
    const uint8x16_t low = vdupq_n_u8('a');
    const uint8x16_t high = vdupq_n_u8('z');
    const uint8x16_t delta = vdupq_n_u8('a' - 'A');

    while (i + 16U <= len) {
        uint8x16_t chunk = vld1q_u8(src + i);
        uint8x16_t lower = vandq_u8(vcgeq_u8(chunk, low), vcleq_u8(chunk, high));
        uint8x16_t folded = vsubq_u8(chunk, vandq_u8(lower, delta));

        vst1q_u8(dst + i, folded);
        i += 16U;
    }
#endif
    for (; i < len; i++) {
        unsigned char byte = src[i];

        dst[i] = byte >= (unsigned char)'a' && byte <= (unsigned char)'z'
            ? (unsigned char)(byte - ('a' - 'A'))
            : byte;
    }
}

#endif
