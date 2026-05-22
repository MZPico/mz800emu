/*
 * test_wd279x.c - unit testy pro public lookup API WD279x chip vrstvy.
 *
 * Pokrývá wd279x_decode_command_type() a wd279x_command_type_name(). Tyto
 * funkce jsou čisté lookup (žádný chip state), tedy testovatelné bez
 * mztest_init() emu inicializace.
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <string.h>

#include "hw-generic/fdc/wd279x.h"

void setUp ( void ) { }
void tearDown ( void ) { }


/* ================================================================
 * Decoder top-nibble pokrytí - každý opcode rozsah mapovaný na enum.
 * ================================================================ */

void test_wd279x_decode_command_type_type1 ( void )
{
    /* Type I commands: Restore / Seek / Step (3 varianty - každá s/bez
     * update bit 4). */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_RESTORE,
                             wd279x_decode_command_type ( 0x00 ) );
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_RESTORE,
                             wd279x_decode_command_type ( 0x0F ) );

    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_SEEK,
                             wd279x_decode_command_type ( 0x10 ) );
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_SEEK,
                             wd279x_decode_command_type ( 0x1F ) );

    /* Step bez update: top nibble 2 nebo 3, bit 4 (= 0x10) = 0 */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_STEP,
                             wd279x_decode_command_type ( 0x20 ) );
    /* Step s update: bit 4 = 1 */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_STEP_UPDATE,
                             wd279x_decode_command_type ( 0x30 ) );

    /* Step In - bez/s update */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_STEP_IN,
                             wd279x_decode_command_type ( 0x40 ) );
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_STEP_IN_UPDATE,
                             wd279x_decode_command_type ( 0x50 ) );

    /* Step Out - bez/s update */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_STEP_OUT,
                             wd279x_decode_command_type ( 0x60 ) );
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_STEP_OUT_UPDATE,
                             wd279x_decode_command_type ( 0x70 ) );
}


void test_wd279x_decode_command_type_type2_3 ( void )
{
    /* Read Sector single (0x80) i multi (0x90) -> stejný typ. */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_READ_SECTOR,
                             wd279x_decode_command_type ( 0x80 ) );
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_READ_SECTOR,
                             wd279x_decode_command_type ( 0x90 ) );

    /* Write Sector single (0xA0) i multi (0xB0). */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_WRITE_SECTOR,
                             wd279x_decode_command_type ( 0xA0 ) );
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_WRITE_SECTOR,
                             wd279x_decode_command_type ( 0xB0 ) );

    /* Read Address (0xC0..0xCF). */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_READ_ADDRESS,
                             wd279x_decode_command_type ( 0xC0 ) );
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_READ_ADDRESS,
                             wd279x_decode_command_type ( 0xCF ) );

    /* Read Track (0xE0..0xEF). */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_READ_TRACK,
                             wd279x_decode_command_type ( 0xE0 ) );

    /* Write Track (0xF0..0xFF). */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_WRITE_TRACK,
                             wd279x_decode_command_type ( 0xF0 ) );
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_WRITE_TRACK,
                             wd279x_decode_command_type ( 0xFF ) );
}


void test_wd279x_decode_command_type_force_interrupt ( void )
{
    /* Force Interrupt 0xD0..0xDF - všechno mapuje na FORCE_INTERRUPT. */
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_FORCE_INTERRUPT,
                             wd279x_decode_command_type ( 0xD0 ) );
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_FORCE_INTERRUPT,
                             wd279x_decode_command_type ( 0xD8 ) );
    TEST_ASSERT_EQUAL_INT ( WD279X_CMD_FORCE_INTERRUPT,
                             wd279x_decode_command_type ( 0xDF ) );
}


/* ================================================================
 * Lidsky čitelné jméno - každá enum hodnota má unikátní string.
 * ================================================================ */

void test_wd279x_command_type_name_known ( void )
{
    TEST_ASSERT_EQUAL_STRING ( "Restore",
                                wd279x_command_type_name ( WD279X_CMD_RESTORE ) );
    TEST_ASSERT_EQUAL_STRING ( "Seek",
                                wd279x_command_type_name ( WD279X_CMD_SEEK ) );
    TEST_ASSERT_EQUAL_STRING ( "Step",
                                wd279x_command_type_name ( WD279X_CMD_STEP ) );
    TEST_ASSERT_EQUAL_STRING ( "Step+U",
                                wd279x_command_type_name ( WD279X_CMD_STEP_UPDATE ) );
    TEST_ASSERT_EQUAL_STRING ( "Step In",
                                wd279x_command_type_name ( WD279X_CMD_STEP_IN ) );
    TEST_ASSERT_EQUAL_STRING ( "Step In+U",
                                wd279x_command_type_name ( WD279X_CMD_STEP_IN_UPDATE ) );
    TEST_ASSERT_EQUAL_STRING ( "Step Out",
                                wd279x_command_type_name ( WD279X_CMD_STEP_OUT ) );
    TEST_ASSERT_EQUAL_STRING ( "Step Out+U",
                                wd279x_command_type_name ( WD279X_CMD_STEP_OUT_UPDATE ) );
    TEST_ASSERT_EQUAL_STRING ( "Read Sector",
                                wd279x_command_type_name ( WD279X_CMD_READ_SECTOR ) );
    TEST_ASSERT_EQUAL_STRING ( "Write Sector",
                                wd279x_command_type_name ( WD279X_CMD_WRITE_SECTOR ) );
    TEST_ASSERT_EQUAL_STRING ( "Read Address",
                                wd279x_command_type_name ( WD279X_CMD_READ_ADDRESS ) );
    TEST_ASSERT_EQUAL_STRING ( "Read Track",
                                wd279x_command_type_name ( WD279X_CMD_READ_TRACK ) );
    TEST_ASSERT_EQUAL_STRING ( "Write Track",
                                wd279x_command_type_name ( WD279X_CMD_WRITE_TRACK ) );
    TEST_ASSERT_EQUAL_STRING ( "Force INT",
                                wd279x_command_type_name ( WD279X_CMD_FORCE_INTERRUPT ) );
}


void test_wd279x_command_type_name_unknown ( void )
{
    /* Explicit UNKNOWN enum hodnota. */
    TEST_ASSERT_EQUAL_STRING ( "Unknown",
                                wd279x_command_type_name ( WD279X_CMD_UNKNOWN ) );
    /* Hodnota mimo enum range -> fallback default branch. */
    TEST_ASSERT_EQUAL_STRING ( "Unknown",
                                wd279x_command_type_name (
                                    (en_WD279X_COMMAND_TYPE) 0x55 ) );
}


/* === MAIN === */

int main ( int argc, char *argv[] )
{
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    RUN_TEST ( test_wd279x_decode_command_type_type1 );
    RUN_TEST ( test_wd279x_decode_command_type_type2_3 );
    RUN_TEST ( test_wd279x_decode_command_type_force_interrupt );
    RUN_TEST ( test_wd279x_command_type_name_known );
    RUN_TEST ( test_wd279x_command_type_name_unknown );

    int result = UNITY_END ( );
    mztest_teardown ( );
    return result;
}
