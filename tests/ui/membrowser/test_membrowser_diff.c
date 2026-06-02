/*
 * test_membrowser_diff.c - unit testy diff enginu (V5).
 *
 * Pure logika bez ImGui / emu / SDL závislostí - STANDALONE typ.
 *
 * Pokrývá:
 *   - rovné buffery (changed = 0)
 *   - kompletně odlišné buffery
 *   - částečné rozdíly + callback ordering
 *   - size mismatch (A < B i A > B)
 *   - NULL pointer safety (size 0)
 *   - is_changed helper
 *   - tail v size mismatch správně započítaný
 *   - callback může terminovat předčasně
 *
 * Licence: GPLv3
 */

#include "mztest.h"

/* Kopírujeme implementační soubor přímo - vyhneme se závislosti na build
 * konfiguraci membrowser engine, plus diff je pure C bez external dep. */
#include "../../../src/ui-imgui/debugger/membrowser/membrowser_diff_engine.h"

#include <string.h>


void setUp(void) {}
void tearDown(void) {}


/* T1: dva rovné buffery - changed = 0, žádný mismatch. */
static void test_equal_buffers(void)
{
    uint8_t a[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t b[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    st_MB_DIFF_STATS s;

    uint64_t ch = membrowser_diff_compute ( a, 8, b, 8, NULL, NULL, &s );

    TEST_ASSERT_EQUAL_UINT64 ( 0, ch );
    TEST_ASSERT_EQUAL_UINT64 ( 0, s.changed );
    TEST_ASSERT_EQUAL_UINT64 ( 8, s.compared_bytes );
    TEST_ASSERT_FALSE ( s.size_mismatch );
}


/* T2: kompletně odlišné buffery. */
static void test_fully_different(void)
{
    uint8_t a[4] = { 0x00, 0x00, 0x00, 0x00 };
    uint8_t b[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    st_MB_DIFF_STATS s;

    uint64_t ch = membrowser_diff_compute ( a, 4, b, 4, NULL, NULL, &s );

    TEST_ASSERT_EQUAL_UINT64 ( 4, ch );
    TEST_ASSERT_EQUAL_UINT64 ( 4, s.changed );
    TEST_ASSERT_EQUAL_UINT64 ( 4, s.compared_bytes );
    TEST_ASSERT_FALSE ( s.size_mismatch );
}


/* Callback log pro test ordering. */
typedef struct {
    uint64_t offsets[16];
    uint8_t  vals_a[16];
    uint8_t  vals_b[16];
    int      count;
} test_cb_log_t;

static bool log_cb ( void *user, uint64_t off, uint8_t va, uint8_t vb )
{
    test_cb_log_t *log = (test_cb_log_t *) user;
    if ( log->count < 16 ) {
        log->offsets[log->count] = off;
        log->vals_a[log->count] = va;
        log->vals_b[log->count] = vb;
        log->count++;
    }
    return true;
}


/* T3: částečné rozdíly + callback ordering. */
static void test_partial_diff_callback(void)
{
    uint8_t a[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t b[8] = { 1, 9, 3, 4, 0, 6, 7, 8 };  /* změna off=1 a off=4 */
    st_MB_DIFF_STATS s;
    test_cb_log_t log;
    memset ( &log, 0, sizeof ( log ) );

    uint64_t ch = membrowser_diff_compute ( a, 8, b, 8, log_cb, &log, &s );

    TEST_ASSERT_EQUAL_UINT64 ( 2, ch );
    TEST_ASSERT_EQUAL_INT ( 2, log.count );
    TEST_ASSERT_EQUAL_UINT64 ( 1, log.offsets[0] );
    TEST_ASSERT_EQUAL_UINT8 ( 2, log.vals_a[0] );
    TEST_ASSERT_EQUAL_UINT8 ( 9, log.vals_b[0] );
    TEST_ASSERT_EQUAL_UINT64 ( 4, log.offsets[1] );
    TEST_ASSERT_EQUAL_UINT8 ( 5, log.vals_a[1] );
    TEST_ASSERT_EQUAL_UINT8 ( 0, log.vals_b[1] );
}


/* T4: size mismatch - A kratší než B. */
static void test_size_mismatch_a_shorter(void)
{
    uint8_t a[3] = { 1, 2, 3 };
    uint8_t b[5] = { 1, 2, 3, 4, 5 };
    st_MB_DIFF_STATS s;

    uint64_t ch = membrowser_diff_compute ( a, 3, b, 5, NULL, NULL, &s );

    TEST_ASSERT_EQUAL_UINT64 ( 3, s.compared_bytes );
    TEST_ASSERT_TRUE ( s.size_mismatch );
    /* Tail = 2 (b je o 2 bajty delší), prefix shoda = 0 změn,
     * celkem changed = 2. */
    TEST_ASSERT_EQUAL_UINT64 ( 2, ch );
}


/* T5: size mismatch - A delší než B. */
static void test_size_mismatch_a_longer(void)
{
    uint8_t a[5] = { 1, 2, 3, 4, 5 };
    uint8_t b[2] = { 1, 9 };  /* prefix má 1 rozdíl + tail 3 */
    st_MB_DIFF_STATS s;

    uint64_t ch = membrowser_diff_compute ( a, 5, b, 2, NULL, NULL, &s );

    TEST_ASSERT_EQUAL_UINT64 ( 2, s.compared_bytes );
    TEST_ASSERT_TRUE ( s.size_mismatch );
    TEST_ASSERT_EQUAL_UINT64 ( 1 + 3, ch );
}


/* T6: oba buffery prázdné - safe edge case. */
static void test_both_empty(void)
{
    st_MB_DIFF_STATS s;
    uint64_t ch = membrowser_diff_compute ( NULL, 0, NULL, 0, NULL, NULL, &s );

    TEST_ASSERT_EQUAL_UINT64 ( 0, ch );
    TEST_ASSERT_EQUAL_UINT64 ( 0, s.compared_bytes );
    TEST_ASSERT_FALSE ( s.size_mismatch );
}


/* T7: NULL stats - graceful no-op (vrací 0). */
static void test_null_stats(void)
{
    uint8_t a[2] = { 1, 2 };
    uint8_t b[2] = { 3, 4 };
    uint64_t ch = membrowser_diff_compute ( a, 2, b, 2, NULL, NULL, NULL );
    TEST_ASSERT_EQUAL_UINT64 ( 0, ch );
}


/* T8: is_changed helper - in-range obojí. */
static void test_is_changed_in_range(void)
{
    uint8_t a[4] = { 1, 2, 3, 4 };
    uint8_t b[4] = { 1, 9, 3, 9 };

    TEST_ASSERT_FALSE ( membrowser_diff_is_changed ( a, 4, b, 4, 0 ) );
    TEST_ASSERT_TRUE  ( membrowser_diff_is_changed ( a, 4, b, 4, 1 ) );
    TEST_ASSERT_FALSE ( membrowser_diff_is_changed ( a, 4, b, 4, 2 ) );
    TEST_ASSERT_TRUE  ( membrowser_diff_is_changed ( a, 4, b, 4, 3 ) );
}


/* T9: is_changed - tail v size mismatch. */
static void test_is_changed_tail(void)
{
    uint8_t a[2] = { 1, 2 };
    uint8_t b[4] = { 1, 2, 3, 4 };

    /* Prefix stejný. */
    TEST_ASSERT_FALSE ( membrowser_diff_is_changed ( a, 2, b, 4, 0 ) );
    TEST_ASSERT_FALSE ( membrowser_diff_is_changed ( a, 2, b, 4, 1 ) );
    /* Tail v B (mimo A) - changed=true. */
    TEST_ASSERT_TRUE ( membrowser_diff_is_changed ( a, 2, b, 4, 2 ) );
    TEST_ASSERT_TRUE ( membrowser_diff_is_changed ( a, 2, b, 4, 3 ) );
    /* Mimo obou - false. */
    TEST_ASSERT_FALSE ( membrowser_diff_is_changed ( a, 2, b, 4, 4 ) );
}


/* Callback co terminuje po prvním rozdílu. */
static bool stop_after_first ( void *user, uint64_t off, uint8_t va, uint8_t vb )
{
    int *cnt = (int *) user;
    (*cnt)++;
    (void) off; (void) va; (void) vb;
    return false;
}


/* T10: callback může předčasně ukončit iteraci. */
static void test_callback_early_terminate(void)
{
    uint8_t a[6] = { 1, 2, 3, 4, 5, 6 };
    uint8_t b[6] = { 9, 9, 9, 9, 9, 9 };
    st_MB_DIFF_STATS s;
    int cnt = 0;

    membrowser_diff_compute ( a, 6, b, 6, stop_after_first, &cnt, &s );

    /* Callback se zavolal právě jednou (po prvním diff bajtu break). */
    TEST_ASSERT_EQUAL_INT ( 1, cnt );
    /* Stats.changed obsahuje jen doiterované diffy (1), bez tail
     * (= obě stejně velké). */
    TEST_ASSERT_EQUAL_UINT64 ( 1, s.changed );
}


int main(int argc, char **argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_equal_buffers);
    RUN_TEST(test_fully_different);
    RUN_TEST(test_partial_diff_callback);
    RUN_TEST(test_size_mismatch_a_shorter);
    RUN_TEST(test_size_mismatch_a_longer);
    RUN_TEST(test_both_empty);
    RUN_TEST(test_null_stats);
    RUN_TEST(test_is_changed_in_range);
    RUN_TEST(test_is_changed_tail);
    RUN_TEST(test_callback_early_terminate);

    (void) argc; (void) argv;
    return UNITY_END();
}
