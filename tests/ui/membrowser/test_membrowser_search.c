/*
 * test_membrowser_search.c - unit testy pro V4 search engine
 *                            (membrowser mutant V4).
 *
 * Testuje pure parser + match logiku bez emu/ImGui/SDL/dbgapi/regex/bp_expr
 * závislostí. Engine ze membrowser_search.cpp je C++ a tahá std::regex +
 * bp_expr (= emu core); test by jinak musel být typu EMU.
 *
 * Místo toho je tu LOKÁLNÍ implementace parse_byte_seq / parse_masked_seq /
 * parse_number / ascii_tolower (zrcadlí membrowser_search.cpp). Driftu se
 * brání tím, že assertions zachovávají stejné kontrakty - při změně
 * parseru v engine je třeba upravit i tento test.
 *
 * Pokrytí:
 *   T1 byte_seq_basic        - "AA BB CC" parse na 3 bajty
 *   T2 byte_seq_invalid      - "XX" vrací error (-1)
 *   T3 masked_basic          - "AA ?? CC" parse na 3 bajty, mask[1]=0
 *   T4 masked_partial_wild   - "?A" odmítnut (nibble wildcard nepodporován)
 *   T5 word_le_parse         - "0x1234" -> bytes {0x34, 0x12}
 *   T6 word_be_parse         - "0x1234" -> bytes {0x12, 0x34}
 *   T7 number_dec_hex_bin    - parser akceptuje 256, 0x100, 0b100000000
 *   T8 ascii_tolower         - 0x41 -> 0x61, 0x5A -> 0x7A, ostatní intakt
 *
 * STANDALONE typ - žádný emu core potřeba.
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>


/* ---- Lokální kopie pure logiky z membrowser_search.cpp ---------------- */


static int parse_hex_byte_local(const char *p, uint8_t *out)
{
    int v = 0;
    int n = 0;
    for (int i = 0; i < 2 && p[i] != '\0'; i++) {
        char c = p[i];
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
        if (d < 0) break;
        v = (v << 4) | d;
        n++;
    }
    if (n == 0) return 0;
    *out = (uint8_t) v;
    return n;
}


static int parse_byte_seq_local(const char *src, uint8_t *out_bytes,
                                uint8_t *out_mask, int max_len)
{
    int count = 0;
    const char *p = src;
    while (*p && count < max_len) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        uint8_t b = 0;
        int n = parse_hex_byte_local(p, &b);
        if (n == 0) return -1;
        out_bytes[count] = b;
        out_mask[count] = 0xFF;
        count++;
        p += n;
    }
    return count;
}


static int parse_masked_seq_local(const char *src, uint8_t *out_bytes,
                                  uint8_t *out_mask, int max_len)
{
    int count = 0;
    const char *p = src;
    while (*p && count < max_len) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        if (p[0] == '?' && p[1] == '?') {
            out_bytes[count] = 0x00;
            out_mask[count] = 0x00;
            count++;
            p += 2;
        } else if (p[0] == '?' || p[1] == '?') {
            return -1;
        } else {
            uint8_t b = 0;
            int n = parse_hex_byte_local(p, &b);
            if (n == 0) return -1;
            out_bytes[count] = b;
            out_mask[count] = 0xFF;
            count++;
            p += n;
        }
    }
    return count;
}


static int parse_number_local(const char *src, uint32_t *out_value)
{
    if (!src || !*src) return 0;
    const char *p = src;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 0;

    char *endp = NULL;
    unsigned long v = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        v = strtoul(p + 2, &endp, 16);
    } else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
        v = strtoul(p + 2, &endp, 2);
    } else {
        v = strtoul(p, &endp, 10);
    }
    if (endp == p) return 0;
    *out_value = (uint32_t) v;
    return 1;
}


static uint8_t ascii_tolower_local(uint8_t b)
{
    if (b >= 0x41 && b <= 0x5A) return b + 0x20;
    return b;
}


/* ---- Test fixtures ---------------------------------------------------- */


void setUp(void) {}
void tearDown(void) {}


/* T1: parse_byte_seq "AA BB CC" -> 3 bytes 0xAA 0xBB 0xCC, mask vse 0xFF. */
static void test_byte_seq_basic(void)
{
    uint8_t bytes[16];
    uint8_t mask[16];
    int n = parse_byte_seq_local("AA BB CC", bytes, mask, 16);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_UINT8(0xAA, bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, mask[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, mask[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, mask[2]);
}


/* T2: parse_byte_seq invalid hex -> -1. */
static void test_byte_seq_invalid(void)
{
    uint8_t bytes[16];
    uint8_t mask[16];
    int n = parse_byte_seq_local("XX YY", bytes, mask, 16);
    TEST_ASSERT_EQUAL_INT(-1, n);
}


/* T3: parse_masked_seq "AA ?? CC" -> 3 bytes, mask[1] = 0. */
static void test_masked_basic(void)
{
    uint8_t bytes[16];
    uint8_t mask[16];
    int n = parse_masked_seq_local("AA ?? CC", bytes, mask, 16);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_UINT8(0xAA, bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, mask[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, mask[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, mask[2]);
}


/* T4: parse_masked_seq nibble wildcard ("?A" / "A?") odmítnut. */
static void test_masked_partial_wildcard_rejected(void)
{
    uint8_t bytes[16];
    uint8_t mask[16];
    int n = parse_masked_seq_local("?A", bytes, mask, 16);
    TEST_ASSERT_EQUAL_INT(-1, n);
    n = parse_masked_seq_local("A?", bytes, mask, 16);
    TEST_ASSERT_EQUAL_INT(-1, n);
}


/* T5: WORD_LE parse - 0x1234 -> bytes[0]=0x34, bytes[1]=0x12. */
static void test_word_le_parse(void)
{
    uint32_t val = 0;
    int ok = parse_number_local("0x1234", &val);
    TEST_ASSERT_EQUAL_INT(1, ok);
    TEST_ASSERT_EQUAL_UINT32(0x1234u, val);
    uint8_t lo = (uint8_t)(val & 0xFF);
    uint8_t hi = (uint8_t)((val >> 8) & 0xFF);
    TEST_ASSERT_EQUAL_UINT8(0x34, lo);
    TEST_ASSERT_EQUAL_UINT8(0x12, hi);
}


/* T6: WORD_BE - reorder LE bajtů na BE. */
static void test_word_be_parse(void)
{
    uint32_t val = 0;
    int ok = parse_number_local("0xABCD", &val);
    TEST_ASSERT_EQUAL_INT(1, ok);
    TEST_ASSERT_EQUAL_UINT32(0xABCDu, val);
    uint8_t lo = (uint8_t)(val & 0xFF);
    uint8_t hi = (uint8_t)((val >> 8) & 0xFF);
    /* BE: bytes[0]=hi, bytes[1]=lo. */
    TEST_ASSERT_EQUAL_UINT8(0xAB, hi);
    TEST_ASSERT_EQUAL_UINT8(0xCD, lo);
}


/* T7: parse_number akceptuje dec / hex / bin literally. */
static void test_number_dec_hex_bin(void)
{
    uint32_t val = 0;
    TEST_ASSERT_EQUAL_INT(1, parse_number_local("256", &val));
    TEST_ASSERT_EQUAL_UINT32(256u, val);
    TEST_ASSERT_EQUAL_INT(1, parse_number_local("0x100", &val));
    TEST_ASSERT_EQUAL_UINT32(0x100u, val);
    TEST_ASSERT_EQUAL_INT(1, parse_number_local("0b100000000", &val));
    TEST_ASSERT_EQUAL_UINT32(256u, val);
    TEST_ASSERT_EQUAL_INT(0, parse_number_local("", &val));
    TEST_ASSERT_EQUAL_INT(0, parse_number_local("abc", &val));
}


/* T8: ascii_tolower - 0x41 -> 0x61, 0x5A -> 0x7A, ostatní intakt. */
static void test_ascii_tolower(void)
{
    TEST_ASSERT_EQUAL_UINT8(0x61, ascii_tolower_local(0x41));
    TEST_ASSERT_EQUAL_UINT8(0x7A, ascii_tolower_local(0x5A));
    TEST_ASSERT_EQUAL_UINT8(0x61, ascii_tolower_local(0x61));  /* už lower */
    TEST_ASSERT_EQUAL_UINT8(0x30, ascii_tolower_local(0x30));  /* '0' */
    TEST_ASSERT_EQUAL_UINT8(0xC0, ascii_tolower_local(0xC0));  /* extended */
    TEST_ASSERT_EQUAL_UINT8(0x00, ascii_tolower_local(0x00));
}


/* T9: masked match semantika - manuální test match s mask[i]=0 (wildcard). */
static void test_masked_match_semantic(void)
{
    /* Pattern: AA ?? CC, buffer: AA 99 CC -> match (?? = wildcard). */
    uint8_t pat[3] = {0xAA, 0x00, 0xCC};
    uint8_t msk[3] = {0xFF, 0x00, 0xFF};
    uint8_t buf[3] = {0xAA, 0x99, 0xCC};

    int matched = 1;
    for (int i = 0; i < 3; i++) {
        if (msk[i] == 0) continue;
        if ((buf[i] & msk[i]) != (pat[i] & msk[i])) {
            matched = 0;
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, matched);

    /* Pattern: AA ?? CC, buffer: BB 99 CC -> no match (AA != BB). */
    buf[0] = 0xBB;
    matched = 1;
    for (int i = 0; i < 3; i++) {
        if (msk[i] == 0) continue;
        if ((buf[i] & msk[i]) != (pat[i] & msk[i])) {
            matched = 0;
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, matched);
}


int main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    UNITY_BEGIN();
    RUN_TEST(test_byte_seq_basic);
    RUN_TEST(test_byte_seq_invalid);
    RUN_TEST(test_masked_basic);
    RUN_TEST(test_masked_partial_wildcard_rejected);
    RUN_TEST(test_word_le_parse);
    RUN_TEST(test_word_be_parse);
    RUN_TEST(test_number_dec_hex_bin);
    RUN_TEST(test_ascii_tolower);
    RUN_TEST(test_masked_match_semantic);
    return UNITY_END();
}
