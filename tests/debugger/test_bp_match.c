/*
 * test_bp_match.c - testy match helperů V1.5.E (SINGLE / RANGE / MASK)
 *
 * Pokrytí:
 *   - bp_match16(mode, x, ref, end, mask) - 16-bit match (PC, MEM addr, port)
 *   - bp_match8(mode, x, ref, end, mask)  - 8-bit match (bank ID)
 *   - bp_match_mode_to_string / _from_string
 *
 * Pure function testy - nepotřebují žádný globální state ani emu init.
 * Pro konzistenci s ostatními testy linkujeme proti EMU baseline.
 *
 * Match modes (en_BP_MATCH_MODE):
 *   SINGLE: x == ref
 *   RANGE:  ref <= x <= end (inclusive bounds)
 *   MASK:   (x & mask) == (ref & mask)
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include "debugger/breakpoints.h"


/* ========================================================================= */
/*  setUp / tearDown                                                          */
/* ========================================================================= */


void setUp ( void ) {
    /* Pure functions, no state. */
}


void tearDown ( void ) {
    /* Pure functions, no state. */
}


/* ========================================================================= */
/*  bp_match16 - SINGLE mode                                                  */
/* ========================================================================= */


void test_match16_single_hit ( void ) {
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_SINGLE, 0x1234, 0x1234, 0, 0 ) );
}


void test_match16_single_miss ( void ) {
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_SINGLE, 0x1234, 0x1235, 0, 0 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_SINGLE, 0x0000, 0x0001, 0, 0 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_SINGLE, 0xFFFE, 0xFFFF, 0, 0 ) );
}


void test_match16_single_zero_ref ( void ) {
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_SINGLE, 0x0000, 0x0000, 0, 0 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_SINGLE, 0x0001, 0x0000, 0, 0 ) );
}


void test_match16_single_max ( void ) {
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_SINGLE, 0xFFFF, 0xFFFF, 0, 0 ) );
}


void test_match16_single_ignores_end_mask ( void ) {
    /* SINGLE musí ignorovat end + mask field. */
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_SINGLE, 0x4000, 0x4000, 0x9999, 0x5555 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_SINGLE, 0x4001, 0x4000, 0xFFFF, 0xFFFF ) );
}


/* ========================================================================= */
/*  bp_match16 - RANGE mode                                                   */
/* ========================================================================= */


void test_match16_range_inside ( void ) {
    /* ref < x < end. */
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_RANGE, 0x4500, 0x4000, 0x4FFF, 0 ) );
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_RANGE, 0x1234, 0x1000, 0x2000, 0 ) );
}


void test_match16_range_at_lower_bound ( void ) {
    /* x == ref (inclusive lower). */
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_RANGE, 0x4000, 0x4000, 0x4FFF, 0 ) );
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_RANGE, 0x0000, 0x0000, 0xFFFF, 0 ) );
}


void test_match16_range_at_upper_bound ( void ) {
    /* x == end (inclusive upper). */
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_RANGE, 0x4FFF, 0x4000, 0x4FFF, 0 ) );
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_RANGE, 0xFFFF, 0x0000, 0xFFFF, 0 ) );
}


void test_match16_range_below ( void ) {
    /* x < ref. */
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_RANGE, 0x3FFF, 0x4000, 0x4FFF, 0 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_RANGE, 0x0000, 0x4000, 0x4FFF, 0 ) );
}


void test_match16_range_above ( void ) {
    /* x > end. */
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_RANGE, 0x5000, 0x4000, 0x4FFF, 0 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_RANGE, 0xFFFF, 0x4000, 0x4FFF, 0 ) );
}


void test_match16_range_inverted_always_false ( void ) {
    /* end < ref - dokumentované chování: false vždy (UI odpovědný za validaci). */
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_RANGE, 0x4500, 0x5000, 0x4000, 0 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_RANGE, 0x5000, 0x5000, 0x4000, 0 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_RANGE, 0x4000, 0x5000, 0x4000, 0 ) );
}


void test_match16_range_single_point ( void ) {
    /* ref == end - ekvivalent SINGLE. */
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_RANGE, 0x4000, 0x4000, 0x4000, 0 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_RANGE, 0x3FFF, 0x4000, 0x4000, 0 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_RANGE, 0x4001, 0x4000, 0x4000, 0 ) );
}


void test_match16_range_full_addr_space ( void ) {
    /* ref=0, end=0xFFFF - true vždy. */
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_RANGE, 0x0000, 0x0000, 0xFFFF, 0 ) );
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_RANGE, 0x8000, 0x0000, 0xFFFF, 0 ) );
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_RANGE, 0xFFFF, 0x0000, 0xFFFF, 0 ) );
}


void test_match16_range_ignores_mask ( void ) {
    /* RANGE musí ignorovat mask field. */
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_RANGE, 0x4500, 0x4000, 0x4FFF, 0xAAAA ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_RANGE, 0x5000, 0x4000, 0x4FFF, 0xAAAA ) );
}


/* ========================================================================= */
/*  bp_match16 - MASK mode                                                    */
/* ========================================================================= */


void test_match16_mask_hit ( void ) {
    /* (x & mask) == (ref & mask). */
    /* x=0x4123, ref=0x41FF, mask=0xFF00 - oba & mask = 0x4100. */
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_MASK, 0x4123, 0x41FF, 0, 0xFF00 ) );
    /* x=0xABCD, ref=0xAB00, mask=0xFF00 - oba & mask = 0xAB00. */
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_MASK, 0xABCD, 0xAB00, 0, 0xFF00 ) );
}


void test_match16_mask_miss ( void ) {
    /* x=0x5123, ref=0x41FF, mask=0xFF00 - 0x5100 != 0x4100. */
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_MASK, 0x5123, 0x41FF, 0, 0xFF00 ) );
}


void test_match16_mask_full_0xFFFF ( void ) {
    /* mask=0xFFFF - ekvivalent SINGLE. */
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_MASK, 0x1234, 0x1234, 0, 0xFFFF ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_MASK, 0x1234, 0x1235, 0, 0xFFFF ) );
}


void test_match16_mask_zero_match_all ( void ) {
    /* mask=0 - (x & 0) == (ref & 0) == 0 vždy. */
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_MASK, 0x0000, 0xFFFF, 0, 0 ) );
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_MASK, 0x1234, 0xABCD, 0, 0 ) );
    TEST_ASSERT_TRUE ( bp_match16 ( BP_MATCH_MASK, 0xFFFF, 0x0000, 0, 0 ) );
}


void test_match16_mask_low_byte_only ( void ) {
    /* mask=0x00FF - high byte ignored. */
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_MASK, 0x9942, 0x4242, 0, 0x00FF ) );
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_MASK, 0x0042, 0xAB42, 0, 0x00FF ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_MASK, 0x0043, 0xAB42, 0, 0x00FF ) );
}


void test_match16_mask_high_byte_only ( void ) {
    /* mask=0xFF00 - low byte ignored. */
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_MASK, 0x4200, 0x4299, 0, 0xFF00 ) );
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_MASK, 0x42FF, 0x4200, 0, 0xFF00 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_MASK, 0x4300, 0x4200, 0, 0xFF00 ) );
}


void test_match16_mask_alternating_bits ( void ) {
    /* mask=0x5555 - odd bity (LSB pohled). */
    /* x=0x5555 & 0x5555 = 0x5555; ref=0xFFFF & 0x5555 = 0x5555. Match. */
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_MASK, 0x5555, 0xFFFF, 0, 0x5555 ) );
    /* x=0xAAAA & 0x5555 = 0; ref=0xFFFF & 0x5555 = 0x5555. Mismatch. */
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_MASK, 0xAAAA, 0xFFFF, 0, 0x5555 ) );
}


void test_match16_mask_ignores_end ( void ) {
    /* MASK musí ignorovat end field. */
    TEST_ASSERT_TRUE  ( bp_match16 ( BP_MATCH_MASK, 0x4123, 0x41FF, 0xFFFF, 0xFF00 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_MASK, 0x5123, 0x41FF, 0x9999, 0xFF00 ) );
}


/* ========================================================================= */
/*  bp_match8 - SINGLE / RANGE / MASK                                         */
/* ========================================================================= */


void test_match8_single_hit ( void ) {
    TEST_ASSERT_TRUE  ( bp_match8 ( BP_MATCH_SINGLE, 0x42, 0x42, 0, 0 ) );
    TEST_ASSERT_TRUE  ( bp_match8 ( BP_MATCH_SINGLE, 0x00, 0x00, 0, 0 ) );
    TEST_ASSERT_TRUE  ( bp_match8 ( BP_MATCH_SINGLE, 0xFF, 0xFF, 0, 0 ) );
}


void test_match8_single_miss ( void ) {
    TEST_ASSERT_FALSE ( bp_match8 ( BP_MATCH_SINGLE, 0x42, 0x43, 0, 0 ) );
    TEST_ASSERT_FALSE ( bp_match8 ( BP_MATCH_SINGLE, 0x00, 0xFF, 0, 0 ) );
}


void test_match8_range_inclusive ( void ) {
    /* Lower + upper bound inclusive. */
    TEST_ASSERT_TRUE  ( bp_match8 ( BP_MATCH_RANGE, 0x10, 0x10, 0x20, 0 ) );
    TEST_ASSERT_TRUE  ( bp_match8 ( BP_MATCH_RANGE, 0x15, 0x10, 0x20, 0 ) );
    TEST_ASSERT_TRUE  ( bp_match8 ( BP_MATCH_RANGE, 0x20, 0x10, 0x20, 0 ) );
    TEST_ASSERT_FALSE ( bp_match8 ( BP_MATCH_RANGE, 0x0F, 0x10, 0x20, 0 ) );
    TEST_ASSERT_FALSE ( bp_match8 ( BP_MATCH_RANGE, 0x21, 0x10, 0x20, 0 ) );
}


void test_match8_range_full_byte ( void ) {
    /* ref=0, end=0xFF - true vždy. */
    TEST_ASSERT_TRUE ( bp_match8 ( BP_MATCH_RANGE, 0x00, 0x00, 0xFF, 0 ) );
    TEST_ASSERT_TRUE ( bp_match8 ( BP_MATCH_RANGE, 0x80, 0x00, 0xFF, 0 ) );
    TEST_ASSERT_TRUE ( bp_match8 ( BP_MATCH_RANGE, 0xFF, 0x00, 0xFF, 0 ) );
}


void test_match8_mask_basic ( void ) {
    /* mask=0xF0 - high nibble. */
    TEST_ASSERT_TRUE  ( bp_match8 ( BP_MATCH_MASK, 0x42, 0x4F, 0, 0xF0 ) );
    TEST_ASSERT_FALSE ( bp_match8 ( BP_MATCH_MASK, 0x52, 0x4F, 0, 0xF0 ) );
}


void test_match8_mask_full_0xFF ( void ) {
    /* mask=0xFF - ekvivalent SINGLE. */
    TEST_ASSERT_TRUE  ( bp_match8 ( BP_MATCH_MASK, 0x42, 0x42, 0, 0xFF ) );
    TEST_ASSERT_FALSE ( bp_match8 ( BP_MATCH_MASK, 0x42, 0x43, 0, 0xFF ) );
}


void test_match8_mask_zero_match_all ( void ) {
    /* mask=0 - true vždy. */
    TEST_ASSERT_TRUE ( bp_match8 ( BP_MATCH_MASK, 0x00, 0xFF, 0, 0 ) );
    TEST_ASSERT_TRUE ( bp_match8 ( BP_MATCH_MASK, 0xFF, 0x00, 0, 0 ) );
}


/* ========================================================================= */
/*  Invalid mode (= sentinel BP_MATCH_COUNT, out-of-range)                    */
/* ========================================================================= */


void test_match16_invalid_mode_returns_false ( void ) {
    /* Defenzivní: out-of-range mode → false. */
    TEST_ASSERT_FALSE ( bp_match16 ( BP_MATCH_COUNT, 0x1234, 0x1234, 0, 0 ) );
    TEST_ASSERT_FALSE ( bp_match16 ( (en_BP_MATCH_MODE) 99, 0x1234, 0x1234, 0, 0 ) );
}


void test_match8_invalid_mode_returns_false ( void ) {
    TEST_ASSERT_FALSE ( bp_match8 ( BP_MATCH_COUNT, 0x42, 0x42, 0, 0 ) );
    TEST_ASSERT_FALSE ( bp_match8 ( (en_BP_MATCH_MODE) 99, 0x42, 0x42, 0, 0 ) );
}


/* ========================================================================= */
/*  String konverze                                                           */
/* ========================================================================= */


void test_mode_to_string_all_3 ( void ) {
    TEST_ASSERT_EQUAL_STRING ( "SINGLE", bp_match_mode_to_string ( BP_MATCH_SINGLE ) );
    TEST_ASSERT_EQUAL_STRING ( "RANGE",  bp_match_mode_to_string ( BP_MATCH_RANGE  ) );
    TEST_ASSERT_EQUAL_STRING ( "MASK",   bp_match_mode_to_string ( BP_MATCH_MASK   ) );
}


void test_mode_to_string_invalid_fallback ( void ) {
    /* Out-of-range / sentinel → fallback "SINGLE". */
    TEST_ASSERT_EQUAL_STRING ( "SINGLE", bp_match_mode_to_string ( BP_MATCH_COUNT ) );
    TEST_ASSERT_EQUAL_STRING ( "SINGLE", bp_match_mode_to_string ( (en_BP_MATCH_MODE) 99 ) );
    TEST_ASSERT_EQUAL_STRING ( "SINGLE", bp_match_mode_to_string ( (en_BP_MATCH_MODE) -1 ) );
}


void test_mode_from_string_round_trip ( void ) {
    en_BP_MATCH_MODE m;

    TEST_ASSERT_TRUE  ( bp_match_mode_from_string ( "SINGLE", &m ) );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_SINGLE, m );

    TEST_ASSERT_TRUE  ( bp_match_mode_from_string ( "RANGE", &m ) );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_RANGE, m );

    TEST_ASSERT_TRUE  ( bp_match_mode_from_string ( "MASK", &m ) );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_MASK, m );
}


void test_mode_from_string_invalid ( void ) {
    en_BP_MATCH_MODE m = BP_MATCH_RANGE;  /* Sentinel - musí zůstat. */

    TEST_ASSERT_FALSE ( bp_match_mode_from_string ( NULL, &m ) );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_RANGE, m );

    TEST_ASSERT_FALSE ( bp_match_mode_from_string ( "FOO", &m ) );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_RANGE, m );

    TEST_ASSERT_FALSE ( bp_match_mode_from_string ( "", &m ) );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_RANGE, m );

    /* Case-sensitive (= JSON serializace stabilní). */
    TEST_ASSERT_FALSE ( bp_match_mode_from_string ( "single", &m ) );
    TEST_ASSERT_FALSE ( bp_match_mode_from_string ( "Range", &m ) );
}


void test_mode_from_string_null_out ( void ) {
    /* NULL out pointer → false (no crash). */
    TEST_ASSERT_FALSE ( bp_match_mode_from_string ( "SINGLE", NULL ) );
}


/* ========================================================================= */
/*  Main                                                                      */
/* ========================================================================= */


int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    /* SINGLE 16-bit */
    RUN_TEST ( test_match16_single_hit );
    RUN_TEST ( test_match16_single_miss );
    RUN_TEST ( test_match16_single_zero_ref );
    RUN_TEST ( test_match16_single_max );
    RUN_TEST ( test_match16_single_ignores_end_mask );

    /* RANGE 16-bit */
    RUN_TEST ( test_match16_range_inside );
    RUN_TEST ( test_match16_range_at_lower_bound );
    RUN_TEST ( test_match16_range_at_upper_bound );
    RUN_TEST ( test_match16_range_below );
    RUN_TEST ( test_match16_range_above );
    RUN_TEST ( test_match16_range_inverted_always_false );
    RUN_TEST ( test_match16_range_single_point );
    RUN_TEST ( test_match16_range_full_addr_space );
    RUN_TEST ( test_match16_range_ignores_mask );

    /* MASK 16-bit */
    RUN_TEST ( test_match16_mask_hit );
    RUN_TEST ( test_match16_mask_miss );
    RUN_TEST ( test_match16_mask_full_0xFFFF );
    RUN_TEST ( test_match16_mask_zero_match_all );
    RUN_TEST ( test_match16_mask_low_byte_only );
    RUN_TEST ( test_match16_mask_high_byte_only );
    RUN_TEST ( test_match16_mask_alternating_bits );
    RUN_TEST ( test_match16_mask_ignores_end );

    /* 8-bit (bank ID) */
    RUN_TEST ( test_match8_single_hit );
    RUN_TEST ( test_match8_single_miss );
    RUN_TEST ( test_match8_range_inclusive );
    RUN_TEST ( test_match8_range_full_byte );
    RUN_TEST ( test_match8_mask_basic );
    RUN_TEST ( test_match8_mask_full_0xFF );
    RUN_TEST ( test_match8_mask_zero_match_all );

    /* Invalid mode (defenzivní) */
    RUN_TEST ( test_match16_invalid_mode_returns_false );
    RUN_TEST ( test_match8_invalid_mode_returns_false );

    /* String konverze */
    RUN_TEST ( test_mode_to_string_all_3 );
    RUN_TEST ( test_mode_to_string_invalid_fallback );
    RUN_TEST ( test_mode_from_string_round_trip );
    RUN_TEST ( test_mode_from_string_invalid );
    RUN_TEST ( test_mode_from_string_null_out );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
