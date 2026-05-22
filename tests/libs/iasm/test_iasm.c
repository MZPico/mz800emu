/**
 * @file   test_iasm.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Testy knihovny iasm (Z80 inline assembler).
 *
 * Testuje: parsování instrukcí, vyhledání opkódů, generování binárního kódu,
 * formáty čísel, speciální instrukce (RST, IX/IY offset),
 * chybové stavy, hraniční případy.
 *
 * Standalone test — iasm je nezávislá knihovna, nepotřebuje emulátor.
 * Linkuje se pouze s Unity + iasm.o.
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

#include "unity.h"

#include <string.h>

#include "libs/iasm/iasm.h"


/** @brief Inicializace před každým testem (prázdná — iasm nemá stav). */
void setUp ( void ) {
}


/** @brief Úklid po každém testu (prázdný — iasm nemá stav). */
void tearDown ( void ) {
}


/** @defgroup iasm_test_macros Pomocná makra pro testy
 *  @{ */

/**
 * @brief Zkompiluje instrukci a ověří, že výsledek odpovídá očekávaným bajtům.
 * @param addr        Adresa instrukce (důležitá jen pro relativní skoky).
 * @param instruction Text instrukce k překladu.
 * @param ...         Očekávané bajty výstupu.
 */
#define ASSERT_ASM(addr, instruction, ...) \
    do { \
        iasm_bin_t result; \
        iasm_error_t error; \
        uint8_t expected[] = { __VA_ARGS__ }; \
        unsigned len = iasm_assemble_line ( (addr), (instruction), &result, &error ); \
        TEST_ASSERT_EQUAL_INT_MESSAGE ( sizeof(expected), len, "Špatný počet bajtů: " instruction ); \
        TEST_ASSERT_EQUAL_INT_MESSAGE ( IASM_OK, error, "Chybový kód není IASM_OK: " instruction ); \
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE ( expected, result.byte, sizeof(expected), "Špatné bajty: " instruction ); \
    } while (0)

/**
 * @brief Ověří, že instrukce vrátí chybu zadaného typu.
 * @param addr           Adresa instrukce.
 * @param instruction    Text instrukce k překladu.
 * @param expected_error Očekávaný chybový kód (iasm_error_t).
 */
#define ASSERT_ASM_ERROR(addr, instruction, expected_error) \
    do { \
        iasm_bin_t result; \
        iasm_error_t error; \
        unsigned len = iasm_assemble_line ( (addr), (instruction), &result, &error ); \
        TEST_ASSERT_EQUAL_INT_MESSAGE ( 0, len, "Měla být chyba, ale vrátilo úspěch: " instruction ); \
        TEST_ASSERT_EQUAL_INT_MESSAGE ( (expected_error), error, "Špatný chybový kód: " instruction ); \
    } while (0)


/** @} */ /* konec skupiny iasm_test_macros */

/** @defgroup iasm_test_smoke Smoke testy — základní funkčnost
 *  @{ */

/** @brief Testuje překlad jednoduché instrukce bez parametrů (NOP). */
void test_nop ( void ) {
    ASSERT_ASM ( 0x0000, "NOP", 0x00 );
}


/** @brief Testuje instrukci s registrovým parametrem (LD A,B). */
void test_ld_a_b ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,B", 0x78 );
}


/** @brief Testuje instrukci s immediate byte (LD A,0x42). */
void test_ld_a_immediate ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,0x42", 0x3E, 0x42 );
}


/** @brief Testuje instrukci s immediate word (LD HL,0x1234). */
void test_ld_hl_immediate ( void ) {
    ASSERT_ASM ( 0x0000, "LD HL,0x1234", 0x21, 0x34, 0x12 );
}


/** @brief Testuje instrukci s adresováním přes (HL). */
void test_ld_a_hl_indirect ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,(HL)", 0x7E );
}


/** @} */ /* konec skupiny iasm_test_smoke */

/** @defgroup iasm_test_singlebyte Jednobajtové instrukce
 *  @{ */

/** @brief Testuje kompletní sadu základních jednobajtových instrukcí. */
void test_single_byte_instructions ( void ) {
    ASSERT_ASM ( 0x0000, "NOP",  0x00 );
    ASSERT_ASM ( 0x0000, "HALT", 0x76 );
    ASSERT_ASM ( 0x0000, "DI",   0xF3 );
    ASSERT_ASM ( 0x0000, "EI",   0xFB );
    ASSERT_ASM ( 0x0000, "RET",  0xC9 );
    ASSERT_ASM ( 0x0000, "RLCA", 0x07 );
    ASSERT_ASM ( 0x0000, "RRCA", 0x0F );
    ASSERT_ASM ( 0x0000, "RLA",  0x17 );
    ASSERT_ASM ( 0x0000, "RRA",  0x1F );
    ASSERT_ASM ( 0x0000, "DAA",  0x27 );
    ASSERT_ASM ( 0x0000, "CPL",  0x2F );
    ASSERT_ASM ( 0x0000, "SCF",  0x37 );
    ASSERT_ASM ( 0x0000, "CCF",  0x3F );
    ASSERT_ASM ( 0x0000, "EXX",  0xD9 );
}


/** @} */ /* konec skupiny iasm_test_singlebyte */

/** @defgroup iasm_test_ld LD instrukce — registr-registr
 *  @{ */

/** @brief Testuje LD instrukce mezi všemi kombinacemi registrů A-L. */
void test_ld_register_pairs ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,A", 0x7F );
    ASSERT_ASM ( 0x0000, "LD A,B", 0x78 );
    ASSERT_ASM ( 0x0000, "LD A,C", 0x79 );
    ASSERT_ASM ( 0x0000, "LD A,D", 0x7A );
    ASSERT_ASM ( 0x0000, "LD A,E", 0x7B );
    ASSERT_ASM ( 0x0000, "LD A,H", 0x7C );
    ASSERT_ASM ( 0x0000, "LD A,L", 0x7D );

    ASSERT_ASM ( 0x0000, "LD B,A", 0x47 );
    ASSERT_ASM ( 0x0000, "LD C,A", 0x4F );
    ASSERT_ASM ( 0x0000, "LD D,A", 0x57 );
    ASSERT_ASM ( 0x0000, "LD E,A", 0x5F );
    ASSERT_ASM ( 0x0000, "LD H,A", 0x67 );
    ASSERT_ASM ( 0x0000, "LD L,A", 0x6F );
}


/** @} */ /* konec skupiny iasm_test_ld */

/** @defgroup iasm_test_numfmt Formáty čísel — hex, dec, bin
 *  @{ */

/** @brief Testuje hexadecimální formát s prefixem 0x. */
void test_hex_0x_format ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,0xFF", 0x3E, 0xFF );
    ASSERT_ASM ( 0x0000, "LD A,0x00", 0x3E, 0x00 );
    ASSERT_ASM ( 0x0000, "LD A,0x42", 0x3E, 0x42 );
}


/** @brief Testuje hexadecimální formát s prefixem #. */
void test_hex_hash_format ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,#FF", 0x3E, 0xFF );
    ASSERT_ASM ( 0x0000, "LD A,#00", 0x3E, 0x00 );
    ASSERT_ASM ( 0x0000, "LD A,#42", 0x3E, 0x42 );
}


/** @brief Testuje desítkový formát čísel. */
void test_decimal_format ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,0",   0x3E, 0x00 );
    ASSERT_ASM ( 0x0000, "LD A,255", 0x3E, 0xFF );
    ASSERT_ASM ( 0x0000, "LD A,66",  0x3E, 0x42 );
    ASSERT_ASM ( 0x0000, "LD A,100", 0x3E, 0x64 );
}


/** @brief Testuje auto-detekci hex (číslo začínající číslicí a obsahující a-f). */
void test_decimal_hex_autodetect ( void ) {
    /* "1A" se parsuje jako hex protože začíná číslicí a obsahuje 'A' */
    ASSERT_ASM ( 0x0000, "LD A,1A", 0x3E, 0x1A );
    /* "FF" začíná písmenem — parser ji nebere jako číslo, nýbrž jako parametr → chyba */
    ASSERT_ASM_ERROR ( 0x0000, "LD A,FF", IASM_ERR_INVALID_INSTRUCTION );
}


/** @brief Testuje binární formát s prefixem %. */
void test_binary_format ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,%00000000", 0x3E, 0x00 );
    ASSERT_ASM ( 0x0000, "LD A,%11111111", 0x3E, 0xFF );
    ASSERT_ASM ( 0x0000, "LD A,%01010101", 0x3E, 0x55 );
    ASSERT_ASM ( 0x0000, "LD A,%10101010", 0x3E, 0xAA );
}


/** @} */ /* konec skupiny iasm_test_numfmt */

/** @defgroup iasm_test_16bit 16-bit immediate (word, little-endian)
 *  @{ */

/** @brief Testuje 16-bit immediate hodnoty s little-endian uložením. */
void test_16bit_immediate ( void ) {
    /* LD BC,0x1234 → 01 34 12 */
    ASSERT_ASM ( 0x0000, "LD BC,0x1234", 0x01, 0x34, 0x12 );
    /* LD DE,0xABCD → 11 CD AB */
    ASSERT_ASM ( 0x0000, "LD DE,0xABCD", 0x11, 0xCD, 0xAB );
    /* LD SP,0x0000 → 31 00 00 */
    ASSERT_ASM ( 0x0000, "LD SP,0x0000", 0x31, 0x00, 0x00 );
    /* LD HL,0xFFFF → 21 FF FF */
    ASSERT_ASM ( 0x0000, "LD HL,0xFFFF", 0x21, 0xFF, 0xFF );
}


/** @} */ /* konec skupiny iasm_test_16bit */

/** @defgroup iasm_test_ixiy IX/IY instrukce
 *  @{ */

/** @brief Testuje IX instrukce s kladným offsetem. */
void test_ix_positive_offset ( void ) {
    /* LD A,(IX+0x05) → DD 05 7E */
    ASSERT_ASM ( 0x0000, "LD A,(IX+0x05)", 0xDD, 0x05, 0x7E );
    /* LD (IX+0x10),A → DD 10 77 */
    ASSERT_ASM ( 0x0000, "LD (IX+0x10),A", 0xDD, 0x10, 0x77 );
}


/** @brief Testuje IY instrukce s kladným offsetem. */
void test_iy_positive_offset ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,(IY+0x05)", 0xFD, 0x05, 0x7E );
}


/** @brief Testuje IX instrukce se záporným offsetem (převod na doplněk). */
void test_ix_negative_offset ( void ) {
    /* LD A,(IX-5) → offset = 0x100 - 5 = 0xFB */
    ASSERT_ASM ( 0x0000, "LD A,(IX-5)", 0xDD, 0xFB, 0x7E );
    /* LD A,(IX-1) → offset = 0xFF */
    ASSERT_ASM ( 0x0000, "LD A,(IX-1)", 0xDD, 0xFF, 0x7E );
    /* LD A,(IX-127) → offset = 0x81 */
    ASSERT_ASM ( 0x0000, "LD A,(IX-127)", 0xDD, 0x81, 0x7E );
}


/** @brief Testuje IY instrukce se záporným offsetem. */
void test_iy_negative_offset ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,(IY-5)", 0xFD, 0xFB, 0x7E );
}


/** @brief Testuje IX instrukce s nulovým offsetem. */
void test_ix_zero_offset ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,(IX+0)", 0xDD, 0x00, 0x7E );
}


/** @brief Testuje LD (IX+d),n — instrukce se dvěma proměnnými. */
void test_ix_load_immediate ( void ) {
    /* LD (IX+0x05),0x42 → DD 36 05 42 (opkód "DD36$#") */
    ASSERT_ASM ( 0x0000, "LD (IX+0x05),0x42", 0xDD, 0x36, 0x05, 0x42 );
}


/** @} */ /* konec skupiny iasm_test_ixiy */

/** @defgroup iasm_test_reljmp Relativní skoky (JR, DJNZ)
 *  @{ */

/** @brief Testuje JR skok dopředu. */
void test_jr_forward ( void ) {
    /* JR na adresu 0x0005, z adresy 0x0000: offset = 0x05 - (0x00 + 2) = 0x03 */
    ASSERT_ASM ( 0x0000, "JR 0x0005", 0x18, 0x03 );
}


/** @brief Testuje JR skok dozadu. */
void test_jr_backward ( void ) {
    /* JR na adresu 0x0000, z adresy 0x0010: offset = 0x00 - (0x10 + 2) = -18 = 0xEE */
    ASSERT_ASM ( 0x0010, "JR 0x0000", 0x18, 0xEE );
}


/** @brief Testuje podmíněné JR skoky (Z, NZ, C, NC). */
void test_jr_conditional ( void ) {
    /* JR Z,0x0005 z adresy 0x0000 → offset = 3 */
    ASSERT_ASM ( 0x0000, "JR Z,0x0005", 0x28, 0x03 );
    /* JR NZ,0x0005 z adresy 0x0000 → offset = 3 */
    ASSERT_ASM ( 0x0000, "JR NZ,0x0005", 0x20, 0x03 );
    /* JR C,0x0005 z adresy 0x0000 → offset = 3 */
    ASSERT_ASM ( 0x0000, "JR C,0x0005", 0x38, 0x03 );
    /* JR NC,0x0005 z adresy 0x0000 → offset = 3 */
    ASSERT_ASM ( 0x0000, "JR NC,0x0005", 0x30, 0x03 );
}


/** @brief Testuje instrukci DJNZ (dekrementuj a skoč). */
void test_djnz ( void ) {
    /* DJNZ na adresu 0x0000, z adresy 0x0010: offset = 0x00 - (0x10 + 2) = -18 = 0xEE */
    ASSERT_ASM ( 0x0010, "DJNZ 0x0000", 0x10, 0xEE );
}


/** @} */ /* konec skupiny iasm_test_reljmp */

/** @defgroup iasm_test_rst RST instrukce — korekce dekadických adres
 *  @{ */

/** @brief Testuje RST instrukce s hexadecimálními adresami. */
void test_rst_hex ( void ) {
    ASSERT_ASM ( 0x0000, "RST 0x00", 0xC7 );
    ASSERT_ASM ( 0x0000, "RST 0x08", 0xCF );
    ASSERT_ASM ( 0x0000, "RST 0x10", 0xD7 );
    ASSERT_ASM ( 0x0000, "RST 0x18", 0xDF );
    ASSERT_ASM ( 0x0000, "RST 0x20", 0xE7 );
    ASSERT_ASM ( 0x0000, "RST 0x28", 0xEF );
    ASSERT_ASM ( 0x0000, "RST 0x30", 0xF7 );
    ASSERT_ASM ( 0x0000, "RST 0x38", 0xFF );
}


/** @brief Testuje RST s dekadickými čísly a automatickou korekcí (RST 10 -> RST 0x10). */
void test_rst_decimal_correction ( void ) {
    ASSERT_ASM ( 0x0000, "RST 10", 0xD7 );
    ASSERT_ASM ( 0x0000, "RST 18", 0xDF );
    ASSERT_ASM ( 0x0000, "RST 20", 0xE7 );
    ASSERT_ASM ( 0x0000, "RST 28", 0xEF );
    ASSERT_ASM ( 0x0000, "RST 30", 0xF7 );
    ASSERT_ASM ( 0x0000, "RST 38", 0xFF );
}


/** @} */ /* konec skupiny iasm_test_rst */

/** @defgroup iasm_test_alu Aritmetické a logické instrukce
 *  @{ */

/** @brief Testuje 8-bit aritmetické a logické instrukce (ADD, ADC, SUB, SBC, AND, XOR, OR, CP, INC, DEC). */
void test_arithmetic_instructions ( void ) {
    ASSERT_ASM ( 0x0000, "ADD A,B",    0x80 );
    ASSERT_ASM ( 0x0000, "ADC A,B",    0x88 );
    ASSERT_ASM ( 0x0000, "SUB B",      0x90 );
    ASSERT_ASM ( 0x0000, "SBC A,B",    0x98 );
    ASSERT_ASM ( 0x0000, "AND B",      0xA0 );
    ASSERT_ASM ( 0x0000, "XOR B",      0xA8 );
    ASSERT_ASM ( 0x0000, "OR B",       0xB0 );
    ASSERT_ASM ( 0x0000, "CP B",       0xB8 );
    ASSERT_ASM ( 0x0000, "INC A",      0x3C );
    ASSERT_ASM ( 0x0000, "DEC A",      0x3D );
    ASSERT_ASM ( 0x0000, "ADD A,0x42", 0xC6, 0x42 );
    ASSERT_ASM ( 0x0000, "SUB 0x42",   0xD6, 0x42 );
    ASSERT_ASM ( 0x0000, "CP 0x42",    0xFE, 0x42 );
}


/** @brief Testuje 16-bit aritmetické instrukce (ADD HL, INC/DEC párové). */
void test_16bit_arithmetic ( void ) {
    ASSERT_ASM ( 0x0000, "ADD HL,BC", 0x09 );
    ASSERT_ASM ( 0x0000, "ADD HL,DE", 0x19 );
    ASSERT_ASM ( 0x0000, "ADD HL,HL", 0x29 );
    ASSERT_ASM ( 0x0000, "ADD HL,SP", 0x39 );
    ASSERT_ASM ( 0x0000, "INC BC",    0x03 );
    ASSERT_ASM ( 0x0000, "DEC BC",    0x0B );
    ASSERT_ASM ( 0x0000, "INC HL",    0x23 );
    ASSERT_ASM ( 0x0000, "DEC HL",    0x2B );
}


/** @} */ /* konec skupiny iasm_test_alu */

/** @defgroup iasm_test_stack Stack instrukce
 *  @{ */

/** @brief Testuje PUSH a POP instrukce pro všechny párové registry. */
void test_push_pop ( void ) {
    ASSERT_ASM ( 0x0000, "PUSH AF", 0xF5 );
    ASSERT_ASM ( 0x0000, "PUSH BC", 0xC5 );
    ASSERT_ASM ( 0x0000, "PUSH DE", 0xD5 );
    ASSERT_ASM ( 0x0000, "PUSH HL", 0xE5 );
    ASSERT_ASM ( 0x0000, "POP AF",  0xF1 );
    ASSERT_ASM ( 0x0000, "POP BC",  0xC1 );
    ASSERT_ASM ( 0x0000, "POP DE",  0xD1 );
    ASSERT_ASM ( 0x0000, "POP HL",  0xE1 );
}


/** @} */ /* konec skupiny iasm_test_stack */

/** @defgroup iasm_test_jpcall JP a CALL instrukce
 *  @{ */

/** @brief Testuje JP a CALL instrukce (podmíněné i nepodmíněné). */
void test_jp_call ( void ) {
    ASSERT_ASM ( 0x0000, "JP 0x1234",    0xC3, 0x34, 0x12 );
    ASSERT_ASM ( 0x0000, "JP Z,0x1234",  0xCA, 0x34, 0x12 );
    ASSERT_ASM ( 0x0000, "JP NZ,0x1234", 0xC2, 0x34, 0x12 );
    ASSERT_ASM ( 0x0000, "JP HL",        0xE9 );

    ASSERT_ASM ( 0x0000, "CALL 0x1234",    0xCD, 0x34, 0x12 );
    ASSERT_ASM ( 0x0000, "CALL Z,0x1234",  0xCC, 0x34, 0x12 );
    ASSERT_ASM ( 0x0000, "CALL NZ,0x1234", 0xC4, 0x34, 0x12 );
}


/** @brief Testuje podmíněné návraty (RET Z, RET NZ, RET C, ...). */
void test_conditional_returns ( void ) {
    ASSERT_ASM ( 0x0000, "RET Z",  0xC8 );
    ASSERT_ASM ( 0x0000, "RET NZ", 0xC0 );
    ASSERT_ASM ( 0x0000, "RET C",  0xD8 );
    ASSERT_ASM ( 0x0000, "RET NC", 0xD0 );
    ASSERT_ASM ( 0x0000, "RET PE", 0xE8 );
    ASSERT_ASM ( 0x0000, "RET PO", 0xE0 );
    ASSERT_ASM ( 0x0000, "RET P",  0xF0 );
    ASSERT_ASM ( 0x0000, "RET M",  0xF8 );
}


/** @} */ /* konec skupiny iasm_test_jpcall */

/** @defgroup iasm_test_ed ED-prefixed instrukce
 *  @{ */

/** @brief Testuje ED-prefixed instrukce (NEG, RETI, RETN, IM, LDI/LDIR/LDD/LDDR, CPI/CPIR). */
void test_ed_prefixed ( void ) {
    ASSERT_ASM ( 0x0000, "NEG",  0xED, 0x44 );
    ASSERT_ASM ( 0x0000, "RETI", 0xED, 0x4D );
    ASSERT_ASM ( 0x0000, "RETN", 0xED, 0x45 );
    ASSERT_ASM ( 0x0000, "IM 0", 0xED, 0x46 );
    ASSERT_ASM ( 0x0000, "IM 1", 0xED, 0x56 );
    ASSERT_ASM ( 0x0000, "IM 2", 0xED, 0x5E );
    ASSERT_ASM ( 0x0000, "LDI",  0xED, 0xA0 );
    ASSERT_ASM ( 0x0000, "LDIR", 0xED, 0xB0 );
    ASSERT_ASM ( 0x0000, "LDD",  0xED, 0xA8 );
    ASSERT_ASM ( 0x0000, "LDDR", 0xED, 0xB8 );
    ASSERT_ASM ( 0x0000, "CPI",  0xED, 0xA1 );
    ASSERT_ASM ( 0x0000, "CPIR", 0xED, 0xB1 );
}


/* ========================================================================
 * BIT, SET, RES INSTRUKCE
 * ======================================================================== */


void test_bit_operations ( void ) {
    ASSERT_ASM ( 0x0000, "BIT 0,A", 0xCB, 0x47 );
    ASSERT_ASM ( 0x0000, "BIT 7,A", 0xCB, 0x7F );
    ASSERT_ASM ( 0x0000, "BIT 3,B", 0xCB, 0x58 );
    ASSERT_ASM ( 0x0000, "SET 0,A", 0xCB, 0xC7 );
    ASSERT_ASM ( 0x0000, "SET 7,A", 0xCB, 0xFF );
    ASSERT_ASM ( 0x0000, "RES 0,A", 0xCB, 0x87 );
    ASSERT_ASM ( 0x0000, "RES 7,A", 0xCB, 0xBF );
}


/* ========================================================================
 * ROTACE A POSUVY (CB-prefixed)
 * ======================================================================== */


void test_rotation_instructions ( void ) {
    ASSERT_ASM ( 0x0000, "RLC A",  0xCB, 0x07 );
    ASSERT_ASM ( 0x0000, "RRC A",  0xCB, 0x0F );
    ASSERT_ASM ( 0x0000, "RL A",   0xCB, 0x17 );
    ASSERT_ASM ( 0x0000, "RR A",   0xCB, 0x1F );
    ASSERT_ASM ( 0x0000, "SLA A",  0xCB, 0x27 );
    ASSERT_ASM ( 0x0000, "SRA A",  0xCB, 0x2F );
    ASSERT_ASM ( 0x0000, "SRL A",  0xCB, 0x3F );
}


/* ========================================================================
 * I/O INSTRUKCE
 * ======================================================================== */


void test_io_instructions ( void ) {
    ASSERT_ASM ( 0x0000, "IN A,(0x42)",  0xDB, 0x42 );
    ASSERT_ASM ( 0x0000, "OUT (0x42),A", 0xD3, 0x42 );
    ASSERT_ASM ( 0x0000, "IN A,(C)",     0xED, 0x78 );
    ASSERT_ASM ( 0x0000, "OUT (C),A",    0xED, 0x79 );
}


/* ========================================================================
 * EX INSTRUKCE
 * ======================================================================== */


void test_exchange_instructions ( void ) {
    ASSERT_ASM ( 0x0000, "EX AF,AF'", 0x08 );
    ASSERT_ASM ( 0x0000, "EX DE,HL",  0xEB );
    ASSERT_ASM ( 0x0000, "EX (SP),HL", 0xE3 );
}


/* ========================================================================
 * LD S NEPŘÍMOU ADRESACÍ (16-bit adresy)
 * ======================================================================== */


void test_ld_indirect_address ( void ) {
    /* LD A,(0x1234) → 3A 34 12 */
    ASSERT_ASM ( 0x0000, "LD A,(0x1234)",    0x3A, 0x34, 0x12 );
    /* LD (0x1234),A → 32 34 12 */
    ASSERT_ASM ( 0x0000, "LD (0x1234),A",    0x32, 0x34, 0x12 );
    /* LD HL,(0x1234) → 2A 34 12 */
    ASSERT_ASM ( 0x0000, "LD HL,(0x1234)",   0x2A, 0x34, 0x12 );
    /* LD (0x1234),HL → 22 34 12 */
    ASSERT_ASM ( 0x0000, "LD (0x1234),HL",   0x22, 0x34, 0x12 );
}


/* ========================================================================
 * CASE-INSENSITIVITY — malá/velká písmena
 * ======================================================================== */


void test_case_insensitive ( void ) {
    ASSERT_ASM ( 0x0000, "nop",       0x00 );
    ASSERT_ASM ( 0x0000, "ld a,b",    0x78 );
    ASSERT_ASM ( 0x0000, "Ld A,b",    0x78 );
    ASSERT_ASM ( 0x0000, "LD a,B",    0x78 );
    ASSERT_ASM ( 0x0000, "ld l,(ix+0x05)", 0xDD, 0x05, 0x6E );
}


/* ========================================================================
 * ZPRACOVÁNÍ MEZER — na začátku, na konci, kolem čárky
 * ======================================================================== */


void test_whitespace_handling ( void ) {
    /* mezery na začátku a konci */
    ASSERT_ASM ( 0x0000, "  NOP  ",    0x00 );
    ASSERT_ASM ( 0x0000, "  LD A,B  ", 0x78 );

    /* mezery kolem čárky se ignorují */
    ASSERT_ASM ( 0x0000, "LD A, B",  0x78 );
    ASSERT_ASM ( 0x0000, "LD A , B", 0x78 );
}


/* ========================================================================
 * CHYBOVÉ STAVY
 * ======================================================================== */


/* neplatná instrukce */
void test_error_invalid_instruction ( void ) {
    ASSERT_ASM_ERROR ( 0x0000, "BLAH",        IASM_ERR_INVALID_INSTRUCTION );
    ASSERT_ASM_ERROR ( 0x0000, "LD X,Y",      IASM_ERR_INVALID_INSTRUCTION );
    ASSERT_ASM_ERROR ( 0x0000, "LD A,QWERTY", IASM_ERR_INVALID_INSTRUCTION );
}


/* byte hodnota mimo rozsah (> 0xFF) */
void test_error_byte_out_of_range ( void ) {
    ASSERT_ASM_ERROR ( 0x0000, "LD A,0x100",  IASM_ERR_VALUE_OUT_OF_RANGE );
    ASSERT_ASM_ERROR ( 0x0000, "LD A,256",    IASM_ERR_VALUE_OUT_OF_RANGE );
    ASSERT_ASM_ERROR ( 0x0000, "LD A,0xFFF",  IASM_ERR_VALUE_OUT_OF_RANGE );
}


/* relativní skok mimo rozsah */
void test_error_relative_jump_out_of_range ( void ) {
    /* JR z adresy 0x0000 na 0x0082: offset = 0x82 - 2 = 0x80 = 128, to je mimo rozsah (-128..127) */
    ASSERT_ASM_ERROR ( 0x0000, "JR 0x0082", IASM_ERR_VALUE_OUT_OF_RANGE );
}


/* prázdný vstup */
void test_error_empty_input ( void ) {
    ASSERT_ASM_ERROR ( 0x0000, "",   IASM_ERR_INVALID_INSTRUCTION );
    ASSERT_ASM_ERROR ( 0x0000, "  ", IASM_ERR_INVALID_INSTRUCTION );
}


/* error parametr může být NULL */
void test_error_null_pointer ( void ) {
    iasm_bin_t result;
    unsigned len;

    /* úspěšná kompilace s error=NULL */
    len = iasm_assemble_line ( 0x0000, "NOP", &result, NULL );
    TEST_ASSERT_EQUAL_INT ( 1, len );

    /* neúspěšná kompilace s error=NULL — nesmí crashnout */
    len = iasm_assemble_line ( 0x0000, "BLAH", &result, NULL );
    TEST_ASSERT_EQUAL_INT ( 0, len );
}


/* ========================================================================
 * IX INSTRUKCE — ADC, ADD, SUB, CP, ... s IX offsetem
 * ======================================================================== */


void test_ix_arithmetic ( void ) {
    ASSERT_ASM ( 0x0000, "ADD A,(IX+0x05)", 0xDD, 0x05, 0x86 );
    ASSERT_ASM ( 0x0000, "ADC A,(IX+0x05)", 0xDD, 0x05, 0x8E );
    ASSERT_ASM ( 0x0000, "SUB (IX+0x05)",   0xDD, 0x05, 0x96 );
    ASSERT_ASM ( 0x0000, "CP (IX+0x05)",    0xDD, 0x05, 0xBE );
    ASSERT_ASM ( 0x0000, "INC (IX+0x05)",   0xDD, 0x05, 0x34 );
    ASSERT_ASM ( 0x0000, "DEC (IX+0x05)",   0xDD, 0x05, 0x35 );
}


/* ========================================================================
 * IX/IY 16-BIT OPERACE
 * ======================================================================== */


void test_ix_iy_16bit ( void ) {
    ASSERT_ASM ( 0x0000, "ADD IX,BC",    0xDD, 0x09 );
    ASSERT_ASM ( 0x0000, "LD IX,0x1234", 0xDD, 0x21, 0x34, 0x12 );
    ASSERT_ASM ( 0x0000, "PUSH IX",      0xDD, 0xE5 );
    ASSERT_ASM ( 0x0000, "POP IX",       0xDD, 0xE1 );
    ASSERT_ASM ( 0x0000, "INC IX",       0xDD, 0x23 );
    ASSERT_ASM ( 0x0000, "DEC IX",       0xDD, 0x2B );

    ASSERT_ASM ( 0x0000, "ADD IY,BC",    0xFD, 0x09 );
    ASSERT_ASM ( 0x0000, "LD IY,0x1234", 0xFD, 0x21, 0x34, 0x12 );
    ASSERT_ASM ( 0x0000, "PUSH IY",      0xFD, 0xE5 );
    ASSERT_ASM ( 0x0000, "POP IY",       0xFD, 0xE1 );
}


/* ========================================================================
 * UNDOCUMENTED: LD r,OP (IX+d) — rotace/bitové operace s IX
 * ======================================================================== */


void test_undocumented_ld_rot_ix ( void ) {
    /* LD A,RL (IX+0x05) */
    ASSERT_ASM ( 0x0000, "LD A,RL (IX+0x05)", 0xDD, 0xCB, 0x05, 0x17 );
    /* LD A,RR (IX+0x05) */
    ASSERT_ASM ( 0x0000, "LD A,RR (IX+0x05)", 0xDD, 0xCB, 0x05, 0x1F );
    /* LD A,RLC (IX+0x05) */
    ASSERT_ASM ( 0x0000, "LD A,RLC (IX+0x05)", 0xDD, 0xCB, 0x05, 0x07 );
    /* LD A,SLA (IX+0x05) */
    ASSERT_ASM ( 0x0000, "LD A,SLA (IX+0x05)", 0xDD, 0xCB, 0x05, 0x27 );
}


/* ========================================================================
 * UNDOCUMENTED: LD r,SET/RES n,(IX+d)
 * ======================================================================== */


void test_undocumented_ld_set_res_ix ( void ) {
    /* LD A,SET 7,(IX+0x05) */
    ASSERT_ASM ( 0x0000, "LD A,SET 7,(IX+0x05)", 0xDD, 0xCB, 0x05, 0xFF );
    /* LD A,RES 0,(IX+0x05) */
    ASSERT_ASM ( 0x0000, "LD A,RES 0,(IX+0x05)", 0xDD, 0xCB, 0x05, 0x87 );
}


/* ========================================================================
 * BIT OPERACE S IX/IY
 * ======================================================================== */


void test_bit_ix_iy ( void ) {
    /* BIT 5,(IX+0x05) — opkód "DDCB$6D" → DD CB 05 6D */
    ASSERT_ASM ( 0x0000, "BIT 5,(IX+0x05)", 0xDD, 0xCB, 0x05, 0x6D );
    /* SET 3,(IX+0x05) — opkód "DDCB$DE" → DD CB 05 DE */
    ASSERT_ASM ( 0x0000, "SET 3,(IX+0x05)", 0xDD, 0xCB, 0x05, 0xDE );
    /* RES 1,(IY+0x05) — opkód "FDCB$8E" → FD CB 05 8E */
    ASSERT_ASM ( 0x0000, "RES 1,(IY+0x05)", 0xFD, 0xCB, 0x05, 0x8E );
}


/* ========================================================================
 * ROTACE S IX/IY
 * ======================================================================== */


void test_rotation_ix_iy ( void ) {
    /* RLC (IX+0x05) */
    ASSERT_ASM ( 0x0000, "RLC (IX+0x05)", 0xDD, 0xCB, 0x05, 0x06 );
    /* RL (IX+0x05) */
    ASSERT_ASM ( 0x0000, "RL (IX+0x05)", 0xDD, 0xCB, 0x05, 0x16 );
    /* SRL (IY+0x05) */
    ASSERT_ASM ( 0x0000, "SRL (IY+0x05)", 0xFD, 0xCB, 0x05, 0x3E );
}


/* ========================================================================
 * UNDOCUMENTED: IXH, IXL, IYH, IYL registry
 * ======================================================================== */


void test_undocumented_ix_iy_halves ( void ) {
    ASSERT_ASM ( 0x0000, "LD A,IXH", 0xDD, 0x7C );
    ASSERT_ASM ( 0x0000, "LD A,IXL", 0xDD, 0x7D );
    ASSERT_ASM ( 0x0000, "LD A,IYH", 0xFD, 0x7C );
    ASSERT_ASM ( 0x0000, "LD A,IYL", 0xFD, 0x7D );

    ASSERT_ASM ( 0x0000, "ADD A,IXH", 0xDD, 0x84 );
    ASSERT_ASM ( 0x0000, "ADD A,IXL", 0xDD, 0x85 );
}


/* ========================================================================
 * UNDOCUMENTED: SLL instrukce
 * ======================================================================== */


void test_undocumented_sll ( void ) {
    ASSERT_ASM ( 0x0000, "SLL A", 0xCB, 0x37 );
    ASSERT_ASM ( 0x0000, "SLL B", 0xCB, 0x30 );
}


/* ========================================================================
 * HLAVNÍ FUNKCE
 * ======================================================================== */


int main ( int argc, char *argv[] ) {

    (void) argc;
    (void) argv;

    UNITY_BEGIN ();

    /* smoke — základní funkčnost */
    RUN_TEST ( test_nop );
    RUN_TEST ( test_ld_a_b );
    RUN_TEST ( test_ld_a_immediate );
    RUN_TEST ( test_ld_hl_immediate );
    RUN_TEST ( test_ld_a_hl_indirect );

    /* jednobajtové instrukce */
    RUN_TEST ( test_single_byte_instructions );

    /* LD registr-registr */
    RUN_TEST ( test_ld_register_pairs );

    /* formáty čísel */
    RUN_TEST ( test_hex_0x_format );
    RUN_TEST ( test_hex_hash_format );
    RUN_TEST ( test_decimal_format );
    RUN_TEST ( test_decimal_hex_autodetect );
    RUN_TEST ( test_binary_format );

    /* 16-bit immediate */
    RUN_TEST ( test_16bit_immediate );

    /* IX/IY instrukce */
    RUN_TEST ( test_ix_positive_offset );
    RUN_TEST ( test_iy_positive_offset );
    RUN_TEST ( test_ix_negative_offset );
    RUN_TEST ( test_iy_negative_offset );
    RUN_TEST ( test_ix_zero_offset );
    RUN_TEST ( test_ix_load_immediate );

    /* relativní skoky */
    RUN_TEST ( test_jr_forward );
    RUN_TEST ( test_jr_backward );
    RUN_TEST ( test_jr_conditional );
    RUN_TEST ( test_djnz );

    /* RST */
    RUN_TEST ( test_rst_hex );
    RUN_TEST ( test_rst_decimal_correction );

    /* aritmetické/logické instrukce */
    RUN_TEST ( test_arithmetic_instructions );
    RUN_TEST ( test_16bit_arithmetic );

    /* stack */
    RUN_TEST ( test_push_pop );

    /* JP/CALL */
    RUN_TEST ( test_jp_call );
    RUN_TEST ( test_conditional_returns );

    /* ED-prefixed */
    RUN_TEST ( test_ed_prefixed );

    /* bit operace */
    RUN_TEST ( test_bit_operations );
    RUN_TEST ( test_rotation_instructions );

    /* I/O */
    RUN_TEST ( test_io_instructions );

    /* EX */
    RUN_TEST ( test_exchange_instructions );

    /* LD nepřímé */
    RUN_TEST ( test_ld_indirect_address );

    /* case-insensitivity */
    RUN_TEST ( test_case_insensitive );

    /* zpracování mezer */
    RUN_TEST ( test_whitespace_handling );

    /* chybové stavy */
    RUN_TEST ( test_error_invalid_instruction );
    RUN_TEST ( test_error_byte_out_of_range );
    RUN_TEST ( test_error_relative_jump_out_of_range );
    RUN_TEST ( test_error_empty_input );
    RUN_TEST ( test_error_null_pointer );

    /* IX aritmetika */
    RUN_TEST ( test_ix_arithmetic );

    /* IX/IY 16-bit operace */
    RUN_TEST ( test_ix_iy_16bit );

    /* undocumented instrukce */
    RUN_TEST ( test_undocumented_ld_rot_ix );
    RUN_TEST ( test_undocumented_ld_set_res_ix );
    RUN_TEST ( test_bit_ix_iy );
    RUN_TEST ( test_rotation_ix_iy );
    RUN_TEST ( test_undocumented_ix_iy_halves );
    RUN_TEST ( test_undocumented_sll );

    return UNITY_END ();
}
