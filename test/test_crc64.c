/* test_crc64.c -- Functional tests for the single slice-by-8 platform_crc64 */
#include "test_framework.h"
#include "platform.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Reference: the historical byte-wise implementation, kept here only as an oracle. */
static uint64_t g_ref_table[256];
static int g_ref_ready = 0;
static uint64_t ref_crc64(const void *data, size_t len)
{
    if (!data || len == 0) return 0;
    if (!g_ref_ready) {
        for (int i = 0; i < 256; i++) {
            uint64_t c = (uint64_t)i;
            for (int j = 0; j < 8; j++) c = (c & 1) ? (c >> 1) ^ 0x42F0E1EBA9EA3693ULL : c >> 1;
            g_ref_table[i] = c;
        }
        g_ref_ready = 1;
    }
    const uint8_t *b = (const uint8_t *)data;
    uint64_t c = ~0ULL;
    for (size_t i = 0; i < len; i++) c = g_ref_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

static void fill_random(uint8_t *buf, size_t n, uint32_t seed)
{
    for (size_t i = 0; i < n; i++) { seed = seed * 1103515245u + 12345u; buf[i] = (uint8_t)(seed >> 16); }
}

TEST(crc64_golden_values) {
    ASSERT_TRUE(platform_crc64("", 0) == 0ULL);
    ASSERT_TRUE(platform_crc64("a", 1) == 0x856176DE4CFF4E76ULL);
    ASSERT_TRUE(platform_crc64("123456789", 9) == 0xB86883E6FA710A9FULL);
    const char *fox = "The quick brown fox jumps over the lazy dog";
    ASSERT_TRUE(platform_crc64(fox, strlen(fox)) == 0xF4C4E982E838D97EULL);
    return 0;
}

TEST(crc64_null_and_empty) {
    ASSERT_TRUE(platform_crc64(NULL, 10) == 0ULL);
    ASSERT_TRUE(platform_crc64_update(0x1234ULL, NULL, 0) == 0x1234ULL);
    ASSERT_TRUE(platform_crc64_update(0x1234ULL, "x", 0) == 0x1234ULL);
    return 0;
}

TEST(crc64_matches_reference_every_alignment_and_length) {
    enum { CAP = 4096 + 64 };
    uint8_t *buf = (uint8_t *)malloc(CAP);
    ASSERT_NOT_NULL(buf);
    fill_random(buf, CAP, 0xC0FFEEu);
    for (size_t off = 0; off < 16; off++) {
        for (size_t len = 0; len <= 300; len++) {
            if (platform_crc64(buf + off, len) != ref_crc64(buf + off, len)) {
                printf("  mismatch off=%zu len=%zu\n", off, len);
                free(buf);
                ASSERT_TRUE(0);
            }
        }
        ASSERT_TRUE(platform_crc64(buf + off, 4096) == ref_crc64(buf + off, 4096));
    }
    free(buf);
    return 0;
}

TEST(crc64_update_chains_at_every_split) {
    uint8_t buf[200];
    fill_random(buf, sizeof(buf), 42u);
    uint64_t whole = platform_crc64(buf, sizeof(buf));
    for (size_t split = 0; split <= sizeof(buf); split++) {
        uint64_t c = platform_crc64_update(0, buf, split);
        c = platform_crc64_update(c, buf + split, sizeof(buf) - split);
        ASSERT_TRUE(c == whole);
    }
    return 0;
}

static double now_seconds(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

TEST(crc64_slice8_faster_than_bytewise) {
    const size_t n = 32u * 1024u * 1024u;
    uint8_t *buf = (uint8_t *)malloc(n);
    ASSERT_NOT_NULL(buf);
    fill_random(buf, n, 7u);
    double best_ref = 1e9, best_new = 1e9;
    volatile uint64_t sink = 0;
    for (int r = 0; r < 3; r++) {
        double t0 = now_seconds(); sink ^= ref_crc64(buf, n); double t1 = now_seconds();
        sink ^= platform_crc64(buf, n); double t2 = now_seconds();
        if (t1 - t0 < best_ref) best_ref = t1 - t0;
        if (t2 - t1 < best_new) best_new = t2 - t1;
    }
    (void)sink;
    if (best_new <= 0) best_new = 1e-6;
    printf("  bytewise %.0f MB/s, slice-by-8 %.0f MB/s (%.2fx)\n",
           32.0 / best_ref, 32.0 / best_new, best_ref / best_new);
#if defined(_MSC_VER) || defined(__OPTIMIZE__)
    ASSERT_TRUE(best_ref / best_new >= 3.0);
#else
    ASSERT_TRUE(best_ref / best_new >= 1.5);   /* Linux test builds are -O0 */
#endif
    free(buf);
    return 0;
}

TEST_MAIN(
    RUN_TEST(crc64_golden_values);
    RUN_TEST(crc64_null_and_empty);
    RUN_TEST(crc64_matches_reference_every_alignment_and_length);
    RUN_TEST(crc64_update_chains_at_every_split);
    RUN_TEST(crc64_slice8_faster_than_bytewise);
)
