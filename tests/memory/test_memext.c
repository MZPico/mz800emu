/*
 * test_memext.c - unit testy pro paměťové rozšíření Memext (PEHU + LUFTNER)
 *
 * Testuje:
 *   - connect / disconnect a propagaci do bus pointerů g_memory.memram_*
 *   - PEHU banking (memext_map_pwrite, párové 8KB stránky, maskování value)
 *   - LUFTNER banking (1B per stránka, 4KB granularita, RAM i FLASH banky)
 *   - R/W přes bus po banking (memory_read_byte / memory_write_byte)
 *   - WOM (Write-Only Memory) chování pro LUFTNER FLASH banky
 *   - memext_get_ram_offset_from_pointer - inverzní pointer mapování
 *
 * Testy operují pouze na oblasti 0x2000-0x7fff, kde není ROM overlay -
 * v této oblasti memory_read/write_byte jde přímo přes memram_read/write[]
 * a tedy přes Memext (když je připojen).
 *
 * PEHU bankuje po dvojicích addr_pointů (ap & 0xfe = páruje 4KB+4KB = 8KB).
 * LUFTNER bankuje po jednotlivých addr_pointech (4KB).
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include <glib.h>
#include <string.h>

#include "hw-generic/memory/memory.h"
#include "hw-generic/memory/memext.h"

/* setUp/tearDown - každý test začíná v deterministickém stavu:
 * Memext odpojen. Test sám si připojí potřebný typ (PEHU / LUFTNER).
 * tearDown opět odpojí - tím chráníme následující testy. */
void setUp(void)
{
    /* AUTO_INIT zapnutý = memext_connect(LUFTNER) volá memext_reset()
     * který nastaví map[i] = i (identity). Bez toho by zůstal random
     * map z memext_init_luftner() a banking testy by selhaly. */
    g_memext.init_luftner = MEMEXT_INIT_LUFTNER_RESET;
    memext_disconnect();
}

void tearDown(void)
{
    memext_disconnect();
}

/* ================================================================
 * SMOKE - connect / disconnect
 * ================================================================ */

void test_memext_connect_pehu(void)
{
    memext_connect(MEMEXT_TYPE_PEHU);
    TEST_ASSERT_TRUE(MEMEXT_TEST_CONNECTED);
    TEST_ASSERT_TRUE(MEMEXT_TEST_TYPE_PEHU);
    TEST_ASSERT_FALSE(MEMEXT_TEST_TYPE_LUFTNER);

    /* Po memext_connect / memext_reset je map identity. */
    for (int i = 0; i < MEMEXT_RAW_MAP_SIZE; i++) {
        TEST_ASSERT_EQUAL_UINT32((uint32_t)i, g_memext.map[i]);
    }
}

void test_memext_connect_luftner(void)
{
    memext_connect(MEMEXT_TYPE_LUFTNER);
    TEST_ASSERT_TRUE(MEMEXT_TEST_CONNECTED);
    TEST_ASSERT_TRUE(MEMEXT_TEST_TYPE_LUFTNER);
    TEST_ASSERT_FALSE(MEMEXT_TEST_TYPE_PEHU);

    /* AUTO_INIT (= MEMEXT_INIT_LUFTNER_RESET, default v setUp) zajišťuje,
     * že memext_reset přepíše random map z memext_init_luftner na
     * identity. */
    for (int i = 0; i < MEMEXT_RAW_MAP_SIZE; i++) {
        TEST_ASSERT_EQUAL_UINT32((uint32_t)i, g_memext.map[i]);
    }
}

void test_memext_disconnect(void)
{
    memext_connect(MEMEXT_TYPE_PEHU);
    TEST_ASSERT_TRUE(MEMEXT_TEST_CONNECTED);

    memext_disconnect();
    TEST_ASSERT_FALSE(MEMEXT_TEST_CONNECTED);

    /* Bus pointery musí ukazovat zpět na g_memory.RAM (= g_memory.RAM[i*4KB]). */
    for (int i = 0; i < 0x10; i++) {
        TEST_ASSERT_EQUAL_PTR(&g_memory.RAM[i << 12], g_memory.memram_read[i]);
        TEST_ASSERT_EQUAL_PTR(&g_memory.RAM[i << 12], g_memory.memram_write[i]);
    }
}

/* ================================================================
 * PEHU - banking
 * ================================================================ */

/* PEHU pwrite: ap & 0xfe = párový base, rawbank = (value & 0x3f) * 2.
 * map[base] = rawbank, map[base+1] = rawbank + 1. */
void test_pehu_pwrite_basic(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_PEHU);

    /* Bank 1 na addr_point 2 (= 0x2000 area). */
    memext_map_pwrite(2, 1);
    TEST_ASSERT_EQUAL_UINT32(2, g_memext.map[2]); /* rawbank = 1*2 = 2 */
    TEST_ASSERT_EQUAL_UINT32(3, g_memext.map[3]); /* rawbank + 1 */

    /* Ostatní addr_pointy zůstaly v identity. */
    TEST_ASSERT_EQUAL_UINT32(0, g_memext.map[0]);
    TEST_ASSERT_EQUAL_UINT32(4, g_memext.map[4]);
}

/* PEHU sudě/liché addr_point dají stejný výsledek (ap & 0xfe). */
void test_pehu_pwrite_addr_point_pair(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_PEHU);

    memext_map_pwrite(5, 3); /* ap=5 → base = 5 & 0xfe = 4 */
    TEST_ASSERT_EQUAL_UINT32(6, g_memext.map[4]); /* rawbank = 3*2 */
    TEST_ASSERT_EQUAL_UINT32(7, g_memext.map[5]);
}

/* PEHU value se maskuje na & 0x3f (= 6 bitů, 64 banků). */
void test_pehu_pwrite_masks_value(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_PEHU);

    /* value = 0x40 = mimo masku, & 0x3f = 0. */
    memext_map_pwrite(0, 0x40);
    TEST_ASSERT_EQUAL_UINT32(0, g_memext.map[0]);
    TEST_ASSERT_EQUAL_UINT32(1, g_memext.map[1]);

    /* value = 0xff & 0x3f = 0x3f = max bank 63. */
    memext_map_pwrite(0, 0xff);
    TEST_ASSERT_EQUAL_UINT32(0x3f * 2, g_memext.map[0]);
    TEST_ASSERT_EQUAL_UINT32(0x3f * 2 + 1, g_memext.map[1]);
}

/* PEHU pwrite musí přepočítat bus pointery (= memory_reconnect_ram). */
void test_pehu_pwrite_updates_bus(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_PEHU);

    /* Bank 2 na ap=2 → map[2] = 4, map[3] = 5. */
    memext_map_pwrite(2, 2);
    TEST_ASSERT_EQUAL_PTR(&g_memext.RAM[4 * MEMEXT_RAW_BANK_SIZE], g_memory.memram_read[2]);
    TEST_ASSERT_EQUAL_PTR(&g_memext.RAM[5 * MEMEXT_RAW_BANK_SIZE], g_memory.memram_read[3]);
    TEST_ASSERT_EQUAL_PTR(&g_memext.RAM[4 * MEMEXT_RAW_BANK_SIZE], g_memory.memram_write[2]);
    TEST_ASSERT_EQUAL_PTR(&g_memext.RAM[5 * MEMEXT_RAW_BANK_SIZE], g_memory.memram_write[3]);
}

/* PEHU R/W přes bus: zápis do banku N přes addr 0x2000+ skončí v g_memext.RAM
 * na offsetu odpovídajícím rawbank. */
void test_pehu_bus_read_write(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_PEHU);

    /* PEHU bank 3 na ap=2 → 0x2000-0x3fff mapuje na g_memext.RAM[0x6000..0x7fff].
     * rawbank = 3*2 = 6 → map[2]=6 → RAM[6*0x1000] = RAM[0x6000]. */
    memext_map_pwrite(2, 3);

    /* Zápis přes bus na 0x2050 → g_memext.RAM[0x6050]. */
    memory_write_byte(0x2050, 0xAB);
    TEST_ASSERT_EQUAL_HEX8(0xAB, g_memext.RAM[0x6050]);

    /* Čtení přes bus z 0x2050 vrátí stejnou hodnotu. */
    TEST_ASSERT_EQUAL_HEX8(0xAB, memory_read_byte(0x2050));

    /* Zápis do druhé poloviny páru (0x3000-0x3fff) jde do RAM[0x7000..0x7fff]. */
    memory_write_byte(0x3010, 0xCD);
    TEST_ASSERT_EQUAL_HEX8(0xCD, g_memext.RAM[0x7010]);
}

/* PEHU bank switch: data v původním banku přežijí přepnutí. */
void test_pehu_bank_switch_preserves_data(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_PEHU);

    /* Bank 1 na ap=2 → RAM[0x2000..0x3fff]. */
    memext_map_pwrite(2, 1);
    memory_write_byte(0x2100, 0x11);

    /* Přepnout na bank 2 → RAM[0x4000..0x5fff]. */
    memext_map_pwrite(2, 2);
    memory_write_byte(0x2100, 0x22);

    /* Bank 1 obsahuje stále 0x11. */
    TEST_ASSERT_EQUAL_HEX8(0x11, g_memext.RAM[0x2100]);
    /* Bank 2 obsahuje 0x22. */
    TEST_ASSERT_EQUAL_HEX8(0x22, g_memext.RAM[0x4100]);

    /* Přepnout zpět na bank 1 a přečíst přes bus. */
    memext_map_pwrite(2, 1);
    TEST_ASSERT_EQUAL_HEX8(0x11, memory_read_byte(0x2100));
}

/* ================================================================
 * LUFTNER - banking RAM
 * ================================================================ */

/* LUFTNER pwrite: map[ap] = value (1B = 1 bank, 4KB granularita). */
void test_luftner_pwrite_single(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_LUFTNER);

    memext_map_pwrite(5, 7);
    TEST_ASSERT_EQUAL_UINT32(7, g_memext.map[5]);

    /* Sousední map zůstaly v identity. */
    TEST_ASSERT_EQUAL_UINT32(4, g_memext.map[4]);
    TEST_ASSERT_EQUAL_UINT32(6, g_memext.map[6]);
}

/* LUFTNER bere celý byte value bez maskování (na rozdíl od PEHU). */
void test_luftner_pwrite_full_byte(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_LUFTNER);

    /* value = 0x7f = max RAM bank (bit 0x80 = FLASH). */
    memext_map_pwrite(2, 0x7f);
    TEST_ASSERT_EQUAL_UINT32(0x7f, g_memext.map[2]);
    TEST_ASSERT_EQUAL_PTR(&g_memext.RAM[0x7f * MEMEXT_RAW_BANK_SIZE], g_memory.memram_read[2]);
}

/* LUFTNER R/W přes bus do RAM banku. */
void test_luftner_ram_bank_rw(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_LUFTNER);

    /* ap=5 → bank 9 → RAM[9 * 0x1000] = RAM[0x9000]. */
    memext_map_pwrite(5, 9);
    memory_write_byte(0x5100, 0x77);
    TEST_ASSERT_EQUAL_HEX8(0x77, g_memext.RAM[0x9100]);
    TEST_ASSERT_EQUAL_HEX8(0x77, memory_read_byte(0x5100));
}

/* ================================================================
 * LUFTNER - FLASH banky + WOM
 * ================================================================ */

/* FLASH bank: rawbank & 0x80 → read z g_memext.FLASH, write do WOM. */
void test_luftner_flash_read(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_LUFTNER);

    /* Předem nasypat hodnotu do FLASH bank 0. */
    g_memext.FLASH[0x123] = 0x5A;

    /* ap=3 → bank 0x80 (FLASH bank 0). map[3] = 0x80. */
    memext_map_pwrite(3, 0x80);
    TEST_ASSERT_EQUAL_UINT32(0x80, g_memext.map[3]);

    /* Read pointer ukazuje do FLASH. */
    TEST_ASSERT_EQUAL_PTR(&g_memext.FLASH[0], g_memory.memram_read[3]);

    /* Čtení přes bus vrátí FLASH hodnotu. */
    TEST_ASSERT_EQUAL_HEX8(0x5A, memory_read_byte(0x3123));
}

/* FLASH bank: zápis přes bus jde do WOM (Write-Only Memory), FLASH se nezmění. */
void test_luftner_wom_write(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_LUFTNER);

    /* Připravit FLASH a vynulovat WOM. */
    g_memext.FLASH[0x200] = 0xC3;
    memset(g_memext.WOM, 0x00, sizeof(g_memext.WOM));

    /* Zmapovat FLASH bank 0 na ap=3. */
    memext_map_pwrite(3, 0x80);

    /* Write pointer ukazuje do WOM. */
    TEST_ASSERT_EQUAL_PTR(&g_memext.WOM[0], g_memory.memram_write[3]);

    /* Zápis přes bus na 0x3200. */
    memory_write_byte(0x3200, 0xE7);

    /* FLASH zůstala beze změny. */
    TEST_ASSERT_EQUAL_HEX8(0xC3, g_memext.FLASH[0x200]);
    /* WOM dostala zápis. */
    TEST_ASSERT_EQUAL_HEX8(0xE7, g_memext.WOM[0x200]);
    /* Čtení přes bus stále vrací FLASH hodnotu. */
    TEST_ASSERT_EQUAL_HEX8(0xC3, memory_read_byte(0x3200));
}

/* LUFTNER FLASH bank N: read pointer = FLASH[(N & 0x7f) * 0x1000]. */
void test_luftner_flash_bank_indexing(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    memext_connect(MEMEXT_TYPE_LUFTNER);

    /* FLASH bank 5 = rawbank 0x85 → FLASH[5 * 0x1000] = FLASH[0x5000]. */
    g_memext.FLASH[0x5050] = 0xBE;
    memext_map_pwrite(4, 0x85);
    TEST_ASSERT_EQUAL_PTR(&g_memext.FLASH[0x5000], g_memory.memram_read[4]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, memory_read_byte(0x4050));
}

/* ================================================================
 * memext_get_ram_offset_from_pointer - inverzní pointer mapování
 * ================================================================ */

void test_offset_null_returns_minus_one(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    TEST_ASSERT_EQUAL_INT32(-1, memext_get_ram_offset_from_pointer(NULL));
}

void test_offset_in_ram_returns_offset(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* Začátek RAM. */
    TEST_ASSERT_EQUAL_INT32(0, memext_get_ram_offset_from_pointer(&g_memext.RAM[0]));
    /* Vnitřek. */
    TEST_ASSERT_EQUAL_INT32(0x12345, memext_get_ram_offset_from_pointer(&g_memext.RAM[0x12345]));
    /* Poslední validní byte. */
    TEST_ASSERT_EQUAL_INT32(MEMEXT_RAM_SIZE - 1,
                            memext_get_ram_offset_from_pointer(&g_memext.RAM[MEMEXT_RAM_SIZE - 1]));
}

/* Pointer mimo RAM (FLASH) → -1. Předpoklad: FLASH a RAM jsou v jiných
 * paměťových regionech (různá pole ve struct), pointer arith přes
 * uintptr_t to korektně rozliší. */
void test_offset_out_of_ram_returns_minus_one(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* WOM je samostatné pole - musí být mimo RAM range. */
    TEST_ASSERT_EQUAL_INT32(-1, memext_get_ram_offset_from_pointer(&g_memext.WOM[0]));

    /* Adresa hned za koncem RAM. */
    TEST_ASSERT_EQUAL_INT32(-1,
                            memext_get_ram_offset_from_pointer(&g_memext.RAM[0] + MEMEXT_RAM_SIZE));
}

/* === MAIN === */

int main(int argc, char *argv[])
{
    mztest_parse_args(argc, argv);
    mztest_init();

    UNITY_BEGIN();

    /* smoke - connect / disconnect */
    RUN_TEST(test_memext_connect_pehu);
    RUN_TEST(test_memext_connect_luftner);
    RUN_TEST(test_memext_disconnect);

    /* unit - PEHU */
    RUN_TEST(test_pehu_pwrite_basic);
    RUN_TEST(test_pehu_pwrite_addr_point_pair);
    RUN_TEST(test_pehu_pwrite_masks_value);
    RUN_TEST(test_pehu_pwrite_updates_bus);
    RUN_TEST(test_pehu_bus_read_write);
    RUN_TEST(test_pehu_bank_switch_preserves_data);

    /* unit - LUFTNER RAM */
    RUN_TEST(test_luftner_pwrite_single);
    RUN_TEST(test_luftner_pwrite_full_byte);
    RUN_TEST(test_luftner_ram_bank_rw);

    /* unit - LUFTNER FLASH + WOM */
    RUN_TEST(test_luftner_flash_read);
    RUN_TEST(test_luftner_wom_write);
    RUN_TEST(test_luftner_flash_bank_indexing);

    /* unit - offset_from_pointer */
    RUN_TEST(test_offset_null_returns_minus_one);
    RUN_TEST(test_offset_in_ram_returns_offset);
    RUN_TEST(test_offset_out_of_ram_returns_minus_one);

    int result = UNITY_END();
    mztest_teardown();
    return result;
}
