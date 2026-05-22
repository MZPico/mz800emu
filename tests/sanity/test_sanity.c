/*
 * test_sanity.c — základní sanity test
 *
 * Ověřuje, že testovací framework funguje a že se emulátorový
 * stav dá inicializovat v headless režimu.
 *
 * Licence: GPLv3
 */

#include "mztest.h"

/* emulátorové hlavičky pro ověření stavu */
#include "emulator/emulator.h"
#include "hw-generic/memory/memory.h"
#include "iface/iface_video.h"

/*
 * Unity setUp/tearDown — volá se před/po KAŽDÉM testu
 */
void setUp(void)
{
    /* inicializaci provádíme jen jednou v main() */
}

void tearDown(void)
{
    /* teardown provádíme jen jednou v main() */
}

/* === SMOKE TESTY === */

/* framework funguje */
void test_framework_works(void)
{
    TEST_ASSERT_EQUAL_INT(1, 1);
    TEST_ASSERT_TRUE(true);
    TEST_ASSERT_FALSE(false);
}

/* emulátorový stav je inicializován */
void test_emulator_state_initialized(void)
{
    /* g_emulator by měla být vynulovaná */
    TEST_ASSERT_FALSE(g_emulator.paused);
    TEST_ASSERT_FALSE(g_emulator.max_speed);
}

/* video interface je inicializován (stub) */
void test_video_interface_initialized(void)
{
    TEST_ASSERT_NOT_NULL(g_iface_video);
    TEST_ASSERT_TRUE(g_iface_video->is_initialized);
}

/* paměť je inicializovaná */
void test_memory_initialized(void)
{
    /* po inicializaci by měla být RAM přístupná */
    g_memory.RAM[0x1000] = 0x42;
    TEST_ASSERT_EQUAL_HEX8(0x42, g_memory.RAM[0x1000]);

    g_memory.RAM[0x1000] = 0x00;
    TEST_ASSERT_EQUAL_HEX8(0x00, g_memory.RAM[0x1000]);
}

/* test level filtering funguje */
void test_level_filtering_smoke(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_SMOKE);
    TEST_ASSERT_TRUE(true); /* vždy projde */
}

void test_level_filtering_full(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_FULL);
    /* tento test se spustí jen při --level full */
    TEST_ASSERT_TRUE(true);
}

/* === MAIN === */

int main(int argc, char *argv[])
{
    mztest_parse_args(argc, argv);
    mztest_init();

    UNITY_BEGIN();

    RUN_TEST(test_framework_works);
    RUN_TEST(test_emulator_state_initialized);
    RUN_TEST(test_video_interface_initialized);
    RUN_TEST(test_memory_initialized);
    RUN_TEST(test_level_filtering_smoke);
    RUN_TEST(test_level_filtering_full);

    int result = UNITY_END();

    mztest_teardown();
    return result;
}
