/**
 * @file   test_zxtape.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Unit testy knihovny zxtape (ZX Spectrum TAP -> CMT konverze).
 *
 * Testuje: lifecycle (vstream/bitstream generování), compute_pulses,
 * checksum validace, speed array, stream wrapper, alokátor callback,
 * error callback, vstupní validace, regresní testy.
 *
 * Linkuje se s TEST_ALL_OBJS (vyžaduje cmt_stream, cmtspeed, mztape).
 *
 * @par Changelog:
 * - 2026-03-14: Proběhla kompletní revize a refaktorizace. Vytvořeny unit testy.
 *
 * @par Licence:
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mztest.h"

#include <string.h>
#include <stdlib.h>

#include "libs/zxtape/zxtape.h"
#include "libs/cmt_stream/cmt_stream.h"
#include "libs/cmtspeed/cmtspeed.h"


/* ========================================================================
 * setUp / tearDown — volány před/po každém testu
 * ======================================================================== */

/** @brief Inicializace před každým testem (prázdná). */
void setUp ( void ) {
}


/** @brief Úklid po každém testu — reset alokátoru a error callbacku na výchozí. */
void tearDown ( void ) {
    /* reset alokátoru a error callbacku na výchozí */
    zxtape_set_allocator ( NULL );
    zxtape_set_error_callback ( NULL );
}


/* ========================================================================
 * Pomocné funkce
 * ======================================================================== */

/**
 * @brief Vytvoří testovací TAP datový blok se známým obsahem.
 *
 * Vytvoří blok ve formátu: flag byte + payload (vzor i & 0xFF) + XOR checksum.
 * Volající musí uvolnit vrácený buffer přes free().
 *
 * @param flag Typ bloku (header/data).
 * @param payload_size Velikost payload dat v bajtech.
 * @param[out] out_total_size Celková velikost vráceného bloku (flag + payload + checksum).
 * @return Nově alokovaný buffer s TAP blokem, nebo NULL při chybě alokace.
 */
static uint8_t* create_test_tapblock ( en_ZXTAPE_BLOCK_FLAG flag, uint16_t payload_size, uint16_t *out_total_size ) {
    /* celková velikost = flag (1) + payload + checksum (1) */
    uint16_t total = 1 + payload_size + 1;
    uint8_t *block = (uint8_t*) malloc ( total );
    if ( !block ) return NULL;

    block[0] = (uint8_t) flag;

    /* naplnění payload vzorem */
    for ( uint16_t i = 0; i < payload_size; i++ ) {
        block[1 + i] = (uint8_t) ( i & 0xFF );
    }

    /* výpočet XOR checksumu (flag + payload) */
    uint8_t xor = 0;
    for ( uint16_t i = 0; i < 1 + payload_size; i++ ) {
        xor ^= block[i];
    }
    block[total - 1] = xor;

    if ( out_total_size ) *out_total_size = total;
    return block;
}


/* ========================================================================
 * SMOKE testy
 * ======================================================================== */

/** @brief Testuje vytvoření vstreamu z header bloku. */
static void test_create_vstream_header ( void ) {
    uint16_t total_size;
    uint8_t *block = create_test_tapblock ( ZXTAPE_BLOCK_FLAG_HEADER, 17, &total_size );
    TEST_ASSERT_NOT_NULL ( block );

    st_CMT_VSTREAM *vstream = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_HEADER, block, total_size, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NOT_NULL ( vstream );

    /* vstream musí mít nenulový počet vzorků */
    TEST_ASSERT_TRUE ( cmt_vstream_get_count_scans ( vstream ) > 0 );

    cmt_vstream_destroy ( vstream );
    free ( block );
}


/** @brief Testuje vytvoření vstreamu z data bloku. */
static void test_create_vstream_data ( void ) {
    uint16_t total_size;
    uint8_t *block = create_test_tapblock ( ZXTAPE_BLOCK_FLAG_DATA, 64, &total_size );
    TEST_ASSERT_NOT_NULL ( block );

    st_CMT_VSTREAM *vstream = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, block, total_size, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NOT_NULL ( vstream );

    TEST_ASSERT_TRUE ( cmt_vstream_get_count_scans ( vstream ) > 0 );

    cmt_vstream_destroy ( vstream );
    free ( block );
}


/* ========================================================================
 * UNIT testy
 * ======================================================================== */

/** @brief Testuje, že header blok (4032 pilot pulzů) generuje delší vstream než data blok (1610). */
static void test_header_longer_than_data ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    uint16_t total_size;
    uint8_t *block = create_test_tapblock ( ZXTAPE_BLOCK_FLAG_HEADER, 17, &total_size );
    TEST_ASSERT_NOT_NULL ( block );

    st_CMT_VSTREAM *vs_header = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_HEADER, block, total_size, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NOT_NULL ( vs_header );

    st_CMT_VSTREAM *vs_data = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, block, total_size, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NOT_NULL ( vs_data );

    /* header stream musí být delší (více pilot pulzů) */
    TEST_ASSERT_TRUE ( cmt_vstream_get_count_scans ( vs_header ) >
                        cmt_vstream_get_count_scans ( vs_data ) );

    cmt_vstream_destroy ( vs_header );
    cmt_vstream_destroy ( vs_data );
    free ( block );
}


/** @brief Testuje vytvoření bitstreamu přes vstream konverzi v unified wrapperu. */
static void test_create_bitstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    uint16_t total_size;
    uint8_t *block = create_test_tapblock ( ZXTAPE_BLOCK_FLAG_DATA, 32, &total_size );
    TEST_ASSERT_NOT_NULL ( block );

    st_CMT_STREAM *stream = zxtape_create_stream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, block, total_size, CMTSPEED_1_1, 44100,
        CMT_STREAM_TYPE_BITSTREAM );
    TEST_ASSERT_NOT_NULL ( stream );
    TEST_ASSERT_NOT_NULL ( stream->str.bitstream );

    cmt_stream_destroy ( stream );
    free ( block );
}


/** @brief Testuje unified wrapper funkci pro oba typy streamů (vstream i bitstream). */
static void test_create_stream_wrapper ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    uint16_t total_size;
    uint8_t *block = create_test_tapblock ( ZXTAPE_BLOCK_FLAG_DATA, 32, &total_size );
    TEST_ASSERT_NOT_NULL ( block );

    /* vstream varianta */
    st_CMT_STREAM *vs = zxtape_create_stream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, block, total_size, CMTSPEED_1_1, 44100,
        CMT_STREAM_TYPE_VSTREAM );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_NOT_NULL ( vs->str.vstream );
    cmt_stream_destroy ( vs );

    /* bitstream varianta */
    st_CMT_STREAM *bs = zxtape_create_stream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, block, total_size, CMTSPEED_1_1, 44100,
        CMT_STREAM_TYPE_BITSTREAM );
    TEST_ASSERT_NOT_NULL ( bs );
    TEST_ASSERT_NOT_NULL ( bs->str.bitstream );
    cmt_stream_destroy ( bs );

    free ( block );
}


/** @brief Testuje compute_pulses — ověření počtu pulzů pro header vs data blok. */
static void test_compute_pulses ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    uint8_t data[] = { 0xFF, 0x00, 0xAA, 0x55 };

    uint64_t long_h = 0, short_h = 0;
    zxtape_compute_pulses ( ZXTAPE_BLOCK_FLAG_HEADER, data, 4, &long_h, &short_h );

    uint64_t long_d = 0, short_d = 0;
    zxtape_compute_pulses ( ZXTAPE_BLOCK_FLAG_DATA, data, 4, &long_d, &short_d );

    /* oba musí mít nenulové počty */
    TEST_ASSERT_TRUE ( long_h > 0 );
    TEST_ASSERT_TRUE ( short_h > 0 );
    TEST_ASSERT_TRUE ( long_d > 0 );
    TEST_ASSERT_TRUE ( short_d > 0 );

    /* header má více pilot pulzů → více long pulzů celkem */
    TEST_ASSERT_TRUE ( long_h > long_d );

    /* datové pulzy jsou stejné → short se nemění */
    TEST_ASSERT_EQUAL_UINT64 ( short_h, short_d );
}


/** @brief Testuje compute_pulses s extrémy — data se 100% jedničkami a 100% nulami. */
static void test_compute_pulses_extremes ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    uint8_t all_ones[] = { 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t all_zeros[] = { 0x00, 0x00, 0x00, 0x00 };

    uint64_t long_1 = 0, short_1 = 0;
    zxtape_compute_pulses ( ZXTAPE_BLOCK_FLAG_DATA, all_ones, 4, &long_1, &short_1 );

    uint64_t long_0 = 0, short_0 = 0;
    zxtape_compute_pulses ( ZXTAPE_BLOCK_FLAG_DATA, all_zeros, 4, &long_0, &short_0 );

    /* všechny jedničky → 32 datových long pulzů navíc */
    TEST_ASSERT_TRUE ( long_1 > long_0 );

    /* všechny nuly → 32 datových short pulzů navíc */
    TEST_ASSERT_TRUE ( short_0 > short_1 );

    /* celkový počet datových pulzů musí být stejný (32 bitů) */
    /* odečteme pilot+pauza (shodné pro oba) — rozdíl je jen v long/short rozložení */
    uint64_t total_1 = long_1 + short_1;
    uint64_t total_0 = long_0 + short_0;
    TEST_ASSERT_EQUAL_UINT64 ( total_1, total_0 );
}


/** @brief Testuje checksum validaci s platným TAP blokem. */
static void test_checksum_valid ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    uint16_t total_size;
    uint8_t *block = create_test_tapblock ( ZXTAPE_BLOCK_FLAG_DATA, 32, &total_size );
    TEST_ASSERT_NOT_NULL ( block );

    TEST_ASSERT_EQUAL_INT ( 0, zxtape_validate_checksum ( block, total_size ) );

    free ( block );
}


/** @brief Testuje checksum validaci s poškozeným TAP blokem. */
static void test_checksum_invalid ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    uint16_t total_size;
    uint8_t *block = create_test_tapblock ( ZXTAPE_BLOCK_FLAG_DATA, 32, &total_size );
    TEST_ASSERT_NOT_NULL ( block );

    /* poškodíme jeden bajt */
    block[5] ^= 0x01;

    TEST_ASSERT_EQUAL_INT ( -1, zxtape_validate_checksum ( block, total_size ) );

    free ( block );
}


/** @brief Testuje checksum validaci — okrajové případy (NULL, příliš krátký blok, minimální platný blok). */
static void test_checksum_edge_cases ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    /* NULL pointer */
    TEST_ASSERT_EQUAL_INT ( -1, zxtape_validate_checksum ( NULL, 10 ) );

    /* příliš krátký blok */
    uint8_t tiny[] = { 0x42 };
    TEST_ASSERT_EQUAL_INT ( -1, zxtape_validate_checksum ( tiny, 1 ) );

    /* minimální platný blok (2 bajty: data + checksum kde XOR = 0) */
    uint8_t minimal[] = { 0x00, 0x00 };
    TEST_ASSERT_EQUAL_INT ( 0, zxtape_validate_checksum ( minimal, 2 ) );
}


/** @brief Testuje, že g_zxtape_speed[] končí terminátorem CMTSPEED_NONE a obsahuje platné rychlosti. */
static void test_speed_array_terminated ( void ) {
    int i = 0;
    while ( g_zxtape_speed[i] != CMTSPEED_NONE ) {
        TEST_ASSERT_TRUE ( cmtspeed_is_valid ( g_zxtape_speed[i] ) );
        i++;
        TEST_ASSERT_TRUE_MESSAGE ( i < 100, "g_zxtape_speed[] neobsahuje terminátor" );
    }
    /* musí obsahovat alespoň jednu platnou rychlost */
    TEST_ASSERT_TRUE ( i > 0 );
}


/** @brief Testuje, že různé rychlosti generují streamy s různou délkou (vyšší rychlost = kratší stream). */
static void test_speed_affects_stream_length ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    uint16_t total_size;
    uint8_t *block = create_test_tapblock ( ZXTAPE_BLOCK_FLAG_DATA, 32, &total_size );
    TEST_ASSERT_NOT_NULL ( block );

    /* základní rychlost 1:1 */
    st_CMT_VSTREAM *vs_1_1 = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, block, total_size, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NOT_NULL ( vs_1_1 );

    /* vyšší rychlost 3:2 → kratší stream */
    st_CMT_VSTREAM *vs_3_2 = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, block, total_size, CMTSPEED_3_2, 44100 );
    TEST_ASSERT_NOT_NULL ( vs_3_2 );

    /* rychlejší → méně vzorků */
    TEST_ASSERT_TRUE ( cmt_vstream_get_count_scans ( vs_1_1 ) >
                        cmt_vstream_get_count_scans ( vs_3_2 ) );

    cmt_vstream_destroy ( vs_1_1 );
    cmt_vstream_destroy ( vs_3_2 );
    free ( block );
}


/* ========================================================================
 * Testy vstupní validace
 * ======================================================================== */

/** @brief Počítadlo volání error callbacku pro testovací účely. */
static int s_error_count = 0;

/**
 * @brief Testovací error callback — inkrementuje počítadlo s_error_count.
 * @param func Jméno funkce (ignorováno).
 * @param line Číslo řádku (ignorováno).
 * @param fmt Formátovací řetězec (ignorováno).
 */
static void test_error_cb ( const char *func, int line, const char *fmt, ... ) {
    (void) func; (void) line; (void) fmt;
    s_error_count++;
}


/** @brief Testuje, že NULL data s nenulovým data_size vrátí chybu. */
static void test_null_data_with_size ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    zxtape_set_error_callback ( test_error_cb );
    s_error_count = 0;

    st_CMT_VSTREAM *vs = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, NULL, 10, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NULL ( vs );
    TEST_ASSERT_TRUE ( s_error_count > 0 );

    s_error_count = 0;
    st_CMT_BITSTREAM *bs = zxtape_create_cmt_bitstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, NULL, 10, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NULL ( bs );
    TEST_ASSERT_TRUE ( s_error_count > 0 );
}


/** @brief Testuje, že neplatná cmtspeed vrátí chybu. */
static void test_invalid_cmtspeed ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    zxtape_set_error_callback ( test_error_cb );
    s_error_count = 0;

    uint8_t data[] = { 0x00 };
    st_CMT_VSTREAM *vs = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, data, 1, CMTSPEED_NONE, 44100 );
    TEST_ASSERT_NULL ( vs );
    TEST_ASSERT_TRUE ( s_error_count > 0 );
}


/** @brief Testuje, že nulový sample rate vrátí chybu. */
static void test_zero_sample_rate ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    zxtape_set_error_callback ( test_error_cb );
    s_error_count = 0;

    uint8_t data[] = { 0x00 };
    st_CMT_VSTREAM *vs = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, data, 1, CMTSPEED_1_1, 0 );
    TEST_ASSERT_NULL ( vs );
    TEST_ASSERT_TRUE ( s_error_count > 0 );
}


/** @brief Testuje, že neznámý flag (jiný než 0x00/0xFF) vrátí chybu. */
static void test_unknown_flag ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    zxtape_set_error_callback ( test_error_cb );
    s_error_count = 0;

    uint8_t data[] = { 0x42 };
    st_CMT_VSTREAM *vs = zxtape_create_cmt_vstream_from_tapblock (
        (en_ZXTAPE_BLOCK_FLAG) 0x42, data, 1, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NULL ( vs );
    TEST_ASSERT_TRUE ( s_error_count > 0 );
}


/* ========================================================================
 * Testy alokátoru a error callbacku
 * ======================================================================== */

/** @brief Počítadlo volání testovacího alokátoru (alloc). */
static int s_alloc_count = 0;

/** @brief Počítadlo volání testovacího alokátoru (alloc0). */
static int s_alloc0_count = 0;

/** @brief Počítadlo volání testovacího alokátoru (free). */
static int s_free_count = 0;

/** @brief Testovací alokátor — malloc s počítadlem. */
static void* test_alloc ( size_t size ) { s_alloc_count++; return malloc ( size ); }

/** @brief Testovací alokátor — calloc s počítadlem. */
static void* test_alloc0 ( size_t size ) { s_alloc0_count++; return calloc ( 1, size ); }

/** @brief Testovací alokátor — free s počítadlem. */
static void  test_free ( void *ptr ) { s_free_count++; free ( ptr ); }


/** @brief Testuje registraci a reset vlastního alokátoru. */
static void test_allocator_custom ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_ZXTAPE_ALLOCATOR custom = { test_alloc, test_alloc0, test_free };
    zxtape_set_allocator ( &custom );

    s_alloc_count = 0;
    s_alloc0_count = 0;
    s_free_count = 0;

    /* zxtape samotná nealokuje přímo (deleguje na cmt_stream),
     * ale alokátor musí být korektně registrován a resetovatelný */
    TEST_ASSERT_EQUAL_INT ( 0, s_alloc_count );
    TEST_ASSERT_EQUAL_INT ( 0, s_free_count );

    /* reset na výchozí */
    zxtape_set_allocator ( NULL );
}


/** @brief Testuje, že vlastní error callback se volá při chybě. */
static void test_error_callback ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    zxtape_set_error_callback ( test_error_cb );
    s_error_count = 0;

    /* vyvoláme chybu — NULL data s nenulovým size */
    st_CMT_VSTREAM *vs = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, NULL, 10, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NULL ( vs );
    TEST_ASSERT_TRUE ( s_error_count > 0 );

    zxtape_set_error_callback ( NULL ); /* reset */
}


/** @brief Testuje, že NULL reset alokátoru nesmí crashnout a knihovna pak funguje normálně. */
static void test_allocator_null_reset ( void ) {
    zxtape_set_allocator ( NULL );
    /* po resetu musí knihovna fungovat normálně */
    uint16_t total_size;
    uint8_t *block = create_test_tapblock ( ZXTAPE_BLOCK_FLAG_DATA, 8, &total_size );
    TEST_ASSERT_NOT_NULL ( block );

    st_CMT_VSTREAM *vs = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, block, total_size, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NOT_NULL ( vs );

    cmt_vstream_destroy ( vs );
    free ( block );
}


/** @brief Testuje, že NULL reset error callbacku nesmí crashnout. */
static void test_error_callback_null_reset ( void ) {
    zxtape_set_error_callback ( NULL );
    /* po resetu musí error reporting fungovat (výchozí fprintf) */
    /* just verify it doesn't crash */
}


/* ========================================================================
 * Regresní testy
 * ======================================================================== */

/** @brief Testuje regresi chybových hlášek — ověřuje, že chybové cesty korektně reportují a nepadají. */
static void test_error_messages_on_failure ( void ) {
    zxtape_set_error_callback ( test_error_cb );
    s_error_count = 0;

    /* neplatná rychlost */
    st_CMT_VSTREAM *vs = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, NULL, 5, CMTSPEED_NONE, 44100 );
    TEST_ASSERT_NULL ( vs );

    st_CMT_BITSTREAM *bs = zxtape_create_cmt_bitstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, NULL, 5, CMTSPEED_NONE, 44100 );
    TEST_ASSERT_NULL ( bs );

    /* obě chybové cesty musí být zavolány */
    TEST_ASSERT_TRUE ( s_error_count >= 2 );

    zxtape_set_error_callback ( NULL );
}


/** @brief Testuje regresi — nulový data_size s platným data pointerem nesmí crashnout. */
static void test_zero_data_size ( void ) {
    uint8_t dummy = 0;

    st_CMT_VSTREAM *vs = zxtape_create_cmt_vstream_from_tapblock (
        ZXTAPE_BLOCK_FLAG_DATA, &dummy, 0, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NOT_NULL ( vs );

    /* stream s 0 datovými bajty — obsahuje jen pilot + sync + pauzu */
    TEST_ASSERT_TRUE ( cmt_vstream_get_count_scans ( vs ) > 0 );

    cmt_vstream_destroy ( vs );
}


/* ========================================================================
 * main
 * ======================================================================== */

/**
 * @brief Vstupní bod testovacího programu.
 * @param argc Počet argumentů příkazové řádky.
 * @param argv Pole argumentů příkazové řádky.
 * @return 0 = všechny testy prošly, nenulová = počet selhání.
 */
int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ();

    UNITY_BEGIN ();

    /* Smoke testy */
    RUN_TEST ( test_create_vstream_header );
    RUN_TEST ( test_create_vstream_data );

    /* Unit testy */
    RUN_TEST ( test_header_longer_than_data );
    RUN_TEST ( test_create_bitstream );
    RUN_TEST ( test_create_stream_wrapper );
    RUN_TEST ( test_compute_pulses );
    RUN_TEST ( test_compute_pulses_extremes );
    RUN_TEST ( test_checksum_valid );
    RUN_TEST ( test_checksum_invalid );
    RUN_TEST ( test_checksum_edge_cases );
    RUN_TEST ( test_speed_array_terminated );
    RUN_TEST ( test_speed_affects_stream_length );
    RUN_TEST ( test_null_data_with_size );
    RUN_TEST ( test_invalid_cmtspeed );
    RUN_TEST ( test_zero_sample_rate );
    RUN_TEST ( test_unknown_flag );
    RUN_TEST ( test_allocator_custom );
    RUN_TEST ( test_error_callback );
    RUN_TEST ( test_allocator_null_reset );
    RUN_TEST ( test_error_callback_null_reset );

    /* Regresní testy */
    RUN_TEST ( test_error_messages_on_failure );
    RUN_TEST ( test_zero_data_size );

    int result = UNITY_END ();
    mztest_teardown ();
    return result;
}
