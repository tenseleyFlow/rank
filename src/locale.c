#include "rank_locale.h"

#include "util.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>

static bool locale_name_is_identity(const char *name);
static bool probe_collation_identity(void);
static int compare_bytes(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len);
static int collate_segments(const char *a, size_t a_size, const char *b, size_t b_size);

static int identity_cached = -1;

void
rank_locale_module_present(void)
{
}

bool
rank_locale_init(const struct rank_options *options)
{
    if (setlocale(LC_ALL, "") == NULL) {
        rank_diag(options, "failed to set locale");
        return false;
    }
    identity_cached = -1;
    return true;
}

bool
rank_locale_collation_identity(void)
{
    if (identity_cached < 0) {
        identity_cached = probe_collation_identity() ? 1 : 0;
    }
    return identity_cached != 0;
}

static bool
probe_collation_identity(void)
{
    const char *name = setlocale(LC_COLLATE, NULL);
    unsigned int i;
    unsigned int j;

    if (locale_name_is_identity(name)) {
        return true;
    }
    if (MB_CUR_MAX != 1) {
        return false;
    }
    for (i = 1; i <= 255U; i++) {
        char a[2];

        a[0] = (char)i;
        a[1] = '\0';
        for (j = 1; j <= 255U; j++) {
            char b[2];
            int coll;
            int byte;

            b[0] = (char)j;
            b[1] = '\0';
            coll = strcoll(a, b);
            byte = i == j ? 0 : (i < j ? -1 : 1);
            if ((coll < 0 && byte >= 0) || (coll > 0 && byte <= 0) || (coll == 0 && byte != 0)) {
                return false;
            }
        }
    }
    return true;
}

int
rank_locale_compare(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len)
{
    char *a_str;
    char *b_str;
    int result;

    if (rank_locale_collation_identity()) {
        return compare_bytes(a, a_len, b, b_len);
    }
    if (a_len == b_len && (a_len == 0 || memcmp(a, b, a_len) == 0)) {
        return 0;
    }
    a_str = rank_xmalloc(a_len + 1U);
    b_str = rank_xmalloc(b_len + 1U);
    memcpy(a_str, a, a_len);
    memcpy(b_str, b, b_len);
    a_str[a_len] = '\0';
    b_str[b_len] = '\0';
    result = collate_segments(a_str, a_len + 1U, b_str, b_len + 1U);
    free(a_str);
    free(b_str);
    return result;
}

/* GNU memcoll semantics: strcoll each NUL-terminated segment; when a
   segment collates equal, step past the NULs on both sides and keep
   going, so embedded NULs act as segment breaks, not terminators.
   Sizes include the trailing NUL appended by the caller. */
static int
collate_segments(const char *a, size_t a_size, const char *b, size_t b_size)
{
    for (;;) {
        int diff = strcoll(a, b);
        size_t a_seg;
        size_t b_seg;

        if (diff != 0) {
            return diff < 0 ? -1 : 1;
        }
        a_seg = strlen(a) + 1U;
        b_seg = strlen(b) + 1U;
        a += a_seg;
        b += b_seg;
        a_size -= a_seg;
        b_size -= b_seg;
        if (a_size == 0) {
            return b_size != 0 ? -1 : 0;
        }
        if (b_size == 0) {
            return 1;
        }
    }
}

static bool
locale_name_is_identity(const char *name)
{
    return name != NULL && (strcmp(name, "C") == 0 || strcmp(name, "POSIX") == 0);
}

static int
compare_bytes(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len)
{
    size_t common = a_len < b_len ? a_len : b_len;
    int result = 0;

    if (common > 0) {
        result = memcmp(a, b, common);
        if (result != 0) {
            return result < 0 ? -1 : 1;
        }
    }
    if (a_len < b_len) {
        return -1;
    }
    if (a_len > b_len) {
        return 1;
    }
    return 0;
}

