/**
 * @file   test_mzglyphs.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 1.0.0
 * @brief  Testy knihoven mz_vcode a mzglyphs.
 *
 * Testuje:
 *   - mz_vcode: ASCII -> video kód konverze (EU a JP varianta),
 *     speciální chování dump verze (řídící znaky -> tečka),
 *     verze API.
 *   - mzglyphs: codepoint mapování (vzorec a charset offsety),
 *     UTF-8 enkódování (3-byte sekvence v PUA), thread-safe buf
 *     varianta, verze API.
 *
 * Standalone test - obě knihovny jsou nezávislé na emulátorovém
 * jádře, linkuje se pouze s Unity + mzlib_mz_vcode + mzlib_mzglyphs.
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

#include "unity.h"

#include <string.h>
#include <stdint.h>

#include "libs/mz_vcode/mz_vcode.h"
#include "libs/mzglyphs/mzglyphs.h"


/** @brief Inicializace před každým testem (prázdná - knihovny bez stavu). */
void setUp ( void ) {
}


/** @brief Úklid po každém testu (prázdný - knihovny bez stavu). */
void tearDown ( void ) {
}


/* ========================================================================
 * mz_vcode testy
 * ======================================================================== */

/**
 * @brief Smoke: digit '0' (ASCII 0x30) -> video kód 0x20 (EU).
 *
 * Hodnota odpovídá tabulce ADCN z ROM monitoru MZ-800 (0x0A92+0x30).
 */
static void test_vcode_digit_zero_eu ( void ) {
    uint8_t code = mz_vcode_from_ascii ( '0', MZ_VCODE_EU );
    TEST_ASSERT_EQUAL_HEX8 ( 0x20, code );
}


/**
 * @brief Písmeno 'A' (ASCII 0x41) -> video kód 0x01 (EU).
 *
 * Velká písmena A-Z mají v MZ CG-ROM lineární mapování 0x01-0x1A.
 */
static void test_vcode_letter_A_eu ( void ) {
    uint8_t code = mz_vcode_from_ascii ( 'A', MZ_VCODE_EU );
    TEST_ASSERT_EQUAL_HEX8 ( 0x01, code );
}


/**
 * @brief Mezera ' ' (ASCII 0x20) -> video kód 0x00 (EU).
 */
static void test_vcode_space_eu ( void ) {
    uint8_t code = mz_vcode_from_ascii ( ' ', MZ_VCODE_EU );
    TEST_ASSERT_EQUAL_HEX8 ( 0x00, code );
}


/**
 * @brief Řídící znak 0x00 -> 0xF0 (MZ_VCODE_NO_KEY) v EU variantě.
 */
static void test_vcode_control_zero_eu ( void ) {
    uint8_t code = mz_vcode_from_ascii ( 0x00, MZ_VCODE_EU );
    TEST_ASSERT_EQUAL_HEX8 ( MZ_VCODE_NO_KEY, code );
}


/**
 * @brief JP odlišnost: ASCII 0x6C -> 0xE5 (EU), 0xE9 (JP).
 *
 * Mezi EU a JP variantou jsou odlišné jen 2 pozice (0x6C, 0x6D).
 */
static void test_vcode_jp_divergence_0x6C ( void ) {
    uint8_t eu = mz_vcode_from_ascii ( 0x6C, MZ_VCODE_EU );
    uint8_t jp = mz_vcode_from_ascii ( 0x6C, MZ_VCODE_JP );
    TEST_ASSERT_EQUAL_HEX8 ( 0xE5, eu );
    TEST_ASSERT_EQUAL_HEX8 ( 0xE9, jp );
}


/**
 * @brief JP odlišnost: ASCII 0x6D -> 0xE9 (EU), 0xEA (JP).
 */
static void test_vcode_jp_divergence_0x6D ( void ) {
    uint8_t eu = mz_vcode_from_ascii ( 0x6D, MZ_VCODE_EU );
    uint8_t jp = mz_vcode_from_ascii ( 0x6D, MZ_VCODE_JP );
    TEST_ASSERT_EQUAL_HEX8 ( 0xE9, eu );
    TEST_ASSERT_EQUAL_HEX8 ( 0xEA, jp );
}


/**
 * @brief Dump varianta: řídící znak (< 0x20) -> video kód tečky (0x2E).
 *
 * Replikuje chování příkazu D (dump) v monitoru: control bytes
 * se nahradí tečkou před převodem přes ADCN.
 */
static void test_vcode_dump_control_to_dot ( void ) {
    /* Tečka '.' (0x2E) -> video kód 0x2E (per ADCN). */
    uint8_t dot_vcode = mz_vcode_from_ascii ( '.', MZ_VCODE_EU );
    /* Control byte 0x05 v dump verzi -> stejný kód jako tečka. */
    uint8_t ctrl_dump = mz_vcode_from_ascii_dump ( 0x05, MZ_VCODE_EU );
    TEST_ASSERT_EQUAL_HEX8 ( dot_vcode, ctrl_dump );
}


/**
 * @brief Dump varianta: tisknutelný znak je nedotčen (chování = non-dump).
 */
static void test_vcode_dump_printable_passthrough ( void ) {
    uint8_t normal = mz_vcode_from_ascii ( 'X', MZ_VCODE_EU );
    uint8_t dump   = mz_vcode_from_ascii_dump ( 'X', MZ_VCODE_EU );
    TEST_ASSERT_EQUAL_HEX8 ( normal, dump );
}


/**
 * @brief Verze API vrací neprázdný statický řetězec.
 */
static void test_vcode_version_string ( void ) {
    const char *v = mz_vcode_version ();
    TEST_ASSERT_NOT_NULL ( v );
    TEST_ASSERT ( strlen ( v ) > 0 );
}


/* ========================================================================
 * mzglyphs testy
 * ======================================================================== */

/**
 * @brief Codepoint vzorec: video_code 0x00, charset EU1 -> U+E100.
 */
static void test_mzglyphs_codepoint_base_eu1 ( void ) {
    uint32_t cp = mzglyphs_get_codepoint ( 0x00, MZGLYPHS_EU1 );
    TEST_ASSERT_EQUAL_HEX32 ( 0xE100, cp );
}


/**
 * @brief Codepoint vzorec: video_code 0xFF, charset JP2 -> U+E4FF.
 *
 * Nejvyšší možný codepoint v PUA rozsahu pokrytém fontem.
 */
static void test_mzglyphs_codepoint_max_jp2 ( void ) {
    uint32_t cp = mzglyphs_get_codepoint ( 0xFF, MZGLYPHS_JP2 );
    TEST_ASSERT_EQUAL_HEX32 ( 0xE4FF, cp );
}


/**
 * @brief Codepoint vzorec: charset offset = 0x100 mezi sousedními sadami.
 */
static void test_mzglyphs_codepoint_charset_offset ( void ) {
    uint32_t eu1 = mzglyphs_get_codepoint ( 0x42, MZGLYPHS_EU1 );
    uint32_t eu2 = mzglyphs_get_codepoint ( 0x42, MZGLYPHS_EU2 );
    uint32_t jp1 = mzglyphs_get_codepoint ( 0x42, MZGLYPHS_JP1 );
    uint32_t jp2 = mzglyphs_get_codepoint ( 0x42, MZGLYPHS_JP2 );
    TEST_ASSERT_EQUAL_HEX32 ( 0xE142, eu1 );
    TEST_ASSERT_EQUAL_HEX32 ( 0xE242, eu2 );
    TEST_ASSERT_EQUAL_HEX32 ( 0xE342, jp1 );
    TEST_ASSERT_EQUAL_HEX32 ( 0xE442, jp2 );
}


/**
 * @brief UTF-8: codepoint v rozsahu U+0800-U+FFFF -> 3-byte sekvence
 *        s leading bity 1110xxxx 10xxxxxx 10xxxxxx.
 */
static void test_mzglyphs_utf8_3byte_sequence ( void ) {
    const char *utf8 = mzglyphs_to_utf8 ( 0x00, MZGLYPHS_EU1 );  /* U+E100 */
    TEST_ASSERT_NOT_NULL ( utf8 );
    TEST_ASSERT_EQUAL_size_t ( 3, strlen ( utf8 ) );
    /* U+E100 = 11100001 00000000 -> 1110 1110 10 000100 10 000000 = 0xEE 0x84 0x80 */
    TEST_ASSERT_EQUAL_HEX8 ( 0xEE, (uint8_t) utf8[0] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x84, (uint8_t) utf8[1] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x80, (uint8_t) utf8[2] );
}


/**
 * @brief UTF-8: leading bajt vždy 0xEE (PUA U+E000-U+EFFF rozsah).
 *
 * Pro celý rozsah U+E100-U+E4FF (= náš font) je vždy první bajt 0xEE.
 */
static void test_mzglyphs_utf8_leading_byte_always_0xEE ( void ) {
    const char *low  = mzglyphs_to_utf8 ( 0x00, MZGLYPHS_EU1 );
    const char *high = mzglyphs_to_utf8 ( 0xFF, MZGLYPHS_JP2 );
    TEST_ASSERT_EQUAL_HEX8 ( 0xEE, (uint8_t) low[0] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xEE, (uint8_t) high[0] );
}


/**
 * @brief Buf varianta: zapíše null-terminated UTF-8 do user bufferu.
 */
static void test_mzglyphs_to_utf8_buf ( void ) {
    char buf[MZGLYPHS_UTF8_BUF_SIZE];
    mzglyphs_to_utf8_buf ( 0x00, MZGLYPHS_EU1, buf );
    TEST_ASSERT_EQUAL_size_t ( 3, strlen ( buf ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0xEE, (uint8_t) buf[0] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x84, (uint8_t) buf[1] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x80, (uint8_t) buf[2] );
    TEST_ASSERT_EQUAL_CHAR ( '\0', buf[3] );
}


/**
 * @brief Buf a static varianta produkují identický řetězec.
 */
static void test_mzglyphs_buf_matches_static ( void ) {
    char buf[MZGLYPHS_UTF8_BUF_SIZE];
    mzglyphs_to_utf8_buf ( 0xA5, MZGLYPHS_JP1, buf );
    const char *stat = mzglyphs_to_utf8 ( 0xA5, MZGLYPHS_JP1 );
    TEST_ASSERT_EQUAL_STRING ( buf, stat );
}


/**
 * @brief Codepoint API a UTF-8 API jsou konzistentní (vzorec stejný).
 *
 * Dekódujeme UTF-8 zpět na codepoint a porovnáme s get_codepoint().
 */
static void test_mzglyphs_codepoint_utf8_roundtrip ( void ) {
    uint32_t cp = mzglyphs_get_codepoint ( 0x7B, MZGLYPHS_EU2 );
    const char *utf8 = mzglyphs_to_utf8 ( 0x7B, MZGLYPHS_EU2 );
    /* Dekódovat 3-byte UTF-8 zpět na codepoint. */
    uint32_t decoded = ( ( (uint32_t) (uint8_t) utf8[0] & 0x0F ) << 12 )
                     | ( ( (uint32_t) (uint8_t) utf8[1] & 0x3F ) <<  6 )
                     | ( ( (uint32_t) (uint8_t) utf8[2] & 0x3F )       );
    TEST_ASSERT_EQUAL_HEX32 ( cp, decoded );
}


/**
 * @brief Verze API vrací neprázdný statický řetězec.
 */
static void test_mzglyphs_version_string ( void ) {
    const char *v = mzglyphs_version ();
    TEST_ASSERT_NOT_NULL ( v );
    TEST_ASSERT ( strlen ( v ) > 0 );
}


/* ========================================================================
 * Test runner
 * ======================================================================== */

int main ( int argc, char *argv[] ) {

    (void) argc;
    (void) argv;

    UNITY_BEGIN ();

    /* mz_vcode */
    RUN_TEST ( test_vcode_digit_zero_eu );
    RUN_TEST ( test_vcode_letter_A_eu );
    RUN_TEST ( test_vcode_space_eu );
    RUN_TEST ( test_vcode_control_zero_eu );
    RUN_TEST ( test_vcode_jp_divergence_0x6C );
    RUN_TEST ( test_vcode_jp_divergence_0x6D );
    RUN_TEST ( test_vcode_dump_control_to_dot );
    RUN_TEST ( test_vcode_dump_printable_passthrough );
    RUN_TEST ( test_vcode_version_string );

    /* mzglyphs */
    RUN_TEST ( test_mzglyphs_codepoint_base_eu1 );
    RUN_TEST ( test_mzglyphs_codepoint_max_jp2 );
    RUN_TEST ( test_mzglyphs_codepoint_charset_offset );
    RUN_TEST ( test_mzglyphs_utf8_3byte_sequence );
    RUN_TEST ( test_mzglyphs_utf8_leading_byte_always_0xEE );
    RUN_TEST ( test_mzglyphs_to_utf8_buf );
    RUN_TEST ( test_mzglyphs_buf_matches_static );
    RUN_TEST ( test_mzglyphs_codepoint_utf8_roundtrip );
    RUN_TEST ( test_mzglyphs_version_string );

    return UNITY_END ();
}
