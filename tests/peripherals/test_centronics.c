/*
 * test_centronics.c — unit testy pro virtuální Centronics tiskárnu
 *
 * Testuje: STROBE edge detekci, capture do souboru, handshake gate (active),
 *          uzavření souboru a založení nového.
 *
 * Capture soubory vznikají v pracovním adresáři testu; každý test po sobě
 * uklízí (close + remove).
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "hw-generic/centronics/centronics.h"

/* ----------------------------------------------------------------------------
 * Pomocné funkce
 * ------------------------------------------------------------------------- */

/** Vynuluje stav modulu do klidového stavu (bez otevřeného souboru). */
static void centronics_test_reset(void)
{
    /* Pozn.: setUp běží po tearDown, který už soubor zavřel a smazal -
     * fp je tedy NULL a memset je bezpečný (nezahodíme živý handle). */
    memset(&g_centronics, 0x00, sizeof(g_centronics));
}

/** Simuluje STROBE pulz na PA7 s danými daty na PB.
 *
 * STROBE je aktivní HIGH (jako v ovladači MRS): assert = náběžná hrana 0 -> 1,
 * kde se bajt sbírá; deassert = sestupná hrana 1 -> 0. Helper vstupuje i opouští
 * klidovou úroveň (PA7 = 0), takže jedno volání = právě jeden zachycený bajt. */
static void centronics_test_strobe_pulse(uint8_t pb)
{
    centronics_pa_strobe_update(0, pb); /* klid (idle low) - bez capture */
    centronics_pa_strobe_update(1, pb); /* STROBE assert (náběžná hrana) - capture */
    centronics_pa_strobe_update(0, pb); /* STROBE deassert (sestupná hrana) - bez capture */
}

/** Načte obsah capture souboru. Vrací počet přečtených bajtů, nebo -1 při chybě. */
static long centronics_test_read_file(const char *name, uint8_t *buf, long maxlen)
{
    FILE *f = fopen(name, "rb");
    if (!f) return -1;
    long n = (long) fread(buf, 1, (size_t) maxlen, f);
    fclose(f);
    return n;
}

void setUp(void)
{
    centronics_test_reset();
}

void tearDown(void)
{
    /* Úklid: zavřít + smazat případně vzniklý capture soubor. */
    if (g_centronics.fp != NULL)
    {
        char name[CENTRONICS_FILENAME_MAX];
        snprintf(name, sizeof(name), "%s", g_centronics.filename);
        centronics_close_file();
        remove(name);
    };
}

/* ================================================================
 * SMOKE
 * ================================================================ */

/* Po resetu je modul neaktivní a bez otevřeného souboru. */
void test_centronics_init_state(void)
{
    TEST_ASSERT_FALSE(centronics_get_active());
    TEST_ASSERT_NULL(g_centronics.fp);
    TEST_ASSERT_EQUAL_UINT32(0, g_centronics.byte_count);
    TEST_ASSERT_EQUAL_UINT32(0, g_centronics.total_byte_count);
}

/* ================================================================
 * UNIT
 * ================================================================ */

/* Neaktivní tiskárna nezachytává - STROBE pulz nevytvoří soubor ani byte. */
void test_centronics_inactive_no_capture(void)
{
    centronics_set_active(false);
    centronics_test_strobe_pulse(0xAA);

    TEST_ASSERT_NULL(g_centronics.fp);
    TEST_ASSERT_EQUAL_UINT32(0, g_centronics.byte_count);
    TEST_ASSERT_EQUAL_UINT32(0, g_centronics.total_byte_count);
}

/* Aktivní tiskárna zachytí byte na sestupné hraně STROBE a uloží ho do souboru. */
void test_centronics_capture_on_falling_edge(void)
{
    centronics_set_active(true);
    centronics_test_strobe_pulse(0x41); /* 'A' */

    TEST_ASSERT_EQUAL_UINT32(1, g_centronics.byte_count);
    TEST_ASSERT_EQUAL_UINT32(1, g_centronics.total_byte_count);
    TEST_ASSERT_NOT_NULL(g_centronics.fp);

    /* Ověření obsahu - soubor nejdřív zavřeme (flush + uvolnění handle). */
    char name[CENTRONICS_FILENAME_MAX];
    snprintf(name, sizeof(name), "%s", g_centronics.filename);
    centronics_close_file();

    uint8_t buf[4];
    long n = centronics_test_read_file(name, buf, sizeof(buf));
    remove(name);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX8(0x41, buf[0]);
}

/* Jeden STROBE pulz zachytí přesně jeden byte; držení STROBE v úrovni 1
 * (bez nové náběžné hrany) nezpůsobí opakovaný capture. */
void test_centronics_no_double_capture(void)
{
    centronics_set_active(true);

    centronics_pa_strobe_update(0, 0x55); /* idle */
    centronics_pa_strobe_update(1, 0x55); /* náběžná hrana - capture */
    centronics_pa_strobe_update(1, 0x55); /* setrvání v 1 - bez nové hrany */
    centronics_pa_strobe_update(1, 0x55); /* setrvání v 1 - bez nové hrany */

    TEST_ASSERT_EQUAL_UINT32(1, g_centronics.byte_count);
}

/* Více bajtů se uloží ve správném pořadí. */
void test_centronics_multiple_bytes_in_order(void)
{
    centronics_set_active(true);

    const uint8_t data[] = { 0x10, 0x20, 0x30, 0x7F };
    for (unsigned i = 0; i < sizeof(data); i++)
    {
        centronics_test_strobe_pulse(data[i]);
    };

    TEST_ASSERT_EQUAL_UINT32(sizeof(data), g_centronics.byte_count);

    char name[CENTRONICS_FILENAME_MAX];
    snprintf(name, sizeof(name), "%s", g_centronics.filename);
    centronics_close_file();

    uint8_t buf[8];
    long n = centronics_test_read_file(name, buf, sizeof(buf));
    remove(name);

    TEST_ASSERT_EQUAL_INT((long) sizeof(data), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, buf, sizeof(data));
}

/* Uzavření souboru vynuluje aktuální počítadlo; další byte založí nový soubor. */
void test_centronics_close_starts_new_file(void)
{
    centronics_set_active(true);

    centronics_test_strobe_pulse(0x01);
    char first[CENTRONICS_FILENAME_MAX];
    snprintf(first, sizeof(first), "%s", g_centronics.filename);
    TEST_ASSERT_EQUAL_UINT32(1, g_centronics.byte_count);

    centronics_close_file();
    TEST_ASSERT_NULL(g_centronics.fp);
    TEST_ASSERT_EQUAL_UINT32(0, g_centronics.byte_count);

    /* Další byte = nový soubor, ale session total pokračuje. */
    centronics_test_strobe_pulse(0x02);
    TEST_ASSERT_NOT_NULL(g_centronics.fp);
    TEST_ASSERT_EQUAL_UINT32(1, g_centronics.byte_count);
    TEST_ASSERT_EQUAL_UINT32(2, g_centronics.total_byte_count);

    /* Úklid prvního souboru (druhý uklidí tearDown). */
    remove(first);
}

/* Vypnutí během session zastaví další capture. */
void test_centronics_disable_stops_capture(void)
{
    centronics_set_active(true);
    centronics_test_strobe_pulse(0xC0);
    TEST_ASSERT_EQUAL_UINT32(1, g_centronics.byte_count);

    centronics_set_active(false);
    centronics_test_strobe_pulse(0xC1);

    /* Žádný nový byte nepřibyl. */
    TEST_ASSERT_EQUAL_UINT32(1, g_centronics.byte_count);
    TEST_ASSERT_EQUAL_UINT32(1, g_centronics.total_byte_count);
}

/* reset() resynchronizuje STROBE baseline, ale neuzavře soubor. */
void test_centronics_reset_keeps_file(void)
{
    centronics_set_active(true);
    centronics_test_strobe_pulse(0xE0);
    TEST_ASSERT_NOT_NULL(g_centronics.fp);

    centronics_reset();
    TEST_ASSERT_NOT_NULL(g_centronics.fp); /* soubor zůstává otevřen */
    TEST_ASSERT_EQUAL_UINT8(0, g_centronics.last_strobe); /* baseline resync */
}

/* === MAIN === */

int main(int argc, char *argv[])
{
    mztest_parse_args(argc, argv);
    mztest_init();

    UNITY_BEGIN();

    /* smoke */
    RUN_TEST(test_centronics_init_state);

    /* unit */
    RUN_TEST(test_centronics_inactive_no_capture);
    RUN_TEST(test_centronics_capture_on_falling_edge);
    RUN_TEST(test_centronics_no_double_capture);
    RUN_TEST(test_centronics_multiple_bytes_in_order);
    RUN_TEST(test_centronics_close_starts_new_file);
    RUN_TEST(test_centronics_disable_stops_capture);
    RUN_TEST(test_centronics_reset_keeps_file);

    int result = UNITY_END();
    mztest_teardown();
    return result;
}
