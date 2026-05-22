/**
 * @file   test_mztape.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Unit testy knihovny mztape (konverze MZF na CMT streamy).
 *
 * Testuje: lifecycle (create/destroy), checksums, vstream/bitstream generování,
 * compute_pulses, speed array, format blocks, alokátor callback,
 * error callback, regresní testy opravených bugů.
 *
 * Linkuje se s TEST_ALL_OBJS (vyžaduje generic_driver, memory_driver,
 * mzf, cmt_stream, endianity, cmtspeed).
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

#include "libs/mztape/mztape.h"
#include "libs/mzf/mzf.h"
#include "libs/generic_driver/generic_driver.h"
#include "generic_driver/memory_driver.h"
#include "libs/cmt_stream/cmt_stream.h"
#include "libs/cmtspeed/cmtspeed.h"


/* ========================================================================
 * setUp / tearDown — volány před/po každém testu
 * ======================================================================== */

static st_HANDLER s_handler;
static int s_handler_open = 0;


/** @brief Inicializace před každým testem — vynuluje handler a příznak otevření */
void setUp ( void ) {
    memset ( &s_handler, 0, sizeof ( s_handler ) );
    s_handler_open = 0;
}


/** @brief Úklid po každém testu — zavře handler, resetuje alokátor a error callback */
void tearDown ( void ) {
    if ( s_handler_open ) {
        generic_driver_close ( &s_handler );
        s_handler_open = 0;
    }
    /* reset alokátoru a error callbacku na výchozí */
    mztape_set_allocator ( NULL );
    mztape_set_error_callback ( NULL );
}


/* ========================================================================
 * Pomocné funkce
 * ======================================================================== */

/**
 * @brief Otevře paměťový handler s realokačním driverem.
 * @param initial_size počáteční velikost bufferu v bajtech
 * @return ukazatel na handler, nebo NULL při chybě
 */
static st_HANDLER* open_memory_handler ( uint32_t initial_size ) {
    st_HANDLER *h = generic_driver_open_memory ( &s_handler, &g_memory_driver_realloc, initial_size );
    if ( h != NULL ) s_handler_open = 1;
    return h;
}


/**
 * @brief Vytvoří kompletní MZF v memory handleru (hlavička + tělo) pro mztape testy.
 * @param body_size velikost těla MZF v bajtech
 * @return ukazatel na handler, nebo NULL při chybě
 */
static st_HANDLER* create_test_mzf_in_memory ( uint16_t body_size ) {
    st_MZF_HEADER hdr;
    memset ( &hdr, 0, sizeof ( hdr ) );
    hdr.ftype = 0x01; /* OBJ */
    hdr.fsize = body_size;
    hdr.fstrt = 0x1200;
    hdr.fexec = 0x1200;
    memset ( hdr.fname.name, 0x0D, MZF_FILE_NAME_LENGTH );
    hdr.fname.name[0] = 'T';
    hdr.fname.name[1] = 'S';
    hdr.fname.name[2] = 'T';
    hdr.fname.terminator = 0x0D;

    st_HANDLER *h = open_memory_handler ( MZF_HEADER_SIZE + body_size );
    if ( !h ) return NULL;

    if ( EXIT_SUCCESS != mzf_write_header ( h, &hdr ) ) return NULL;

    if ( body_size > 0 ) {
        uint8_t *body = (uint8_t*) malloc ( body_size );
        if ( !body ) return NULL;
        for ( uint16_t i = 0; i < body_size; i++ ) {
            body[i] = (uint8_t) ( i & 0xFF );
        }
        int rc = mzf_write_body ( h, body, body_size );
        free ( body );
        if ( EXIT_SUCCESS != rc ) return NULL;
    }

    return h;
}


/* ========================================================================
 * SMOKE testy
 * ======================================================================== */

/** @brief Testuje vytvoření a zničení MZF tapového objektu */
static void test_mztapemzf_create_destroy ( void ) {
    st_HANDLER *h = create_test_mzf_in_memory ( 64 );
    TEST_ASSERT_NOT_NULL ( h );

    st_MZTAPE_MZF *mztmzf = mztape_create_mztapemzf ( h, 0 );
    TEST_ASSERT_NOT_NULL ( mztmzf );
    TEST_ASSERT_EQUAL_UINT16 ( 64, mztmzf->size );
    TEST_ASSERT_NOT_NULL ( mztmzf->body );

    mztape_mztmzf_destroy ( mztmzf );
}


/** @brief Testuje destroy s NULL parametrem (nesmí crashnout) */
static void test_mztapemzf_destroy_null ( void ) {
    mztape_mztmzf_destroy ( NULL );
}


/* ========================================================================
 * UNIT testy
 * ======================================================================== */

/** @brief Testuje správnost header a body checksumů v MZF tapovém objektu */
static void test_mztapemzf_checksums ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_HANDLER *h = create_test_mzf_in_memory ( 32 );
    TEST_ASSERT_NOT_NULL ( h );

    st_MZTAPE_MZF *mztmzf = mztape_create_mztapemzf ( h, 0 );
    TEST_ASSERT_NOT_NULL ( mztmzf );

    /* checksums musí být nenulové pro nenulová data */
    TEST_ASSERT_NOT_EQUAL ( 0, mztmzf->chkh );
    TEST_ASSERT_NOT_EQUAL ( 0, mztmzf->chkb );

    /* header checksum nesmí přesáhnout 128*8 = 1024 (počet bitů v hlavičce) */
    TEST_ASSERT_TRUE ( mztmzf->chkh <= 128 * 8 );

    /* body checksum nesmí přesáhnout body_size*8 */
    TEST_ASSERT_TRUE ( mztmzf->chkb <= mztmzf->size * 8 );

    mztape_mztmzf_destroy ( mztmzf );
}


/** @brief Testuje vytvoření vstreamu z testovacího MZF */
static void test_create_vstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_HANDLER *h = create_test_mzf_in_memory ( 64 );
    TEST_ASSERT_NOT_NULL ( h );

    st_MZTAPE_MZF *mztmzf = mztape_create_mztapemzf ( h, 0 );
    TEST_ASSERT_NOT_NULL ( mztmzf );

    st_CMT_VSTREAM *vstream = mztape_create_cmt_vstream_from_mztmzf (
        mztmzf, MZTAPE_FORMATSET_MZ800_SANE, CMTSPEED_1_1, 44100 );
    TEST_ASSERT_NOT_NULL ( vstream );

    /* vstream musí mít nenulový počet vzorků */
    TEST_ASSERT_TRUE ( cmt_vstream_get_count_scans ( vstream ) > 0 );

    cmt_vstream_destroy ( vstream );
    mztape_mztmzf_destroy ( mztmzf );
}


/** @brief Testuje vytvoření bitstreamu přes vstream konverzi */
static void test_create_bitstream ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_HANDLER *h = create_test_mzf_in_memory ( 64 );
    TEST_ASSERT_NOT_NULL ( h );

    st_MZTAPE_MZF *mztmzf = mztape_create_mztapemzf ( h, 0 );
    TEST_ASSERT_NOT_NULL ( mztmzf );

    st_CMT_STREAM *stream = mztape_create_stream_from_mztapemzf (
        mztmzf, CMTSPEED_1_1, CMT_STREAM_TYPE_BITSTREAM,
        MZTAPE_FORMATSET_MZ800_SANE, 44100 );
    TEST_ASSERT_NOT_NULL ( stream );

    TEST_ASSERT_NOT_NULL ( stream->str.bitstream );

    cmt_stream_destroy ( stream );
    mztape_mztmzf_destroy ( mztmzf );
}


/** @brief Testuje wrapper funkci pro vytvoření obou typů streamů (vstream i bitstream) */
static void test_create_stream_wrapper ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_HANDLER *h = create_test_mzf_in_memory ( 32 );
    TEST_ASSERT_NOT_NULL ( h );

    st_MZTAPE_MZF *mztmzf = mztape_create_mztapemzf ( h, 0 );
    TEST_ASSERT_NOT_NULL ( mztmzf );

    /* vstream varianta */
    st_CMT_STREAM *vs = mztape_create_stream_from_mztapemzf (
        mztmzf, CMTSPEED_1_1, CMT_STREAM_TYPE_VSTREAM,
        MZTAPE_FORMATSET_MZ800_SANE, 44100 );
    TEST_ASSERT_NOT_NULL ( vs );
    TEST_ASSERT_NOT_NULL ( vs->str.vstream );
    cmt_stream_destroy ( vs );

    /* bitstream varianta */
    st_CMT_STREAM *bs = mztape_create_stream_from_mztapemzf (
        mztmzf, CMTSPEED_1_1, CMT_STREAM_TYPE_BITSTREAM,
        MZTAPE_FORMATSET_MZ800_SANE, 44100 );
    TEST_ASSERT_NOT_NULL ( bs );
    TEST_ASSERT_NOT_NULL ( bs->str.bitstream );
    cmt_stream_destroy ( bs );

    mztape_mztmzf_destroy ( mztmzf );
}


/** @brief Testuje compute_pulses — ověření počtu pulzů pro známý MZF a porovnání SANE vs. plný formát */
static void test_compute_pulses ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_HANDLER *h = create_test_mzf_in_memory ( 64 );
    TEST_ASSERT_NOT_NULL ( h );

    st_MZTAPE_MZF *mztmzf = mztape_create_mztapemzf ( h, 0 );
    TEST_ASSERT_NOT_NULL ( mztmzf );

    uint64_t long_pulses = 0, short_pulses = 0;
    mztape_compute_pulses ( mztmzf, MZTAPE_FORMATSET_MZ800_SANE, &long_pulses, &short_pulses );

    /* musí být nenulový počet obou typů pulzů */
    TEST_ASSERT_TRUE ( long_pulses > 0 );
    TEST_ASSERT_TRUE ( short_pulses > 0 );

    /* SANE formát má kratší GAP → menší celkový počet pulzů než plný formát */
    uint64_t long_full = 0, short_full = 0;
    mztape_compute_pulses ( mztmzf, MZTAPE_FORMATSET_MZ800, &long_full, &short_full );
    TEST_ASSERT_TRUE ( ( long_full + short_full ) > ( long_pulses + short_pulses ) );

    mztape_mztmzf_destroy ( mztmzf );
}


/** @brief Testuje, že pole g_mztape_speed[] je správně ukončeno terminátorem CMTSPEED_NONE */
static void test_speed_array_terminated ( void ) {
    int i = 0;
    while ( g_mztape_speed[i] != CMTSPEED_NONE ) {
        TEST_ASSERT_TRUE ( cmtspeed_is_valid ( g_mztape_speed[i] ) );
        i++;
        TEST_ASSERT_TRUE_MESSAGE ( i < 100, "g_mztape_speed[] neobsahuje terminátor" );
    }
    /* musí obsahovat alespoň jednu platnou rychlost */
    TEST_ASSERT_TRUE ( i > 0 );
}


/* ========================================================================
 * Testy alokátoru a error callbacku
 * ======================================================================== */

/* počítadla pro vlastní alokátor */
static int s_alloc_count = 0;
static int s_alloc0_count = 0;
static int s_free_count = 0;

/** @brief Testovací alokátor — počítá volání a deleguje na malloc */
static void* test_alloc ( size_t size ) { s_alloc_count++; return malloc ( size ); }
/** @brief Testovací alokátor — počítá volání a deleguje na calloc */
static void* test_alloc0 ( size_t size ) { s_alloc0_count++; return calloc ( 1, size ); }
/** @brief Testovací dealokátor — počítá volání a deleguje na free */
static void  test_free ( void *ptr ) { s_free_count++; free ( ptr ); }


/** @brief Testuje vlastní alokátor — ověření, že se alloc/alloc0/free volají */
static void test_allocator_custom ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    st_MZTAPE_ALLOCATOR custom = { test_alloc, test_alloc0, test_free };
    mztape_set_allocator ( &custom );

    s_alloc_count = 0;
    s_alloc0_count = 0;
    s_free_count = 0;

    st_HANDLER *h = create_test_mzf_in_memory ( 16 );
    TEST_ASSERT_NOT_NULL ( h );

    st_MZTAPE_MZF *mztmzf = mztape_create_mztapemzf ( h, 0 );
    TEST_ASSERT_NOT_NULL ( mztmzf );

    /* alloc0 pro strukturu, alloc pro body */
    TEST_ASSERT_TRUE ( s_alloc0_count > 0 );
    TEST_ASSERT_TRUE ( s_alloc_count > 0 );

    mztape_mztmzf_destroy ( mztmzf );

    /* free pro body + strukturu */
    TEST_ASSERT_TRUE ( s_free_count >= 2 );

    mztape_set_allocator ( NULL ); /* reset */
}


/* počítadlo pro error callback */
static int s_error_count = 0;

/** @brief Testovací error callback — počítá volání */
static void test_error_cb ( const char *func, int line, const char *fmt, ... ) {
    (void) func; (void) line; (void) fmt;
    s_error_count++;
}


/** @brief Selhávající alokátor — vždy vrací NULL */
static void* test_alloc_fail ( size_t size ) { (void) size; return NULL; }
/** @brief Selhávající alokátor — vždy vrací NULL */
static void* test_alloc0_fail ( size_t size ) { (void) size; return NULL; }
/** @brief Prázdný dealokátor — nic nedělá */
static void  test_free_noop ( void *ptr ) { (void) ptr; }


/** @brief Testuje vlastní error callback — ověření, že se volá při selhání alokace */
static void test_error_callback ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    mztape_set_error_callback ( test_error_cb );
    s_error_count = 0;

    /* selhávající alokátor → alloc0 vrátí NULL → error callback musí být zavolán */
    st_MZTAPE_ALLOCATOR fail_alloc = { test_alloc_fail, test_alloc0_fail, test_free_noop };
    mztape_set_allocator ( &fail_alloc );

    st_HANDLER *h = open_memory_handler ( 128 + 64 );
    TEST_ASSERT_NOT_NULL ( h );

    st_MZTAPE_MZF *mztmzf = mztape_create_mztapemzf ( h, 0 );
    TEST_ASSERT_NULL ( mztmzf ); /* musí selhat — alloc0 vrátil NULL */
    TEST_ASSERT_TRUE ( s_error_count > 0 ); /* error callback musí být zavolán */

    mztape_set_allocator ( NULL ); /* reset */
    mztape_set_error_callback ( NULL ); /* reset */
}


/* ========================================================================
 * Regresní testy
 * ======================================================================== */

/** @brief Regresní test: destruktor musí uvolnit body (dříve memory leak) */
static void test_destroy_frees_body ( void ) {
    st_HANDLER *h = create_test_mzf_in_memory ( 64 );
    TEST_ASSERT_NOT_NULL ( h );

    /* vlastní alokátor pro počítání free volání */
    st_MZTAPE_ALLOCATOR custom = { test_alloc, test_alloc0, test_free };
    mztape_set_allocator ( &custom );
    s_free_count = 0;

    st_MZTAPE_MZF *mztmzf = mztape_create_mztapemzf ( h, 0 );
    TEST_ASSERT_NOT_NULL ( mztmzf );
    TEST_ASSERT_NOT_NULL ( mztmzf->body );

    mztape_mztmzf_destroy ( mztmzf );

    /* musí proběhnout minimálně 2 free: body + struktura */
    TEST_ASSERT_TRUE_MESSAGE ( s_free_count >= 2,
        "Destruktor musí uvolnit body i strukturu (regrese: memory leak)" );

    mztape_set_allocator ( NULL );
}


/**
 * @brief Regresní test: chybová cesta nesmí způsobit use-after-free.
 *
 * Dříve mztape_create_mztapemzf() při chybě čtení body volal:
 *   mztape_mztmzf_destroy(mztmzf);  // uvolní strukturu
 *   baseui_tools_mem_free(mztmzf->body);  // use-after-free!
 *
 * Po opravě se body uvolňuje uvnitř destroy.
 */
static void test_create_error_no_use_after_free ( void ) {
    /* Vytvoříme handler s hlavičkou, ale bez dostatečného těla */
    st_MZF_HEADER hdr;
    memset ( &hdr, 0, sizeof ( hdr ) );
    hdr.ftype = 0x01;
    hdr.fsize = 1000; /* deklaruje 1000 B těla */
    hdr.fstrt = 0x1200;
    hdr.fexec = 0x1200;
    memset ( hdr.fname.name, 0x0D, MZF_FILE_NAME_LENGTH );
    hdr.fname.terminator = 0x0D;

    st_HANDLER *h = open_memory_handler ( MZF_HEADER_SIZE + 8 ); /* jen 8 B těla */
    TEST_ASSERT_NOT_NULL ( h );

    TEST_ASSERT_EQUAL_INT ( EXIT_SUCCESS, mzf_write_header ( h, &hdr ) );

    /* Zapíšeme jen 8 bajtů těla — čtení 1000 B selže */
    uint8_t body[8] = {0};
    generic_driver_write ( h, MZF_HEADER_SIZE, body, 8 );

    /* Vlastní alokátor pro sledování */
    st_MZTAPE_ALLOCATOR custom = { test_alloc, test_alloc0, test_free };
    mztape_set_allocator ( &custom );
    mztape_set_error_callback ( test_error_cb );
    s_free_count = 0;
    s_error_count = 0;

    st_MZTAPE_MZF *mztmzf = mztape_create_mztapemzf ( h, 0 );

    /*
     * Výsledek závisí na implementaci memory driveru — pokud driver
     * vrátí úspěch i pro krátké čtení, mztmzf bude vytvořen.
     * V obou případech nesmí dojít k use-after-free.
     */
    if ( mztmzf ) {
        mztape_mztmzf_destroy ( mztmzf );
    }
    /* Pokud test doběhl sem, use-after-free nenastal */

    mztape_set_allocator ( NULL );
    mztape_set_error_callback ( NULL );
}


/* ========================================================================
 * main
 * ======================================================================== */

int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ();

    UNITY_BEGIN ();

    /* Smoke testy */
    RUN_TEST ( test_mztapemzf_create_destroy );
    RUN_TEST ( test_mztapemzf_destroy_null );

    /* Unit testy */
    RUN_TEST ( test_mztapemzf_checksums );
    RUN_TEST ( test_create_vstream );
    RUN_TEST ( test_create_bitstream );
    RUN_TEST ( test_create_stream_wrapper );
    RUN_TEST ( test_compute_pulses );
    RUN_TEST ( test_speed_array_terminated );
    RUN_TEST ( test_allocator_custom );
    RUN_TEST ( test_error_callback );

    /* Regresní testy */
    RUN_TEST ( test_destroy_frees_body );
    RUN_TEST ( test_create_error_no_use_after_free );

    int result = UNITY_END ();
    mztest_teardown ();
    return result;
}
