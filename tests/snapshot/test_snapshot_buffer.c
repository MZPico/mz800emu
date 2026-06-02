/*
 * test_snapshot_buffer.c - unit testy pro buffer-based snapshot API
 *
 * Testuje:
 *   snapshot_io_open_write_buffer / open_read_buffer / close_to_buffer
 *   snapshot_save_to_buffer / snapshot_load_from_buffer
 *
 * Pokrývá:
 *   - low-level io: roundtrip přes paměť (write_bin → read_bin)
 *   - high-level: save_to_buffer + load_from_buffer obnoví stav RAM
 *   - cross-compat: snapshot uložený do bufferu lze loadnout přes file API
 *     a opačně
 *   - edge cases: NULL/size=0 vstup, korupce dat, close_to_buffer na
 *     non-buffer handle
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emulator/snapshot/snapshot.h"
#include "emulator/snapshot/snapshot_io.h"
#include "emulator/snapshot/snapshot_mgr.h"
#include "emulator/emulator.h"
#include "hw-generic/memory/memory.h"

static const char *TMP_FILE = "tests/data/tmp/test_buffer.mzs";

void setUp(void) { }
void tearDown(void)
{
    remove(TMP_FILE);
}

/* ================================================================
 * SMOKE TESTY (low-level snapshot_io)
 * ================================================================ */

/* otevření a zavření prázdného buffer-writeru produkuje neprázdný buffer */
void test_buf_io_create_empty_archive(void)
{
    snapshot_io_t *io = snapshot_io_open_write_buffer(6);
    TEST_ASSERT_NOT_NULL_MESSAGE(io,
        "snapshot_io_open_write_buffer returned NULL");

    uint8_t *data = NULL;
    size_t size = 0;
    en_SNAPSHOT_RESULT res = snapshot_io_close_to_buffer(io, &data, &size);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK, res);
    TEST_ASSERT_NOT_NULL(data);
    /* prázdný ZIP archiv (jen EOCD) má 22 bajtů */
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(22, size);
    g_free(data);
}

/* otevření čtečky z NULL bufferu nebo velikosti 0 → NULL */
void test_buf_io_open_read_null_or_empty(void)
{
    TEST_ASSERT_NULL(snapshot_io_open_read_buffer(NULL, 100));

    uint8_t dummy = 0;
    TEST_ASSERT_NULL(snapshot_io_open_read_buffer(&dummy, 0));
}

/* otevření čtečky nad náhodnými daty → NULL (zip reader detekuje korupci) */
void test_buf_io_open_read_corrupted(void)
{
    /* random bytes, určitě ne validní ZIP */
    uint8_t junk[256];
    for (int i = 0; i < 256; i++) junk[i] = (uint8_t)(i ^ 0x5A);

    snapshot_io_t *io = snapshot_io_open_read_buffer(junk, sizeof(junk));
    TEST_ASSERT_NULL_MESSAGE(io,
        "open_read_buffer should reject non-ZIP data");
}

/* ================================================================
 * UNIT TESTY (low-level snapshot_io)
 * ================================================================ */

/* roundtrip write_bin → buffer → open_read_buffer → read_bin */
void test_buf_io_roundtrip_small(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x42, 0x13, 0x37 };

    /* zápis */
    snapshot_io_t *w = snapshot_io_open_write_buffer(6);
    TEST_ASSERT_NOT_NULL(w);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK,
        snapshot_io_write_bin(w, "data/payload.bin", payload, sizeof(payload)));

    uint8_t *zip = NULL;
    size_t zip_size = 0;
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK,
        snapshot_io_close_to_buffer(w, &zip, &zip_size));
    TEST_ASSERT_NOT_NULL(zip);
    TEST_ASSERT_GREATER_THAN(0, zip_size);

    /* čtení */
    snapshot_io_t *r = snapshot_io_open_read_buffer(zip, zip_size);
    TEST_ASSERT_NOT_NULL(r);

    uint8_t *read_data = NULL;
    size_t read_size = 0;
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK,
        snapshot_io_read_bin(r, "data/payload.bin", &read_data, &read_size));
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), read_size);
    TEST_ASSERT_EQUAL_MEMORY(payload, read_data, sizeof(payload));

    g_free(read_data);
    snapshot_io_close(r);
    g_free(zip);
}

/* close_to_buffer na file-based handle → ERR_IO, žádný leak */
void test_buf_io_close_to_buffer_on_file_handle(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    snapshot_io_t *io = snapshot_io_open_write(TMP_FILE, 6);
    TEST_ASSERT_NOT_NULL(io);

    uint8_t *data = (uint8_t *)0xDEADBEEF;  /* sentinel - musí být přepsán na NULL */
    size_t size = 12345;

    en_SNAPSHOT_RESULT res = snapshot_io_close_to_buffer(io, &data, &size);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_ERR_IO, res);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_size_t(0, size);
    /* handle už nesmí být použit - close_to_buffer jej uvolní */
}

/* ================================================================
 * UNIT TESTY (high-level snapshot_save_to_buffer / load_from_buffer)
 * ================================================================ */

/* roundtrip save_to_buffer → load_from_buffer obnoví stav RAM */
void test_buf_save_load_roundtrip(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* zapsat rozpoznatelný vzor do RAM */
    for (int i = 0; i < 256; i++) {
        g_memory.RAM[0x3000 + i] = (uint8_t)((i * 31) & 0xFF);
    }
    uint8_t backup[256];
    memcpy(backup, &g_memory.RAM[0x3000], 256);

    g_emulator.paused = true;

    /* save do bufferu */
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    en_SNAPSHOT_RESULT res = snapshot_save_to_buffer("test buffer roundtrip",
                                                     &buf, &buf_size);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SNAPSHOT_OK, res,
        snapshot_result_to_string(res));
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_GREATER_THAN(0, buf_size);

    /* zničit data v RAM */
    memset(&g_memory.RAM[0x3000], 0xA5, 256);
    TEST_ASSERT_EQUAL_HEX8(0xA5, g_memory.RAM[0x3000]);

    /* load z bufferu */
    res = snapshot_load_from_buffer(buf, buf_size);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SNAPSHOT_OK, res,
        snapshot_result_to_string(res));

    /* ověřit obnovení */
    TEST_ASSERT_EQUAL_MEMORY(backup, &g_memory.RAM[0x3000], 256);

    g_free(buf);
    g_emulator.paused = false;
}

/* save bez pauzy → ERR_NOT_PAUSED, out_data zůstává NULL */
void test_buf_save_requires_pause(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    g_emulator.paused = false;

    uint8_t *buf = (uint8_t *)0xCAFEBABE;
    size_t size = 999;
    en_SNAPSHOT_RESULT res = snapshot_save_to_buffer("nope", &buf, &size);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_ERR_NOT_PAUSED, res);
    TEST_ASSERT_NULL(buf);
    TEST_ASSERT_EQUAL_size_t(0, size);
}

/* save_to_buffer s NULL out_data → ERR_IO */
void test_buf_save_null_outputs(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    g_emulator.paused = true;

    size_t size = 0;
    en_SNAPSHOT_RESULT res = snapshot_save_to_buffer("x", NULL, &size);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_ERR_IO, res);

    uint8_t *buf = NULL;
    res = snapshot_save_to_buffer("x", &buf, NULL);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_ERR_IO, res);

    g_emulator.paused = false;
}

/* load_from_buffer bez pauzy → ERR_NOT_PAUSED */
void test_buf_load_requires_pause(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* nejdřív vyrobit platný buffer */
    g_emulator.paused = true;
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK,
        snapshot_save_to_buffer("for load test", &buf, &buf_size));
    g_emulator.paused = false;

    en_SNAPSHOT_RESULT res = snapshot_load_from_buffer(buf, buf_size);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_ERR_NOT_PAUSED, res);

    g_free(buf);
}

/* load_from_buffer s NULL/size=0 → ERR_IO */
void test_buf_load_null_or_empty(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    g_emulator.paused = true;

    en_SNAPSHOT_RESULT res = snapshot_load_from_buffer(NULL, 100);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_ERR_IO, res);

    uint8_t dummy = 0;
    res = snapshot_load_from_buffer(&dummy, 0);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_ERR_IO, res);

    g_emulator.paused = false;
}

/* load_from_buffer s korupčními daty → ERR_ZIP */
void test_buf_load_corrupted(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    g_emulator.paused = true;

    uint8_t junk[512];
    for (int i = 0; i < 512; i++) junk[i] = (uint8_t)(i ^ 0x33);

    en_SNAPSHOT_RESULT res = snapshot_load_from_buffer(junk, sizeof(junk));
    TEST_ASSERT_EQUAL_INT_MESSAGE(SNAPSHOT_ERR_ZIP, res,
        "Corrupted input should return SNAPSHOT_ERR_ZIP");

    g_emulator.paused = false;
}

/* ================================================================
 * FULL TESTY - cross-compat mezi file a buffer API
 * ================================================================ */

/* buffer → file: uložit přes save_to_buffer, zapsat na disk, načíst přes
 * snapshot_load. Identický formát = musí fungovat. */
void test_buf_crosscompat_buffer_to_file(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_FULL);

    for (int i = 0; i < 128; i++) {
        g_memory.RAM[0x4000 + i] = (uint8_t)(0xC0 + i);
    }
    uint8_t backup[128];
    memcpy(backup, &g_memory.RAM[0x4000], 128);

    g_emulator.paused = true;

    /* 1. uložit do bufferu */
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK,
        snapshot_save_to_buffer("crosscompat buffer→file", &buf, &buf_size));

    /* 2. zapsat buffer 1:1 na disk */
    FILE *fp = fopen(TMP_FILE, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_size_t(buf_size, fwrite(buf, 1, buf_size, fp));
    fclose(fp);

    /* 3. zničit RAM */
    memset(&g_memory.RAM[0x4000], 0x77, 128);

    /* 4. načíst přes file API - musí projít beze změny checksumu */
    en_SNAPSHOT_RESULT res = snapshot_load(TMP_FILE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SNAPSHOT_OK, res,
        snapshot_result_to_string(res));

    /* 5. ověřit obnovení */
    TEST_ASSERT_EQUAL_MEMORY(backup, &g_memory.RAM[0x4000], 128);

    g_free(buf);
    g_emulator.paused = false;
}

/* file → buffer: uložit přes snapshot_save, načíst soubor do paměti,
 * loadnout přes load_from_buffer */
void test_buf_crosscompat_file_to_buffer(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_FULL);

    for (int i = 0; i < 128; i++) {
        g_memory.RAM[0x5000 + i] = (uint8_t)(0xE0 + i);
    }
    uint8_t backup[128];
    memcpy(backup, &g_memory.RAM[0x5000], 128);

    g_emulator.paused = true;

    /* 1. save do souboru */
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_OK,
        snapshot_save(TMP_FILE, "crosscompat file→buffer"));

    /* 2. načíst celý soubor do paměti */
    FILE *fp = fopen(TMP_FILE, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    TEST_ASSERT_GREATER_THAN(0, fsize);

    uint8_t *blob = g_malloc((size_t)fsize);
    TEST_ASSERT_EQUAL_size_t((size_t)fsize, fread(blob, 1, fsize, fp));
    fclose(fp);

    /* 3. zničit RAM */
    memset(&g_memory.RAM[0x5000], 0x88, 128);

    /* 4. načíst přes buffer API */
    en_SNAPSHOT_RESULT res = snapshot_load_from_buffer(blob, (size_t)fsize);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SNAPSHOT_OK, res,
        snapshot_result_to_string(res));

    /* 5. ověřit */
    TEST_ASSERT_EQUAL_MEMORY(backup, &g_memory.RAM[0x5000], 128);

    g_free(blob);
    g_emulator.paused = false;
}

/* === MAIN === */

int main(int argc, char *argv[])
{
    mztest_parse_args(argc, argv);
    mztest_init();

    UNITY_BEGIN();

    /* smoke - low-level io */
    RUN_TEST(test_buf_io_create_empty_archive);
    RUN_TEST(test_buf_io_open_read_null_or_empty);
    RUN_TEST(test_buf_io_open_read_corrupted);

    /* unit - low-level io */
    RUN_TEST(test_buf_io_roundtrip_small);
    RUN_TEST(test_buf_io_close_to_buffer_on_file_handle);

    /* unit - high-level save/load */
    RUN_TEST(test_buf_save_load_roundtrip);
    RUN_TEST(test_buf_save_requires_pause);
    RUN_TEST(test_buf_save_null_outputs);
    RUN_TEST(test_buf_load_requires_pause);
    RUN_TEST(test_buf_load_null_or_empty);
    RUN_TEST(test_buf_load_corrupted);

    /* full - cross-compat */
    RUN_TEST(test_buf_crosscompat_buffer_to_file);
    RUN_TEST(test_buf_crosscompat_file_to_buffer);

    int result = UNITY_END();
    mztest_teardown();
    return result;
}
