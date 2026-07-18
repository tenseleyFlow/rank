#include "sys/scan.h"

size_t
rank_scan_blank_scalar(const unsigned char *text, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (text[i] == (unsigned char)' ' || text[i] == (unsigned char)'\t') {
            return i;
        }
    }
    return len;
}

size_t
rank_scan_nonblank_scalar(const unsigned char *text, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (text[i] != (unsigned char)' ' && text[i] != (unsigned char)'\t') {
            return i;
        }
    }
    return len;
}

void
rank_fold_upper_scalar(unsigned char *dst, const unsigned char *src, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char byte = src[i];

        dst[i] = byte >= (unsigned char)'a' && byte <= (unsigned char)'z'
            ? (unsigned char)(byte - ('a' - 'A'))
            : byte;
    }
}
