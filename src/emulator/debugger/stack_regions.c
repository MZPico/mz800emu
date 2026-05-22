/*
 * stack_regions.c - implementace registru stack regionů.
 *
 * Modifikace: výhradně EMU vlákno (dbgapi handlery v safepointu).
 * Lookup z UI vlákna probíhá přes snapshot v STACK_REGIONS_LIST handleru
 * (= UI nesahá přímo do globálního pole bez synchronizace).
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

#include "stack_regions.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "cfgmain.h"
#include "libs/cfgfile/cfgroot.h"
#include "libs/cfgfile/cfgmodule.h"
#include "libs/cfgfile/cfgelement.h"


/* ============================================================================
 * Globální stav
 * ============================================================================ */

/**
 * @brief Statické pole regionů. Init = zero (count = 0, active = false).
 */
st_STACK_REGION g_stack_regions[ STACK_REGIONS_MAX ] = { { { 0 }, 0, 0, 0, 0, 0 } };

/**
 * @brief Aktuální počet platných regionů.
 */
int g_stack_regions_count = 0;

/**
 * @brief Hot-path aktivační flag. Mirror "g_stack_regions_count > 0".
 */
bool g_stack_regions_active = false;


/* ============================================================================
 * Validace jména
 * ============================================================================ */

/**
 * @brief Validuje, že jméno regionu obsahuje jen [a-zA-Z0-9_] a je neprázdné.
 *
 * @param name  C string (může být NULL).
 * @return true při validním jménu, false při NULL / prázdný / neplatný znak.
 */
static bool stack_regions_name_is_valid ( const char *name )
{
    if ( !name ) return false;
    if ( name[ 0 ] == '\0' ) return false;
    for ( const char *p = name; *p; p++ ) {
        char c = *p;
        bool ok = ( c >= 'a' && c <= 'z' )
               || ( c >= 'A' && c <= 'Z' )
               || ( c >= '0' && c <= '9' )
               || ( c == '_' );
        if ( !ok ) return false;
    };
    return true;
}


/* ============================================================================
 * API
 * ============================================================================ */

int stack_regions_add ( const char *name, uint16_t base, uint16_t limit )
{
    /* Validace argumentů. */
    if ( g_stack_regions_count >= STACK_REGIONS_MAX ) return -1;
    if ( !stack_regions_name_is_valid ( name ) ) return -1;
    if ( base <= limit ) return -1;    /* netriviální region = base > limit */

    /* Kontrola délky jména - musí se vejít včetně '\0'. */
    size_t name_len = strlen ( name );
    if ( name_len >= STACK_REGION_NAME_MAX ) return -1;

    /* Kontrola unikátního jména (= UX usnadnění; ne tvrdá podmínka, ale
     * dva regiony se stejným jménem UI zmate v dropdownu). */
    for ( int i = 0; i < g_stack_regions_count; i++ ) {
        if ( strcmp ( g_stack_regions[ i ].name, name ) == 0 ) return -1;
    };

    int idx = g_stack_regions_count;
    st_STACK_REGION *r = &g_stack_regions[ idx ];

    /* Kopie jména + zarovnání zbytku na 0 (= konzistentní layout pro
     * případnou serializaci později). */
    memset ( r->name, 0, STACK_REGION_NAME_MAX );
    memcpy ( r->name, name, name_len );

    r->base            = base;
    r->limit           = limit;
    r->watermark       = base;            /* init = base, klesá až při PUSH */
    r->push_count      = 0;
    r->pop_count       = 0;

    g_stack_regions_count++;
    g_stack_regions_active = ( g_stack_regions_count > 0 );

    return idx;
}


int stack_regions_check_overlap ( int idx_ignore, uint16_t base, uint16_t limit )
{
    /* Iteruje aktivní sloty 0..count-1, vynechá idx_ignore. Match podmínka:
     * intervaly <a.limit..a.base> a <b.limit..b.base> se překrývají pokud
     * a.limit <= b.base && a.base >= b.limit (inkluzivní). */
    for ( int i = 0; i < g_stack_regions_count; i++ ) {
        if ( i == idx_ignore ) continue;
        const st_STACK_REGION *r = &g_stack_regions[ i ];
        if ( limit <= r->base && base >= r->limit ) {
            return i;
        };
    };
    return -1;
}


bool stack_regions_edit ( int idx, const char *name, uint16_t base, uint16_t limit )
{
    /* Validace indexu. */
    if ( idx < 0 || idx >= g_stack_regions_count ) return false;

    /* Validace jména a rozsahu (stejná pravidla jako add). */
    if ( !stack_regions_name_is_valid ( name ) ) return false;
    if ( base <= limit ) return false;

    size_t name_len = strlen ( name );
    if ( name_len >= STACK_REGION_NAME_MAX ) return false;

    /* Kontrola unikátního jména proti ostatním slotům (vyjma idx). */
    for ( int i = 0; i < g_stack_regions_count; i++ ) {
        if ( i == idx ) continue;
        if ( strcmp ( g_stack_regions[ i ].name, name ) == 0 ) return false;
    };

    /* Overlap detekce vůči ostatním regionům (vyjma editovaného). */
    if ( stack_regions_check_overlap ( idx, base, limit ) >= 0 ) return false;

    /* Commit nových hodnot + reset runtime stats (= staré stats neplatí
     * pro nový rozsah). */
    st_STACK_REGION *r = &g_stack_regions[ idx ];
    memset ( r->name, 0, STACK_REGION_NAME_MAX );
    memcpy ( r->name, name, name_len );
    r->base       = base;
    r->limit      = limit;
    r->watermark  = base;
    r->push_count = 0;
    r->pop_count  = 0;

    return true;
}


bool stack_regions_remove ( int index )
{
    if ( index < 0 || index >= g_stack_regions_count ) return false;

    /* Compact - posun všech regionů za index doleva o jednu pozici. */
    for ( int i = index; i + 1 < g_stack_regions_count; i++ ) {
        g_stack_regions[ i ] = g_stack_regions[ i + 1 ];
    };

    /* Vyčisti uvolněný slot na konci (= konzistentní zero pattern). */
    int last = g_stack_regions_count - 1;
    memset ( &g_stack_regions[ last ], 0, sizeof ( st_STACK_REGION ) );

    g_stack_regions_count--;
    g_stack_regions_active = ( g_stack_regions_count > 0 );
    return true;
}


bool stack_regions_reset_watermark ( int index )
{
    if ( index < 0 || index >= g_stack_regions_count ) return false;
    st_STACK_REGION *r = &g_stack_regions[ index ];
    r->watermark  = r->base;
    r->push_count = 0;
    r->pop_count  = 0;
    return true;
}


void stack_regions_reset_all_stats ( void )
{
    for ( int i = 0; i < g_stack_regions_count; i++ ) {
        st_STACK_REGION *r = &g_stack_regions[ i ];
        r->watermark  = r->base;
        r->push_count = 0;
        r->pop_count  = 0;
    };
}


st_STACK_REGION *stack_regions_find_by_sp ( uint16_t sp )
{
    for ( int i = 0; i < g_stack_regions_count; i++ ) {
        st_STACK_REGION *r = &g_stack_regions[ i ];
        if ( sp >= r->limit && sp <= r->base ) {
            return r;
        };
    };
    return NULL;
}


void stack_regions_on_sp_change ( uint16_t old_sp, uint16_t new_sp )
{
    /* Caller (= mzarch hot loop) testuje g_stack_regions_active předem.
     * Defenzivně tu znovu testujeme = stejné chování jako kdyby caller
     * volání skipnul. Předejde to případnému prázdnému find_by_sp scanu. */
    if ( !g_stack_regions_active ) return;

    st_STACK_REGION *r = stack_regions_find_by_sp ( new_sp );
    if ( !r ) return;

    /* Watermark update - vždy, bez ohledu na delta typ. */
    if ( new_sp < r->watermark ) {
        r->watermark = new_sp;
    };

    /* Klasifikace delta. Used signed math pro detekci wrap-aroundu
     * (= SP 0x0001 -> 0xFFFF přes 16-bit underflow je teoreticky možný,
     * ale nereálný v sane Z80 kódu - kdyby k tomu došlo, delta vyjde
     * mimo {-2, +2} a counter se neinkrementuje, watermark se updatne
     * konzervativně). */
    int32_t delta = (int32_t) new_sp - (int32_t) old_sp;
    if ( delta == -2 ) {
        r->push_count++;
    } else if ( delta == +2 ) {
        r->pop_count++;
    };
    /* Ostatní delta (LD SP,nn / EX (SP),HL / INC SP / DEC SP / 16-bit
     * skok) = ignored. Watermark update už proběhl. */
}


/* ============================================================================
 * V6 - persistence definic do cfgmain.ini sekce [STACK_REGIONS]
 * ============================================================================ */

/**
 * @brief Idempotence flag - cfg modul se registruje jen jednou za běh.
 */
static bool s_cfg_registered = false;

/**
 * @brief Per-slot user data pro propagate/save callback. Index slotu
 *        se předává přes void* data parametru cfgelement_set_*_cb.
 *
 * Hodnoty se generují staticky při registraci (= žádný malloc).
 * Velikost STACK_REGIONS_MAX (= 8) odpovídá počtu slotů.
 */
static int s_cfg_slot_index[ STACK_REGIONS_MAX ];

/**
 * @brief Pomocná struktura: dočasná kopie načtených dat ze sekce
 *        [STACK_REGIONS] před commit do g_stack_regions[].
 *
 * Propagate callbacky neaktualizují globální pole přímo, ale plní tento
 * staging area. Po cfgmodule_propagate() se v finalize fázi staging
 * překlopí do g_stack_regions[] s plnou validací (= odmítnutí invalid
 * záznamů, zachování zpětné kompatibility se starým INI bez sekce).
 *
 * @invariant po commit fázi je staging zase reset (= žádné stale data
 * při opakovaném propagate volání).
 */
typedef struct st_STACK_REGION_STAGING {
    char     name[ STACK_REGION_NAME_MAX ];
    unsigned base;          /**< 0..0xFFFF, jinak invalid. */
    unsigned limit;         /**< 0..0xFFFF, jinak invalid. */
    bool     name_seen;     /**< true = propagate callback proběhl pro name. */
    bool     base_seen;
    bool     limit_seen;
} st_STACK_REGION_STAGING;

static st_STACK_REGION_STAGING s_cfg_staging[ STACK_REGIONS_MAX ];
static unsigned s_cfg_staging_count = 0;

/**
 * @brief Propagate cb pro [STACK_REGIONS] count - kolik slotů je validních.
 */
static void cfg_propagate_count ( void *e, void *data )
{
    (void) data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    s_cfg_staging_count = cfgelement_get_unsigned_value ( elm );
    if ( s_cfg_staging_count > STACK_REGIONS_MAX ) {
        s_cfg_staging_count = STACK_REGIONS_MAX;
    };
}

/**
 * @brief Save cb pro [STACK_REGIONS] count - zapíše aktuální g_stack_regions_count.
 */
static void cfg_save_count ( void *e, void *data )
{
    (void) data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    cfgelement_set_unsigned_value ( elm, (unsigned) g_stack_regions_count );
}

/**
 * @brief Propagate cb pro region_N_name - uloží textovou hodnotu do staging slotu.
 */
static void cfg_propagate_name ( void *e, void *data )
{
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    int idx = *(int *) data;
    if ( idx < 0 || idx >= STACK_REGIONS_MAX ) return;
    const char *txt = cfgelement_get_text_value ( elm );
    if ( !txt ) txt = "";
    /* Bezpečná kopie s clip na buffer. */
    memset ( s_cfg_staging[ idx ].name, 0, STACK_REGION_NAME_MAX );
    strncpy ( s_cfg_staging[ idx ].name, txt, STACK_REGION_NAME_MAX - 1 );
    s_cfg_staging[ idx ].name_seen = true;
}

/**
 * @brief Save cb pro region_N_name - zapíše název ze g_stack_regions[N] pokud
 *        N < count, jinak prázdný string.
 */
static void cfg_save_name ( void *e, void *data )
{
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    int idx = *(int *) data;
    if ( idx < 0 || idx >= STACK_REGIONS_MAX ) return;
    const char *txt = ( idx < g_stack_regions_count )
                       ? g_stack_regions[ idx ].name
                       : "";
    cfgelement_set_text_value ( elm, txt );
}

/**
 * @brief Propagate cb pro region_N_base - uloží unsigned hodnotu do staging slotu.
 */
static void cfg_propagate_base ( void *e, void *data )
{
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    int idx = *(int *) data;
    if ( idx < 0 || idx >= STACK_REGIONS_MAX ) return;
    s_cfg_staging[ idx ].base = cfgelement_get_unsigned_value ( elm );
    s_cfg_staging[ idx ].base_seen = true;
}

/**
 * @brief Save cb pro region_N_base.
 */
static void cfg_save_base ( void *e, void *data )
{
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    int idx = *(int *) data;
    if ( idx < 0 || idx >= STACK_REGIONS_MAX ) return;
    unsigned v = ( idx < g_stack_regions_count )
                  ? g_stack_regions[ idx ].base
                  : 0;
    cfgelement_set_unsigned_value ( elm, v );
}

/**
 * @brief Propagate cb pro region_N_limit.
 */
static void cfg_propagate_limit ( void *e, void *data )
{
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    int idx = *(int *) data;
    if ( idx < 0 || idx >= STACK_REGIONS_MAX ) return;
    s_cfg_staging[ idx ].limit = cfgelement_get_unsigned_value ( elm );
    s_cfg_staging[ idx ].limit_seen = true;
}

/**
 * @brief Save cb pro region_N_limit.
 */
static void cfg_save_limit ( void *e, void *data )
{
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    int idx = *(int *) data;
    if ( idx < 0 || idx >= STACK_REGIONS_MAX ) return;
    unsigned v = ( idx < g_stack_regions_count )
                  ? g_stack_regions[ idx ].limit
                  : 0;
    cfgelement_set_unsigned_value ( elm, v );
}

/**
 * @brief Module-level propagate callback - finalizace po propagate
 *        všech elementů. Překlopí staging do g_stack_regions[] přes
 *        validační volání stack_regions_add().
 *
 * Bezpečné chování: invalid záznamy (= nesplňující validaci jména /
 * base > limit / duplicate) se tiše ignorují. Žádný crash, žádná zpráva
 * na user. To umožňuje "zpětnou kompatibilitu" se starým INI bez sekce
 * (= count = 0 default, žádné region_N_*, žádný side effect).
 */
static void cfg_module_propagate_done ( void *m, void *data )
{
    (void) m;
    (void) data;

    /* Validační re-import přes stack_regions_add. Spadne tichý invalid
     * záznam, ostatní pokračují. */
    for ( unsigned i = 0; i < s_cfg_staging_count && i < STACK_REGIONS_MAX; i++ ) {
        st_STACK_REGION_STAGING *s = &s_cfg_staging[ i ];
        /* Záznam musí mít minimum name + base + limit. */
        if ( !s->name_seen || !s->base_seen || !s->limit_seen ) continue;
        /* Při restoring z INI musíme akceptovat všechny 16-bit hodnoty. */
        uint16_t base  = (uint16_t)( s->base & 0xFFFFu );
        uint16_t limit = (uint16_t)( s->limit & 0xFFFFu );
        /* stack_regions_add validuje a vrátí -1 při fail. Žádné error
         * reportingu (= clean fail-soft). */
        (void) stack_regions_add ( s->name, base, limit );
    };
}


void stack_regions_cfg_init ( void )
{
    if ( s_cfg_registered ) return;
    s_cfg_registered = true;

    /* Inicializace per-slot index pole (= konstanty 0..7 v stack data). */
    for ( int i = 0; i < STACK_REGIONS_MAX; i++ ) {
        s_cfg_slot_index[ i ] = i;
    };

    /* Vyčisti staging (= žádné stale hodnoty po opakované propagate). */
    memset ( s_cfg_staging, 0, sizeof ( s_cfg_staging ) );
    s_cfg_staging_count = 0;

    CFGMOD *cmod = cfgroot_register_new_module ( g_cfgmain, "STACK_REGIONS" );
    if ( !cmod ) {
        /* Re-registrace odmítnuta cfgroot (= duplicate). Žádný emu fatal,
         * jen běh bez persistence. */
        return;
    };

    /* Module-level propagate callback - po načtení všech elementů
     * proběhne finalize commit. */
    cfgmodule_set_propagate_cb ( cmod, cfg_module_propagate_done, NULL );

    CFGELM *elm;

    /* count = kolik slotů má být po restore aktivních. Default 0
     * (= žádné regiony při prvním spuštění). */
    elm = cfgmodule_register_new_element ( cmod, "count", CFGENTYPE_UNSIGNED,
                                            0, 0, STACK_REGIONS_MAX );
    cfgelement_set_propagate_cb ( elm, cfg_propagate_count, NULL );
    cfgelement_set_save_cb      ( elm, cfg_save_count, NULL );

    /* Per-slot registrace (max STACK_REGIONS_MAX = 8). Každý slot má
     * 4 elementy s callbacky parameterizovanými přes ukazatel na
     * s_cfg_slot_index[i]. */
    for ( int i = 0; i < STACK_REGIONS_MAX; i++ ) {
        char key[ 32 ];

        snprintf ( key, sizeof ( key ), "region_%d_name", i );
        elm = cfgmodule_register_new_element ( cmod, key, CFGENTYPE_TEXT, "" );
        cfgelement_set_propagate_cb ( elm, cfg_propagate_name, &s_cfg_slot_index[ i ] );
        cfgelement_set_save_cb      ( elm, cfg_save_name, &s_cfg_slot_index[ i ] );

        snprintf ( key, sizeof ( key ), "region_%d_base", i );
        elm = cfgmodule_register_new_element ( cmod, key, CFGENTYPE_UNSIGNED,
                                                0, 0, 0xFFFFu );
        cfgelement_set_propagate_cb ( elm, cfg_propagate_base, &s_cfg_slot_index[ i ] );
        cfgelement_set_save_cb      ( elm, cfg_save_base, &s_cfg_slot_index[ i ] );

        snprintf ( key, sizeof ( key ), "region_%d_limit", i );
        elm = cfgmodule_register_new_element ( cmod, key, CFGENTYPE_UNSIGNED,
                                                0, 0, 0xFFFFu );
        cfgelement_set_propagate_cb ( elm, cfg_propagate_limit, &s_cfg_slot_index[ i ] );
        cfgelement_set_save_cb      ( elm, cfg_save_limit, &s_cfg_slot_index[ i ] );
    };

    /* Parse + propagate - načte hodnoty ze starého INI (pokud existuje)
     * a aktivuje propagate callbacky, které postupně naplní staging
     * a finalize cb pak překlopí do g_stack_regions[]. */
    cfgmodule_parse ( cmod );
    cfgmodule_propagate ( cmod );
}
