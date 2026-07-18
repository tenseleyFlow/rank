#include "sys/scan.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_TRIALS 50000
#define FUZZ_MAX_LEN 512
#define FUZZ_MAX_OFFSET 32

static uint32_t rng_state = 0x2545f491U;

static uint32_t
rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int
main(void)
{
    static unsigned char buf[FUZZ_MAX_LEN + FUZZ_MAX_OFFSET];
    static unsigned char fold_a[FUZZ_MAX_LEN + FUZZ_MAX_OFFSET];
    static unsigned char fold_b[FUZZ_MAX_LEN + FUZZ_MAX_OFFSET];
    unsigned int trial;

    for (trial = 0; trial < FUZZ_TRIALS; trial++) {
        size_t len = (size_t)(rng() % (FUZZ_MAX_LEN + 1U));
        size_t off = (size_t)(rng() % (FUZZ_MAX_OFFSET + 1U));
        const unsigned char *text = buf + off;
        size_t i;

        for (i = 0; i < len; i++) {
            uint32_t roll = rng();

            if (roll % 4U == 0) {
                buf[off + i] = roll % 8U == 0 ? (unsigned char)'\t' : (unsigned char)' ';
            } else {
                buf[off + i] = (unsigned char)(roll >> 8);
            }
        }

        if (rank_scan_blank(text, len) != rank_scan_blank_scalar(text, len)) {
            fprintf(stderr, "scan-fuzz: blank mismatch trial=%u len=%zu off=%zu\n", trial, len, off);
            return 1;
        }
        if (rank_scan_nonblank(text, len) != rank_scan_nonblank_scalar(text, len)) {
            fprintf(stderr, "scan-fuzz: nonblank mismatch trial=%u len=%zu off=%zu\n", trial, len, off);
            return 1;
        }
        rank_fold_upper(fold_a + off, text, len);
        rank_fold_upper_scalar(fold_b + off, text, len);
        if (len > 0 && memcmp(fold_a + off, fold_b + off, len) != 0) {
            fprintf(stderr, "scan-fuzz: fold mismatch trial=%u len=%zu off=%zu\n", trial, len, off);
            return 1;
        }
    }
    printf("scan fuzz ok\n");
    return 0;
}
