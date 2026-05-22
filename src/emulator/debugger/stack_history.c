/*
 * stack_history.c - implementace ring bufferu historie SP.
 *
 * Modifikace: výhradně EMU vlákno (hot-path stack_history_record +
 * dbgapi handlery v safepointu). Lookup z UI vlákna probíhá přes
 * stack_history_copy_recent v safepointu (= dbgapi GET handler vola
 * z EMU vlákna a kopíruje data do caller-vlastněného bufferu).
 *
 * Implementační poznámka k slope:
 *   Lineární regrese počítáme v double precision uvnitř a vrátíme
 *   float - 16-bit SP a 32-bit cycles se vejdou do double bez ztráty,
 *   ale double sumy chrání před overflow (= sum(x^2) může přesáhnout
 *   2^32 při typických T-state hodnotách).
 *
 * ----------------------------- License -------------------------------------
 *
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
 *
 * ---------------------------------------------------------------------------
 */

#include "stack_history.h"

#include <stdbool.h>
#include <string.h>

#include "cfgmain.h"
#include "libs/cfgfile/cfgroot.h"
#include "libs/cfgfile/cfgmodule.h"
#include "libs/cfgfile/cfgelement.h"


/* ============================================================================
 * Globální stav
 * ============================================================================ */

/**
 * @brief Statický ring buffer vzorků. Init = zero (count = 0).
 */
static st_STACK_HISTORY_SAMPLE g_stack_history[ STACK_HISTORY_SIZE ];

/**
 * @brief Pozice dalšího zápisu v ring bufferu (= 0..SIZE-1, modulo SIZE).
 */
static uint32_t g_stack_history_write_idx = 0;

/**
 * @brief Aktuální počet validních vzorků v bufferu (0..SIZE, saturuje).
 */
static uint32_t g_stack_history_count = 0;

/**
 * @brief Hot-path aktivační flag, modifikuje výhradně EMU vlákno.
 */
bool g_stack_history_active = false;


/* ============================================================================
 * API
 * ============================================================================ */

void stack_history_reset ( void )
{
    g_stack_history_write_idx = 0;
    g_stack_history_count = 0;
    /* Vlastní pole vzorků nezeruje (= zbytkový obsah je nedostupný,
     * count = 0 ho schová). Šetří memset 48 KB při každém resetu. */
}


void stack_history_set_active ( bool enable )
{
    if ( !enable ) {
        /* Vypnutí = drop pending dat (= další zapnutí začne s čistým
         * stavem, ne s prokombinovanou historií ze starého běhu). */
        stack_history_reset ( );
    };
    g_stack_history_active = enable;
}


void stack_history_record ( uint16_t sp, uint32_t cycles )
{
    /* Caller (= mzarch hot loop) testuje g_stack_history_active předem.
     * Defenzivně tu znovu testujeme = stejné chování jako kdyby caller
     * volání skipnul. Náklad: 1 byte read + branch (predicted taken). */
    if ( !g_stack_history_active ) return;

    st_STACK_HISTORY_SAMPLE *s = &g_stack_history[ g_stack_history_write_idx ];
    s->cycles = cycles;
    s->sp     = sp;
    s->_pad   = 0;

    /* Wrap write_idx přes SIZE. STACK_HISTORY_SIZE je 4096 = power of 2
     * (= maska místo modulo, kompilátor často optimalizuje, ale explicit
     * mask zaručuje stejné chování bez ohledu na build flagy). */
    g_stack_history_write_idx = ( g_stack_history_write_idx + 1u )
                                 & ( STACK_HISTORY_SIZE - 1u );

    if ( g_stack_history_count < STACK_HISTORY_SIZE ) {
        g_stack_history_count++;
    };
}


uint32_t stack_history_get_count ( void )
{
    return g_stack_history_count;
}


st_STACK_HISTORY_SAMPLE stack_history_get_sample ( uint32_t i )
{
    if ( i >= g_stack_history_count ) {
        st_STACK_HISTORY_SAMPLE empty = { 0, 0, 0 };
        return empty;
    };

    /* Mapování lineárního indexu (0 = oldest, count-1 = newest) na fyzický
     * index v ring bufferu:
     *   - Pokud count < SIZE: buffer ještě nepřetekl, oldest = slot 0,
     *     newest = slot count-1. fyz_idx = i.
     *   - Pokud count == SIZE: oldest = slot write_idx (= další k přepsání),
     *     newest = slot (write_idx - 1) mod SIZE. fyz_idx = (write_idx + i) mod SIZE. */
    uint32_t phys;
    if ( g_stack_history_count < STACK_HISTORY_SIZE ) {
        phys = i;
    } else {
        phys = ( g_stack_history_write_idx + i ) & ( STACK_HISTORY_SIZE - 1u );
    };
    return g_stack_history[ phys ];
}


uint32_t stack_history_copy_recent ( st_STACK_HISTORY_SAMPLE *dst,
                                       uint32_t max_count )
{
    if ( !dst || max_count == 0 ) return 0;

    uint32_t n = g_stack_history_count;
    if ( n > max_count ) n = max_count;

    /* Lineární index `start` = oldest sample z posledních n vzorků.
     * = count - n (= jak hluboko od konce začínáme). */
    uint32_t start_lin = g_stack_history_count - n;

    for ( uint32_t i = 0; i < n; i++ ) {
        dst[ i ] = stack_history_get_sample ( start_lin + i );
    };
    return n;
}


float stack_history_compute_slope ( uint32_t window_size )
{
    if ( window_size > g_stack_history_count ) {
        window_size = g_stack_history_count;
    };
    if ( window_size < 2 ) return 0.0f;

    /* Sumy v double - sum_xx a sum_xy mohou přesáhnout 32-bit range
     * při velkém okénku a velkých cycles hodnotách. */
    double sum_x  = 0.0;
    double sum_y  = 0.0;
    double sum_xx = 0.0;
    double sum_xy = 0.0;

    uint32_t start_lin = g_stack_history_count - window_size;

    /* Použít první vzorek jako reference pro x (= numerická stabilita,
     * cycles bývá řádu 10^9 a x*x by jinak ztratilo přesnost double). */
    st_STACK_HISTORY_SAMPLE first = stack_history_get_sample ( start_lin );
    double x_ref = (double) first.cycles;

    for ( uint32_t i = 0; i < window_size; i++ ) {
        st_STACK_HISTORY_SAMPLE s = stack_history_get_sample ( start_lin + i );
        double x = (double) s.cycles - x_ref;
        double y = (double) s.sp;
        sum_x  += x;
        sum_y  += y;
        sum_xx += x * x;
        sum_xy += x * y;
    };

    double n = (double) window_size;
    double denom = n * sum_xx - sum_x * sum_x;
    if ( denom == 0.0 ) {
        /* Všechny vzorky mají stejný cycles (= rozumný edge case pri
         * rychlém slope checku nad stejným tickem). */
        return 0.0f;
    };
    double numer = n * sum_xy - sum_x * sum_y;
    return (float) ( numer / denom );
}


/* ============================================================================
 * V6 - persistence runtime nastavení do cfgmain.ini sekce [STACK_HISTORY]
 * ============================================================================ */

/**
 * @brief Idempotence flag pro stack_history_cfg_init.
 */
static bool s_cfg_registered = false;

/**
 * @brief Propagate cb pro [STACK_HISTORY] enabled - aplikuje hodnotu
 *        ze starého INI přes stack_history_set_active.
 *
 * Volá se v debugger_init() z hlavního vlákna před startem emu vlákna.
 * Při enable=true zachová flag, při enable=false navíc vyprázdní buffer
 * (= konzistentní startup chování s manuálním toggle z UI).
 */
static void cfg_propagate_enabled ( void *e, void *data )
{
    (void) data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    int v = cfgelement_get_bool_value ( elm );
    stack_history_set_active ( v ? true : false );
}

/**
 * @brief Save cb pro [STACK_HISTORY] enabled - zapíše aktuální g_stack_history_active.
 */
static void cfg_save_enabled ( void *e, void *data )
{
    (void) data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    cfgelement_set_bool_value ( elm, g_stack_history_active ? 1 : 0 );
}


void stack_history_cfg_init ( void )
{
    if ( s_cfg_registered ) return;
    s_cfg_registered = true;

    CFGMOD *cmod = cfgroot_register_new_module ( g_cfgmain, "STACK_HISTORY" );
    if ( !cmod ) {
        /* Duplicate registrace - jen běh bez persistence. */
        return;
    };

    CFGELM *elm;
    elm = cfgmodule_register_new_element ( cmod, "enabled", CFGENTYPE_BOOL, 0 );
    cfgelement_set_propagate_cb ( elm, cfg_propagate_enabled, NULL );
    cfgelement_set_save_cb      ( elm, cfg_save_enabled, NULL );

    cfgmodule_parse ( cmod );
    cfgmodule_propagate ( cmod );
}
