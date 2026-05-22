/**
 * @file   test_cmt_stream.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Testy knihovny cmt_stream (CMT audio streamy)
 *
 * Testuje vytvareni/ruseni streamu, zapis/cteni dat, konverze mezi
 * formaty (bitstream <-> vstream), polaritu, hranicni pripady,
 * chybove stavy a wrapper dispatch.
 *
 * Vyzaduje inicializaci emulatoru (mztest_init).
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

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "libs/cmt_stream/cmt_stream.h"


/* ========================================================================
 * setUp / tearDown — volány před/po každém testu
 * ======================================================================== */

/**
 * @brief Inicializace pred kazdym testem (Unity setUp)
 */
void setUp ( void ) {
}

/**
 * @brief Uklid po kazdem testu (Unity tearDown)
 */
void tearDown ( void ) {
}


/* ========================================================================
 * SMOKE TESTY — základní funkčnost
 * ======================================================================== */


/** @brief Testuje vytvoreni a uvolneni bitstreamu */
void test_bitstream_new_destroy ( void ) {
    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 44100, 4, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );
    TEST_ASSERT_EQUAL_UINT32 ( 44100, bs->rate );
    TEST_ASSERT_EQUAL_UINT32 ( 4, bs->blocks );
    TEST_ASSERT_NOT_NULL ( bs->data );
    cmt_bitstream_destroy ( bs );
}


/** @brief Testuje vytvoreni a uvolneni vstreamu */
void test_vstream_new_destroy ( void ) {
    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_EQUAL_UINT32 ( 44100, vs->rate );
    TEST_ASSERT_EQUAL_INT ( 0, vs->start_value );
    cmt_vstream_destroy ( vs );
}


/** @brief Testuje vytvoreni a uvolneni wrapper streamu */
void test_stream_new_destroy ( void ) {
    st_CMT_STREAM *s = cmt_stream_new ( CMT_STREAM_TYPE_BITSTREAM );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_INT ( CMT_STREAM_TYPE_BITSTREAM, s->stream_type );
    /* wrapper nemá vnitřní stream — destroy musí být bezpečný i tak */
    cmt_stream_destroy ( s );
}


/** @brief Testuje zapis a cteni bitu na konkretni pozici v bitstreamu */
void test_bitstream_set_get_value ( void ) {
    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 44100, 2, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );

    /* nastavení bitů */
    cmt_bitstream_set_value_on_position ( bs, 0, 1 );
    cmt_bitstream_set_value_on_position ( bs, 1, 0 );
    cmt_bitstream_set_value_on_position ( bs, 2, 1 );
    cmt_bitstream_set_value_on_position ( bs, 31, 1 );

    /* ověření */
    TEST_ASSERT_EQUAL_INT ( 1, cmt_bitstream_get_value_on_position ( bs, 0 ) );
    TEST_ASSERT_EQUAL_INT ( 0, cmt_bitstream_get_value_on_position ( bs, 1 ) );
    TEST_ASSERT_EQUAL_INT ( 1, cmt_bitstream_get_value_on_position ( bs, 2 ) );
    TEST_ASSERT_EQUAL_INT ( 0, cmt_bitstream_get_value_on_position ( bs, 3 ) );
    TEST_ASSERT_EQUAL_INT ( 1, cmt_bitstream_get_value_on_position ( bs, 31 ) );

    cmt_bitstream_destroy ( bs );
}


/* ========================================================================
 * UNIT TESTY: BITSTREAM
 * ======================================================================== */


/** @brief Testuje spravne nastaveni parametru pri vytvoreni bitstreamu */
void test_bitstream_new_params ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 22050, 8, CMT_STREAM_POLARITY_INVERTED );
    TEST_ASSERT_NOT_NULL ( bs );
    TEST_ASSERT_EQUAL_UINT32 ( 22050, bs->rate );
    TEST_ASSERT_EQUAL_UINT32 ( 8, bs->blocks );
    TEST_ASSERT_EQUAL_INT ( CMT_STREAM_POLARITY_INVERTED, bs->polarity );
    TEST_ASSERT_FLOAT_WITHIN ( 1e-10, 1.0 / 22050, bs->scan_time );
    TEST_ASSERT_EQUAL_UINT32 ( 0, bs->scans );
    TEST_ASSERT_FLOAT_WITHIN ( 1e-10, 0.0, bs->stream_length );
    cmt_bitstream_destroy ( bs );
}


/** @brief Testuje vytvoreni bitstreamu s 0 bloky — bez datove oblasti */
void test_bitstream_new_zero_blocks ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 44100, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );
    TEST_ASSERT_NULL ( bs->data );
    TEST_ASSERT_EQUAL_UINT32 ( 0, bs->blocks );
    cmt_bitstream_destroy ( bs );
}


/** @brief Testuje vypocet potrebneho poctu bloku z poctu scanu */
void test_bitstream_blocks_from_scans ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    TEST_ASSERT_EQUAL_UINT32 ( 0, cmt_bitstream_compute_required_blocks_from_scans ( 0 ) );
    TEST_ASSERT_EQUAL_UINT32 ( 1, cmt_bitstream_compute_required_blocks_from_scans ( 1 ) );
    TEST_ASSERT_EQUAL_UINT32 ( 1, cmt_bitstream_compute_required_blocks_from_scans ( 31 ) );
    TEST_ASSERT_EQUAL_UINT32 ( 1, cmt_bitstream_compute_required_blocks_from_scans ( 32 ) );
    TEST_ASSERT_EQUAL_UINT32 ( 2, cmt_bitstream_compute_required_blocks_from_scans ( 33 ) );
    TEST_ASSERT_EQUAL_UINT32 ( 2, cmt_bitstream_compute_required_blocks_from_scans ( 64 ) );
    TEST_ASSERT_EQUAL_UINT32 ( 3, cmt_bitstream_compute_required_blocks_from_scans ( 65 ) );
}


/** @brief Testuje ze set_value_on_position aktualizuje scans a stream_length */
void test_bitstream_set_value_updates_scans ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 44100, 4, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );
    TEST_ASSERT_EQUAL_UINT32 ( 0, bs->scans );

    cmt_bitstream_set_value_on_position ( bs, 0, 1 );
    TEST_ASSERT_EQUAL_UINT32 ( 1, bs->scans );

    cmt_bitstream_set_value_on_position ( bs, 99, 1 );
    TEST_ASSERT_EQUAL_UINT32 ( 100, bs->scans );
    TEST_ASSERT_FLOAT_WITHIN ( 1e-10, 100.0 / 44100.0, bs->stream_length );

    /* zápis na existující pozici nemění scans */
    cmt_bitstream_set_value_on_position ( bs, 50, 1 );
    TEST_ASSERT_EQUAL_UINT32 ( 100, bs->scans );

    cmt_bitstream_destroy ( bs );
}


/** @brief Testuje get_size — vraci velikost bitstreamu v bajtech */
void test_bitstream_get_size ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 44100, 4, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );
    /* 4 bloky × 4 bajty = 16 B */
    TEST_ASSERT_EQUAL_UINT32 ( 16, cmt_bitstream_get_size ( bs ) );
    cmt_bitstream_destroy ( bs );

    bs = cmt_bitstream_new ( 44100, 1, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );
    TEST_ASSERT_EQUAL_UINT32 ( 4, cmt_bitstream_get_size ( bs ) );
    cmt_bitstream_destroy ( bs );
}


/** @brief Testuje change_polarity — invertuje data a aktualizuje polarity pole */
void test_bitstream_change_polarity ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 44100, 1, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );

    /* nastavení vzoru */
    cmt_bitstream_set_value_on_position ( bs, 0, 1 );
    cmt_bitstream_set_value_on_position ( bs, 1, 0 );
    cmt_bitstream_set_value_on_position ( bs, 2, 1 );

    /* invertování */
    cmt_bitstream_change_polarity ( bs, CMT_STREAM_POLARITY_INVERTED );
    TEST_ASSERT_EQUAL_INT ( CMT_STREAM_POLARITY_INVERTED, bs->polarity );
    TEST_ASSERT_EQUAL_INT ( 0, cmt_bitstream_get_value_on_position ( bs, 0 ) );
    TEST_ASSERT_EQUAL_INT ( 1, cmt_bitstream_get_value_on_position ( bs, 1 ) );
    TEST_ASSERT_EQUAL_INT ( 0, cmt_bitstream_get_value_on_position ( bs, 2 ) );

    /* opakované volání se stejnou polaritou — nic se nemění */
    cmt_bitstream_change_polarity ( bs, CMT_STREAM_POLARITY_INVERTED );
    TEST_ASSERT_EQUAL_INT ( 0, cmt_bitstream_get_value_on_position ( bs, 0 ) );

    /* zpět na normální */
    cmt_bitstream_change_polarity ( bs, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_EQUAL_INT ( CMT_STREAM_POLARITY_NORMAL, bs->polarity );
    TEST_ASSERT_EQUAL_INT ( 1, cmt_bitstream_get_value_on_position ( bs, 0 ) );

    cmt_bitstream_destroy ( bs );
}


/** @brief Testuje prevod casu na pozici v bitstreamu */
void test_bitstream_position_by_time ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 44100, 2, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );

    /* čas 0 = pozice 0 */
    TEST_ASSERT_EQUAL_UINT32 ( 0, cmt_bitstream_get_position_by_time ( bs, 0.0 ) );

    /* 1 vzorek = 1/44100 s */
    uint32_t pos = cmt_bitstream_get_position_by_time ( bs, 1.0 / 44100 );
    TEST_ASSERT_EQUAL_UINT32 ( 1, pos );

    cmt_bitstream_destroy ( bs );
}


/* ========================================================================
 * UNIT TESTY: VSTREAM
 * ======================================================================== */


/** @brief Testuje spravne nastaveni parametru pri vytvoreni vstreamu */
void test_vstream_new_params ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 1, CMT_STREAM_POLARITY_INVERTED );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_EQUAL_UINT32 ( 44100, vs->rate );
    TEST_ASSERT_EQUAL_INT ( CMT_VSTREAM_BYTELENGTH8, vs->min_byte_length );
    TEST_ASSERT_EQUAL_INT ( 1, vs->start_value );
    TEST_ASSERT_EQUAL_INT ( CMT_STREAM_POLARITY_INVERTED, vs->polarity );
    TEST_ASSERT_FLOAT_WITHIN ( 1e-10, 1.0 / 44100, vs->scan_time );
    cmt_vstream_destroy ( vs );
}


/** @brief Testuje ze nevalidni min_byte_length vraci NULL */
void test_vstream_new_invalid_byte_len ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, 3, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NULL ( vs );

    vs = cmt_vstream_new ( 44100, 5, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NULL ( vs );
}


/** @brief Testuje ze nevalidni rate (0) vraci NULL — regrese BUG 3 */
void test_vstream_new_invalid_rate ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    /* rate 0 */
    st_CMT_VSTREAM *vs = cmt_vstream_new ( 0, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NULL ( vs );
}


/** @brief Testuje validaci rate pro vysoke hodnoty — po refaktorizaci timing systemu */
void test_vstream_new_high_rate ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    /* rate vyšší než pixel clock (17 721 600 Hz) — dříve by vracelo NULL,
     * po refaktorizaci je povolené, knihovna nezávisí na pxclk */
    st_CMT_VSTREAM *vs = cmt_vstream_new ( 48000000, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_EQUAL_UINT32 ( 48000000, vs->rate );
    TEST_ASSERT_FLOAT_WITHIN ( 1e-15, 1.0 / 48000000, vs->scan_time );
    cmt_vstream_destroy ( vs );

    /* rate 1 — minimální platná hodnota */
    vs = cmt_vstream_new ( 1, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_EQUAL_UINT32 ( 1, vs->rate );
    cmt_vstream_destroy ( vs );

    /* rate 384000 — 2× max profesionální audio */
    vs = cmt_vstream_new ( 384000, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_EQUAL_UINT32 ( 384000, vs->rate );
    cmt_vstream_destroy ( vs );
}


/** @brief Testuje vypocet msticks z rate — bez zavislosti na pxclk */
void test_vstream_msticks_calculation ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    /* rate 44100: msticks = round(0.001 / (1/44100)) = round(44.1) = 44 */
    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_EQUAL_UINT32 ( 44, vs->msticks );
    cmt_vstream_destroy ( vs );

    /* rate 22050: msticks = round(0.001 / (1/22050)) = round(22.05) = 22 */
    vs = cmt_vstream_new ( 22050, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_EQUAL_UINT32 ( 22, vs->msticks );
    cmt_vstream_destroy ( vs );

    /* rate 48000: msticks = round(0.001 / (1/48000)) = round(48.0) = 48 */
    vs = cmt_vstream_new ( 48000, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_EQUAL_UINT32 ( 48, vs->msticks );
    cmt_vstream_destroy ( vs );
}


/**
 * @brief Testuje numericku shodu sekundoveho prepoctu s puvodnim GDG tick prepoctem
 *
 * Overuje, ze round(seconds * rate) == round(gdg_ticks * rate / pxclk)
 * pro namerene hodnoty z Intercopy (mztape) a ZX tape.
 */
void test_vstream_timing_numerical_equivalence ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    /* referenční pixel clock MZ-800 */
    const double pxclk = 17721600.0;
    const uint32_t rate = 44100;

    /*
     * mztape Intercopy: GDG ticky {8335,8760}, {4356,4930}
     * sekundové konstanty: {0.000470330,0.000494308}, {0.000245802,0.000278204}
     */
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 8335.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000470330 * rate ) );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 8760.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000494308 * rate ) );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 4356.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000245802 * rate ) );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 4930.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000278204 * rate ) );

    /*
     * mztape cmt.com: GDG ticky {9300,8660}, {5400,4660}
     * sekundové konstanty: {0.000524796,0.000488665}, {0.000304762,0.000262935}
     */
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 9300.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000524796 * rate ) );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 8660.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000488665 * rate ) );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 5400.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000304762 * rate ) );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 4660.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000262935 * rate ) );

    /*
     * zxtape: GDG ticky pilot {10853,10853}, sync {2571,3299},
     *         long {8583,8583}, short {4291,4291}
     * sekundové: pilot {0.000612395,0.000612395}, sync {0.000145070,0.000186161},
     *            long {0.000484335,0.000484335}, short {0.000242140,0.000242140}
     */
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 10853.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000612395 * rate ) );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 2571.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000145070 * rate ) );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 3299.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000186161 * rate ) );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 8583.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000484335 * rate ) );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) round ( 4291.0 * rate / pxclk ),
                               (uint32_t) round ( 0.000242140 * rate ) );
}


/** @brief Testuje pridani jedne hodnoty do vstreamu */
void test_vstream_add_value_basic ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    int ret = cmt_vstream_add_value ( vs, 0, 100 );
    TEST_ASSERT_EQUAL_INT ( EXIT_SUCCESS, ret );
    TEST_ASSERT_EQUAL_UINT64 ( 100, vs->scans );

    cmt_vstream_destroy ( vs );
}


/** @brief Testuje pridani vice hodnot se stejnou polaritou — merge do jednoho pulzu */
void test_vstream_add_value_same_value ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    cmt_vstream_add_value ( vs, 0, 50 );
    cmt_vstream_add_value ( vs, 0, 50 );
    TEST_ASSERT_EQUAL_UINT64 ( 100, vs->scans );

    /* ověření čtením */
    cmt_vstream_read_reset ( vs );
    uint64_t samples;
    int value;
    int ret = cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( EXIT_SUCCESS, ret );
    TEST_ASSERT_EQUAL_INT ( 0, value );
    TEST_ASSERT_EQUAL_UINT64 ( 100, samples );

    cmt_vstream_destroy ( vs );
}


/** @brief Testuje stridani hodnot ve vstreamu — spravne cteni pulzu */
void test_vstream_add_value_alternating ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    cmt_vstream_add_value ( vs, 0, 10 );
    cmt_vstream_add_value ( vs, 1, 20 );
    cmt_vstream_add_value ( vs, 0, 30 );
    TEST_ASSERT_EQUAL_UINT64 ( 60, vs->scans );

    cmt_vstream_read_reset ( vs );
    uint64_t samples;
    int value;

    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( 0, value );
    TEST_ASSERT_EQUAL_UINT64 ( 10, samples );

    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( 1, value );
    TEST_ASSERT_EQUAL_UINT64 ( 20, samples );

    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( 0, value );
    TEST_ASSERT_EQUAL_UINT64 ( 30, samples );

    /* za koncem */
    int ret = cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( EXIT_FAILURE, ret );

    cmt_vstream_destroy ( vs );
}


/** @brief Testuje presah 0xFE v uint8 modu — eskalace na uint16 */
void test_vstream_add_value_overflow_u8 ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    /* 300 vzorků > 0xFE (254) — musí eskalovat na uint16 */
    int ret = cmt_vstream_add_value ( vs, 0, 300 );
    TEST_ASSERT_EQUAL_INT ( EXIT_SUCCESS, ret );
    TEST_ASSERT_EQUAL_UINT64 ( 300, vs->scans );

    /* ověření zpětným čtením */
    cmt_vstream_read_reset ( vs );
    uint64_t samples;
    int value;
    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_UINT64 ( 300, samples );
    TEST_ASSERT_EQUAL_INT ( 0, value );

    cmt_vstream_destroy ( vs );
}


/** @brief Testuje presah 0xFFFE — eskalace na uint32 */
void test_vstream_add_value_overflow_u16 ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    /* 70000 > 0xFFFE (65534) — musí eskalovat na uint32 */
    int ret = cmt_vstream_add_value ( vs, 0, 70000 );
    TEST_ASSERT_EQUAL_INT ( EXIT_SUCCESS, ret );
    TEST_ASSERT_EQUAL_UINT64 ( 70000, vs->scans );

    /* ověření zpětným čtením */
    cmt_vstream_read_reset ( vs );
    uint64_t samples;
    int value;
    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_UINT64 ( 70000, samples );
    TEST_ASSERT_EQUAL_INT ( 0, value );

    cmt_vstream_destroy ( vs );
}


/** @brief Testuje ze count_samples=0 vraci chybu */
void test_vstream_add_value_zero_count ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    int ret = cmt_vstream_add_value ( vs, 0, 0 );
    TEST_ASSERT_EQUAL_INT ( EXIT_FAILURE, ret );

    cmt_vstream_destroy ( vs );
}


/** @brief Testuje reset cteci pozice vstreamu */
void test_vstream_read_reset ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    cmt_vstream_add_value ( vs, 0, 10 );
    cmt_vstream_add_value ( vs, 1, 20 );

    /* přečtení */
    uint64_t samples;
    int value;
    cmt_vstream_read_pulse ( vs, &samples, &value );
    cmt_vstream_read_pulse ( vs, &samples, &value );

    /* reset a znovu */
    cmt_vstream_read_reset ( vs );
    TEST_ASSERT_EQUAL_UINT32 ( 0, vs->last_read_position );
    TEST_ASSERT_EQUAL_UINT64 ( 0, vs->last_read_position_sample );
    TEST_ASSERT_EQUAL_INT ( vs->start_value, vs->last_read_value );

    int ret = cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( EXIT_SUCCESS, ret );
    TEST_ASSERT_EQUAL_INT ( 0, value );
    TEST_ASSERT_EQUAL_UINT64 ( 10, samples );

    cmt_vstream_destroy ( vs );
}


/** @brief Testuje get_value — pristup k hodnote na dane vzorkove pozici */
void test_vstream_get_value ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    /* 10× 0, 20× 1, 30× 0 */
    cmt_vstream_add_value ( vs, 0, 10 );
    cmt_vstream_add_value ( vs, 1, 20 );
    cmt_vstream_add_value ( vs, 0, 30 );

    cmt_vstream_read_reset ( vs );
    int value;

    /* pozice 5 — v úseku 0 */
    int ret = cmt_vstream_get_value ( vs, 5, &value );
    TEST_ASSERT_EQUAL_INT ( EXIT_SUCCESS, ret );
    TEST_ASSERT_EQUAL_INT ( 0, value );

    /* pozice 15 — v úseku 1 */
    ret = cmt_vstream_get_value ( vs, 15, &value );
    TEST_ASSERT_EQUAL_INT ( EXIT_SUCCESS, ret );
    TEST_ASSERT_EQUAL_INT ( 1, value );

    /* pozice 40 — v úseku 0 */
    ret = cmt_vstream_get_value ( vs, 40, &value );
    TEST_ASSERT_EQUAL_INT ( EXIT_SUCCESS, ret );
    TEST_ASSERT_EQUAL_INT ( 0, value );

    cmt_vstream_destroy ( vs );
}


/** @brief Testuje change_polarity — aktualizuje vsechny pole vstreamu (regrese BUG 4) */
void test_vstream_change_polarity ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    cmt_vstream_add_value ( vs, 0, 10 );
    cmt_vstream_add_value ( vs, 1, 10 );

    /* invertování */
    cmt_vstream_change_polarity ( vs, CMT_STREAM_POLARITY_INVERTED );
    TEST_ASSERT_EQUAL_INT ( CMT_STREAM_POLARITY_INVERTED, vs->polarity );
    TEST_ASSERT_EQUAL_INT ( 1, vs->start_value );
    TEST_ASSERT_EQUAL_INT ( 0, vs->last_set_value );
    TEST_ASSERT_EQUAL_INT ( 1, vs->last_read_value );

    /* opakované volání se stejnou polaritou — nic se nemění */
    cmt_vstream_change_polarity ( vs, CMT_STREAM_POLARITY_INVERTED );
    TEST_ASSERT_EQUAL_INT ( 1, vs->start_value );

    cmt_vstream_destroy ( vs );
}


/** @brief Testuje vstream s BYTELENGTH16 — zapis a cteni pulzu */
void test_vstream_bytelength16 ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH16, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    cmt_vstream_add_value ( vs, 0, 1000 );
    cmt_vstream_add_value ( vs, 1, 2000 );

    cmt_vstream_read_reset ( vs );
    uint64_t samples;
    int value;

    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( 0, value );
    TEST_ASSERT_EQUAL_UINT64 ( 1000, samples );

    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( 1, value );
    TEST_ASSERT_EQUAL_UINT64 ( 2000, samples );

    cmt_vstream_destroy ( vs );
}


/** @brief Testuje vstream s BYTELENGTH32 — zapis a cteni pulzu */
void test_vstream_bytelength32 ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH32, 1, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    cmt_vstream_add_value ( vs, 1, 100000 );
    cmt_vstream_add_value ( vs, 0, 200000 );

    cmt_vstream_read_reset ( vs );
    uint64_t samples;
    int value;

    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( 1, value );
    TEST_ASSERT_EQUAL_UINT64 ( 100000, samples );

    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( 0, value );
    TEST_ASSERT_EQUAL_UINT64 ( 200000, samples );

    cmt_vstream_destroy ( vs );
}


/* ========================================================================
 * UNIT TESTY: KONVERZE
 * ======================================================================== */


/** @brief Testuje konverzi vstream na bitstream — zakladni pripad */
void test_vstream_to_bitstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );

    /* 10× 0, 10× 1, 10× 0 */
    cmt_vstream_add_value ( vs, 0, 10 );
    cmt_vstream_add_value ( vs, 1, 10 );
    cmt_vstream_add_value ( vs, 0, 10 );

    st_CMT_BITSTREAM *bs = cmt_bitstream_new_from_vstream ( vs, 0 );
    TEST_ASSERT_NOT_NULL ( bs );
    TEST_ASSERT_EQUAL_UINT32 ( 44100, bs->rate );

    /* ověření dat */
    uint32_t i;
    for ( i = 0; i < 10; i++ ) {
        TEST_ASSERT_EQUAL_INT ( 0, cmt_bitstream_get_value_on_position ( bs, i ) );
    }
    for ( i = 10; i < 20; i++ ) {
        TEST_ASSERT_EQUAL_INT ( 1, cmt_bitstream_get_value_on_position ( bs, i ) );
    }
    for ( i = 20; i < 30; i++ ) {
        TEST_ASSERT_EQUAL_INT ( 0, cmt_bitstream_get_value_on_position ( bs, i ) );
    }

    cmt_bitstream_destroy ( bs );
    cmt_vstream_destroy ( vs );
}


/** @brief Testuje konverzi bitstream na vstream */
void test_bitstream_to_vstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    /* vytvoříme bitstream s vzorem: 5× 1, 5× 0, 5× 1 */
    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 44100, 1, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );

    uint32_t i;
    for ( i = 0; i < 5; i++ ) cmt_bitstream_set_value_on_position ( bs, i, 1 );
    for ( i = 5; i < 10; i++ ) cmt_bitstream_set_value_on_position ( bs, i, 0 );
    for ( i = 10; i < 15; i++ ) cmt_bitstream_set_value_on_position ( bs, i, 1 );

    st_CMT_VSTREAM *vs = cmt_vstream_new_from_bitstream ( bs, 0 );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_EQUAL_UINT32 ( 44100, vs->rate );
    TEST_ASSERT_EQUAL_UINT64 ( 15, vs->scans );

    /* ověření pulzů */
    cmt_vstream_read_reset ( vs );
    uint64_t samples;
    int value;

    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( 1, value );
    TEST_ASSERT_EQUAL_UINT64 ( 5, samples );

    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( 0, value );
    TEST_ASSERT_EQUAL_UINT64 ( 5, samples );

    cmt_vstream_read_pulse ( vs, &samples, &value );
    TEST_ASSERT_EQUAL_INT ( 1, value );
    TEST_ASSERT_EQUAL_UINT64 ( 5, samples );

    cmt_vstream_destroy ( vs );
    cmt_bitstream_destroy ( bs );
}


/** @brief Testuje roundtrip konverzi vstream -> bitstream -> vstream — data se zachovaji */
void test_roundtrip_vstream_bitstream_vstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    /* vytvoříme vstream s vzorem */
    st_CMT_VSTREAM *vs1 = cmt_vstream_new ( 44100, CMT_VSTREAM_BYTELENGTH8, 1, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs1 );
    cmt_vstream_add_value ( vs1, 1, 100 );
    cmt_vstream_add_value ( vs1, 0, 200 );
    cmt_vstream_add_value ( vs1, 1, 50 );

    /* vstream → bitstream */
    st_CMT_BITSTREAM *bs = cmt_bitstream_new_from_vstream ( vs1, 0 );
    TEST_ASSERT_NOT_NULL ( bs );

    /* bitstream → vstream */
    st_CMT_VSTREAM *vs2 = cmt_vstream_new_from_bitstream ( bs, 0 );
    TEST_ASSERT_NOT_NULL ( vs2 );

    /* porovnání počtu vzorků */
    TEST_ASSERT_EQUAL_UINT64 ( cmt_vstream_get_count_scans ( vs1 ), cmt_vstream_get_count_scans ( vs2 ) );

    /* porovnání dat — přečteme oba vstreamy a porovnáme pulzy */
    cmt_vstream_read_reset ( vs1 );
    cmt_vstream_read_reset ( vs2 );

    uint64_t s1, s2;
    int v1, v2;
    while ( EXIT_SUCCESS == cmt_vstream_read_pulse ( vs1, &s1, &v1 ) ) {
        int ret = cmt_vstream_read_pulse ( vs2, &s2, &v2 );
        TEST_ASSERT_EQUAL_INT ( EXIT_SUCCESS, ret );
        TEST_ASSERT_EQUAL_INT ( v1, v2 );
        TEST_ASSERT_EQUAL_UINT64 ( s1, s2 );
    }

    cmt_vstream_destroy ( vs2 );
    cmt_bitstream_destroy ( bs );
    cmt_vstream_destroy ( vs1 );
}


/** @brief Testuje roundtrip konverzi bitstream -> vstream -> bitstream — data se zachovaji */
void test_roundtrip_bitstream_vstream_bitstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    /* vytvoříme bitstream s náhodným vzorem */
    st_CMT_BITSTREAM *bs1 = cmt_bitstream_new ( 44100, 2, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs1 );

    uint32_t i;
    for ( i = 0; i < 50; i++ ) {
        /* střídavý vzor s delšími běhy */
        int val = ( i < 15 ) ? 0 : ( i < 30 ) ? 1 : 0;
        cmt_bitstream_set_value_on_position ( bs1, i, val );
    }

    /* bitstream → vstream */
    st_CMT_VSTREAM *vs = cmt_vstream_new_from_bitstream ( bs1, 0 );
    TEST_ASSERT_NOT_NULL ( vs );

    /* vstream → bitstream */
    st_CMT_BITSTREAM *bs2 = cmt_bitstream_new_from_vstream ( vs, 0 );
    TEST_ASSERT_NOT_NULL ( bs2 );

    /* porovnání bit po bitu */
    TEST_ASSERT_EQUAL_UINT64 ( bs1->scans, bs2->scans );
    for ( i = 0; i < bs1->scans; i++ ) {
        TEST_ASSERT_EQUAL_INT (
            cmt_bitstream_get_value_on_position ( bs1, i ),
            cmt_bitstream_get_value_on_position ( bs2, i )
        );
    }

    cmt_bitstream_destroy ( bs2 );
    cmt_vstream_destroy ( vs );
    cmt_bitstream_destroy ( bs1 );
}


/* ========================================================================
 * UNIT TESTY: STREAM WRAPPER
 * ======================================================================== */


/** @brief Testuje get_stream_type pro bitstream wrapper */
void test_stream_type_bitstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_STREAM *s = cmt_stream_new ( CMT_STREAM_TYPE_BITSTREAM );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_INT ( CMT_STREAM_TYPE_BITSTREAM, cmt_stream_get_stream_type ( s ) );
    cmt_stream_destroy ( s );
}


/** @brief Testuje get_stream_type pro vstream wrapper */
void test_stream_type_vstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_STREAM *s = cmt_stream_new ( CMT_STREAM_TYPE_VSTREAM );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_INT ( CMT_STREAM_TYPE_VSTREAM, cmt_stream_get_stream_type ( s ) );
    cmt_stream_destroy ( s );
}


/** @brief Testuje get_stream_type_txt — textova reprezentace typu streamu */
void test_stream_type_txt ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_STREAM *sb = cmt_stream_new ( CMT_STREAM_TYPE_BITSTREAM );
    TEST_ASSERT_NOT_NULL ( sb );
    TEST_ASSERT_EQUAL_STRING ( "bitstream", cmt_stream_get_stream_type_txt ( sb ) );
    cmt_stream_destroy ( sb );

    st_CMT_STREAM *sv = cmt_stream_new ( CMT_STREAM_TYPE_VSTREAM );
    TEST_ASSERT_NOT_NULL ( sv );
    TEST_ASSERT_EQUAL_STRING ( "vstream", cmt_stream_get_stream_type_txt ( sv ) );
    cmt_stream_destroy ( sv );
}


/** @brief Testuje wrapper gettery — spravna delegace na bitstream */
void test_stream_getters_bitstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 22050, 2, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );
    cmt_bitstream_set_value_on_position ( bs, 0, 1 );
    cmt_bitstream_set_value_on_position ( bs, 9, 1 );

    st_CMT_STREAM *s = cmt_stream_new ( CMT_STREAM_TYPE_BITSTREAM );
    TEST_ASSERT_NOT_NULL ( s );
    s->str.bitstream = bs;

    TEST_ASSERT_EQUAL_UINT32 ( cmt_bitstream_get_size ( bs ), cmt_stream_get_size ( s ) );
    TEST_ASSERT_EQUAL_UINT32 ( 22050, cmt_stream_get_rate ( s ) );
    TEST_ASSERT_EQUAL_UINT64 ( 10, cmt_stream_get_count_scans ( s ) );
    TEST_ASSERT_FLOAT_WITHIN ( 1e-10, 1.0 / 22050.0, cmt_stream_get_scantime ( s ) );

    cmt_stream_destroy ( s );
}


/** @brief Testuje wrapper gettery — spravna delegace na vstream */
void test_stream_getters_vstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_VSTREAM *vs = cmt_vstream_new ( 11025, CMT_VSTREAM_BYTELENGTH8, 0, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( vs );
    cmt_vstream_add_value ( vs, 0, 100 );

    st_CMT_STREAM *s = cmt_stream_new ( CMT_STREAM_TYPE_VSTREAM );
    TEST_ASSERT_NOT_NULL ( s );
    s->str.vstream = vs;

    TEST_ASSERT_EQUAL_UINT32 ( cmt_vstream_get_size ( vs ), cmt_stream_get_size ( s ) );
    TEST_ASSERT_EQUAL_UINT32 ( 11025, cmt_stream_get_rate ( s ) );
    TEST_ASSERT_EQUAL_UINT64 ( 100, cmt_stream_get_count_scans ( s ) );

    cmt_stream_destroy ( s );
}


/** @brief Testuje set_polarity — wrapper deleguje na vnitrni bitstream */
void test_stream_set_polarity ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_CMT_BITSTREAM *bs = cmt_bitstream_new ( 44100, 1, CMT_STREAM_POLARITY_NORMAL );
    TEST_ASSERT_NOT_NULL ( bs );
    cmt_bitstream_set_value_on_position ( bs, 0, 1 );

    st_CMT_STREAM *s = cmt_stream_new ( CMT_STREAM_TYPE_BITSTREAM );
    TEST_ASSERT_NOT_NULL ( s );
    s->str.bitstream = bs;

    cmt_stream_set_polarity ( s, CMT_STREAM_POLARITY_INVERTED );
    TEST_ASSERT_EQUAL_INT ( CMT_STREAM_POLARITY_INVERTED, bs->polarity );
    TEST_ASSERT_EQUAL_INT ( 0, cmt_bitstream_get_value_on_position ( bs, 0 ) );

    cmt_stream_destroy ( s );
}


/* ========================================================================
 * UNIT TESTY: CHYBOVÉ STAVY
 * ======================================================================== */


/** @brief Testuje ze destroy(NULL) pro bitstream necrashne */
void test_bitstream_destroy_null ( void ) {
    cmt_bitstream_destroy ( NULL );
}

/** @brief Testuje ze destroy(NULL) pro vstream necrashne */
void test_vstream_destroy_null ( void ) {
    cmt_vstream_destroy ( NULL );
}

/** @brief Testuje ze destroy(NULL) pro stream wrapper necrashne */
void test_stream_destroy_null ( void ) {
    cmt_stream_destroy ( NULL );
}


/** @brief Testuje ze neznamy typ streamu vraci NULL */
void test_stream_new_unknown_type ( void ) {
    st_CMT_STREAM *s = cmt_stream_new ( (en_CMT_STREAM_TYPE) 99 );
    TEST_ASSERT_NULL ( s );
}


/* ========================================================================
 * HLAVNÍ FUNKCE
 * ======================================================================== */


/**
 * @brief Hlavni funkce testoveho modulu cmt_stream
 * @param argc Pocet argumentu prikazove radky
 * @param argv Argumenty prikazove radky
 * @return Navratovy kod Unity (0 = vsechny testy prosly)
 */
int main ( int argc, char *argv[] ) {

    mztest_parse_args ( argc, argv );
    mztest_init ();

    UNITY_BEGIN ();

    /* === smoke — základní funkčnost === */
    RUN_TEST ( test_bitstream_new_destroy );
    RUN_TEST ( test_vstream_new_destroy );
    RUN_TEST ( test_stream_new_destroy );
    RUN_TEST ( test_bitstream_set_get_value );

    /* === unit: bitstream === */
    RUN_TEST ( test_bitstream_new_params );
    RUN_TEST ( test_bitstream_new_zero_blocks );
    RUN_TEST ( test_bitstream_blocks_from_scans );
    RUN_TEST ( test_bitstream_set_value_updates_scans );
    RUN_TEST ( test_bitstream_get_size );
    RUN_TEST ( test_bitstream_change_polarity );
    RUN_TEST ( test_bitstream_position_by_time );

    /* === unit: vstream === */
    RUN_TEST ( test_vstream_new_params );
    RUN_TEST ( test_vstream_new_invalid_byte_len );
    RUN_TEST ( test_vstream_new_invalid_rate );
    RUN_TEST ( test_vstream_new_high_rate );
    RUN_TEST ( test_vstream_msticks_calculation );
    RUN_TEST ( test_vstream_add_value_basic );
    RUN_TEST ( test_vstream_add_value_same_value );
    RUN_TEST ( test_vstream_add_value_alternating );
    RUN_TEST ( test_vstream_add_value_overflow_u8 );
    RUN_TEST ( test_vstream_add_value_overflow_u16 );
    RUN_TEST ( test_vstream_add_value_zero_count );
    RUN_TEST ( test_vstream_read_reset );
    RUN_TEST ( test_vstream_get_value );
    RUN_TEST ( test_vstream_change_polarity );
    RUN_TEST ( test_vstream_bytelength16 );
    RUN_TEST ( test_vstream_bytelength32 );

    /* === unit: konverze === */
    RUN_TEST ( test_vstream_to_bitstream );
    RUN_TEST ( test_bitstream_to_vstream );
    RUN_TEST ( test_roundtrip_vstream_bitstream_vstream );
    RUN_TEST ( test_roundtrip_bitstream_vstream_bitstream );

    /* === unit: stream wrapper === */
    RUN_TEST ( test_stream_type_bitstream );
    RUN_TEST ( test_stream_type_vstream );
    RUN_TEST ( test_stream_type_txt );
    RUN_TEST ( test_stream_getters_bitstream );
    RUN_TEST ( test_stream_getters_vstream );
    RUN_TEST ( test_stream_set_polarity );

    /* === regresní: timing refaktorizace === */
    RUN_TEST ( test_vstream_timing_numerical_equivalence );

    /* === chybové stavy === */
    RUN_TEST ( test_bitstream_destroy_null );
    RUN_TEST ( test_vstream_destroy_null );
    RUN_TEST ( test_stream_destroy_null );
    RUN_TEST ( test_stream_new_unknown_type );

    int result = UNITY_END ();

    mztest_teardown ();
    return result;
}
