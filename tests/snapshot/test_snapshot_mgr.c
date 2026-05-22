/*
 * test_snapshot_mgr.c — unit testy pro snapshot manager
 *
 * Testuje: snapshot_register_component, snapshot_save, snapshot_load,
 *          snapshot_read_metadata, snapshot_result_to_string
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include <glib.h>
#include <string.h>

#include "emulator/snapshot/snapshot.h"
#include "emulator/snapshot/snapshot_mgr.h"
#include "emulator/emulator.h"
#include "hw-generic/memory/memory.h"

static const char *TMP_FILE = "tests/data/tmp/test_mgr.mzs";

void setUp(void) { }
void tearDown(void)
{
    remove(TMP_FILE);
}

/* ================================================================
 * SMOKE TESTY
 * ================================================================ */

/* snapshot_init registruje komponenty — ověřit, že registrace proběhla */
void test_mgr_components_registered(void)
{
    /* snapshot_init() se volá v mztest_init() —
     * ověříme, že se zaregistrovalo >0 komponent */
    TEST_ASSERT_GREATER_THAN(0, snapshot_mgr_get_component_count());
}

/* snapshot_result_to_string vrací nenulový string pro každý kód */
void test_mgr_result_to_string(void)
{
    TEST_ASSERT_NOT_NULL(snapshot_result_to_string(SNAPSHOT_OK));
    TEST_ASSERT_NOT_NULL(snapshot_result_to_string(SNAPSHOT_ERR_IO));
    TEST_ASSERT_NOT_NULL(snapshot_result_to_string(SNAPSHOT_ERR_ZIP));
    TEST_ASSERT_NOT_NULL(snapshot_result_to_string(SNAPSHOT_ERR_NOT_PAUSED));
}

/* ================================================================
 * UNIT TESTY
 * ================================================================ */

/* save + load roundtrip — základní test celého pipeline */
void test_mgr_save_load_roundtrip(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* zapsat vzor do RAM */
    for (int i = 0; i < 256; i++) {
        g_memory.RAM[0x1000 + i] = (uint8_t)i;
    }

    /* uložit zálohu */
    uint8_t backup[256];
    memcpy(backup, &g_memory.RAM[0x1000], 256);

    /* emulace musí být v pauze pro save/load */
    g_emulator.paused = true;

    /* save */
    en_SNAPSHOT_RESULT res = snapshot_save(TMP_FILE, "test roundtrip");
    TEST_ASSERT_EQUAL_INT_MESSAGE(SNAPSHOT_OK, res,
        snapshot_result_to_string(res));

    /* zničit data v RAM */
    memset(&g_memory.RAM[0x1000], 0xFF, 256);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_memory.RAM[0x1000]);

    /* load */
    res = snapshot_load(TMP_FILE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SNAPSHOT_OK, res,
        snapshot_result_to_string(res));

    /* ověřit obnovení */
    TEST_ASSERT_EQUAL_MEMORY(backup, &g_memory.RAM[0x1000], 256);

    g_emulator.paused = false;
}

/* save bez pauzy → ERR_NOT_PAUSED */
void test_mgr_save_requires_pause(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    g_emulator.paused = false;
    en_SNAPSHOT_RESULT res = snapshot_save(TMP_FILE, "should fail");
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_ERR_NOT_PAUSED, res);
}

/* load bez pauzy → ERR_NOT_PAUSED */
void test_mgr_load_requires_pause(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* nejprve vytvořit validní snapshot */
    g_emulator.paused = true;
    snapshot_save(TMP_FILE, "for load test");

    /* zkusit load bez pauzy */
    g_emulator.paused = false;
    en_SNAPSHOT_RESULT res = snapshot_load(TMP_FILE);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_ERR_NOT_PAUSED, res);
}

/* load neexistujícího souboru → chyba */
void test_mgr_load_nonexistent(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    g_emulator.paused = true;
    en_SNAPSHOT_RESULT res = snapshot_load("tests/data/tmp/neexistuje.mzs");
    TEST_ASSERT_NOT_EQUAL(SNAPSHOT_OK, res);
    g_emulator.paused = false;
}

/* metadata — přečtení metadat z uloženého snapshotu */
void test_mgr_read_metadata(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    g_emulator.paused = true;
    en_SNAPSHOT_RESULT res = snapshot_save(TMP_FILE, "metadata test popis");
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK, res);
    g_emulator.paused = false;

    st_SNAPSHOT_METADATA metadata;
    memset(&metadata, 0, sizeof(metadata));

    res = snapshot_read_metadata(TMP_FILE, &metadata);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK, res);

    /* ověřit metadata */
    TEST_ASSERT_EQUAL_STRING("metadata test popis", metadata.description);
    TEST_ASSERT_EQUAL_INT(MZARCH, metadata.architecture);
    TEST_ASSERT_EQUAL_INT(1, metadata.format_version);

    /* checksum nesmí být prázdný */
    TEST_ASSERT_GREATER_THAN(0, strlen(metadata.checksum));
}

/* metadata z neexistujícího souboru → chyba */
void test_mgr_read_metadata_nonexistent(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    st_SNAPSHOT_METADATA metadata;
    en_SNAPSHOT_RESULT res = snapshot_read_metadata("tests/data/tmp/neexistuje.mzs", &metadata);
    TEST_ASSERT_NOT_EQUAL(SNAPSHOT_OK, res);
}

/* vícenásobný save→load */
void test_mgr_multiple_save_load(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_FULL);

    g_emulator.paused = true;

    for (int round = 0; round < 5; round++) {
        /* zapsat unikátní vzor */
        uint8_t pattern = (uint8_t)(round * 37 + 11);
        for (int i = 0; i < 64; i++) {
            g_memory.RAM[0x2000 + i] = pattern;
        }

        TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK, snapshot_save(TMP_FILE, "multi"));

        /* zničit */
        memset(&g_memory.RAM[0x2000], 0, 64);

        /* obnovit */
        TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK, snapshot_load(TMP_FILE));

        /* ověřit */
        TEST_ASSERT_EQUAL_HEX8(pattern, g_memory.RAM[0x2000]);
        TEST_ASSERT_EQUAL_HEX8(pattern, g_memory.RAM[0x2000 + 63]);
    }

    g_emulator.paused = false;
}

/* === MAIN === */

int main(int argc, char *argv[])
{
    mztest_parse_args(argc, argv);
    mztest_init();

    UNITY_BEGIN();

    /* smoke */
    RUN_TEST(test_mgr_components_registered);
    RUN_TEST(test_mgr_result_to_string);

    /* unit */
    RUN_TEST(test_mgr_save_load_roundtrip);
    RUN_TEST(test_mgr_save_requires_pause);
    RUN_TEST(test_mgr_load_requires_pause);
    RUN_TEST(test_mgr_load_nonexistent);
    RUN_TEST(test_mgr_read_metadata);
    RUN_TEST(test_mgr_read_metadata_nonexistent);

    /* full */
    RUN_TEST(test_mgr_multiple_save_load);

    int result = UNITY_END();
    mztest_teardown();
    return result;
}
