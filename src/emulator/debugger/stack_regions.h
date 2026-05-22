/*
 * stack_regions.h - registr pojmenovaných stack regionů + runtime statistiky.
 *
 * Stack region = pojmenovaný záznam <base, limit> v adresním prostoru Z80,
 * kde SP může legitimně bydlet. Drží runtime statistiky: watermark
 * (= nejnižší SP zaznamenaný v rámci regionu), push_count a pop_count
 * (= počet poklesů / nárůstů SP o 2). Slouží Stack Monitor panelu pro
 * vizualizaci hloubky zásobníku, low-water mark a aktivity.
 *
 * Aktivační flag g_stack_regions_active dovoluje hot-path call site
 * obejít volání hook funkce při zero overhead (= žádný region definovaný
 * -> 1 byte read + branch, branch predictor naučí "vždy false").
 *
 * Threading: registr modifikuje pouze EMU vlákno (přes dbgapi handlery
 * v safepointu mezi instrukcemi). UI vlákno čte přes
 * DBGAPI_CMD_STACK_REGIONS_* (= shoda s BP storage patternem - UI nikdy
 * přímý zápis). V1 si UI panel udržuje lokální cache snapshotu, který
 * obnovuje při refresh ticku.
 *
 * V1 omezení:
 *   - Pevně omezený počet (STACK_REGIONS_MAX = 8) - V1 nepotřebuje víc.
 *   - Bez overlap detekce při add (= V1 vrací první match při find_by_sp).
 *
 * V6 přidává persistenci definic do cfgmain.ini sekce [STACK_REGIONS]:
 * pole count + per-slot triplety (name/base/limit). Watermark a counters
 * se NEpersistují (= runtime stav, reset při emu startu).
 * Viz stack_regions_cfg_init().
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

#ifndef STACK_REGIONS_H
#define STACK_REGIONS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Maximální počet stack regionů.
 *
 * V1 hodnota = 8. Slot navíc je nepravděpodobný (= typický program
 * má 1-2 stacky, CP/M BIOS 3-4). Vyšší hodnota by zbytečně rozšířila
 * hot-path lookup smyčku ve find_by_sp.
 */
#define STACK_REGIONS_MAX 8


/**
 * @brief Maximální délka jména regionu včetně zakončovacího '\0'.
 *
 * Validace v stack_regions_add: jméno musí být neprázdné, obsahovat jen
 * [a-zA-Z0-9_] a sedět do bufferu (= cap při kopírování).
 */
#define STACK_REGION_NAME_MAX 32


/**
 * @brief Definice + runtime stav jednoho stack regionu.
 *
 * @invariant base > limit (= region netriviální, alespoň 1 word).
 * @invariant watermark v intervalu <limit..base>, init = base.
 * @invariant Po reset_watermark: watermark = base, push_count = pop_count = 0.
 */
typedef struct st_STACK_REGION
{
    char     name[ STACK_REGION_NAME_MAX ]; /**< Uživatelské jméno, '\0'-terminated. */
    uint16_t base;                          /**< Vrchol (nejvyšší adresa, "first push" pozice). */
    uint16_t limit;                         /**< Dno (nejnižší povolená adresa). */
    uint16_t watermark;                     /**< Runtime: nejnižší SP zaznamenaný v <limit..base>. */
    uint64_t push_count;                    /**< Runtime: PUSH-like (SP klesl o 2 v regionu). */
    uint64_t pop_count;                     /**< Runtime: POP-like (SP vzrostl o 2 v regionu). */
} st_STACK_REGION;


/**
 * @brief Pole stack regionů. Indexované 0..g_stack_regions_count-1.
 *
 * Storage je statické (= žádné heap alloc, žádné free hazardy mezi
 * UI a EMU vláknem). Modifikuje výhradně EMU vlákno (přes dbgapi handlery).
 */
extern st_STACK_REGION g_stack_regions[ STACK_REGIONS_MAX ];


/**
 * @brief Aktuální počet definovaných regionů (0..STACK_REGIONS_MAX).
 */
extern int g_stack_regions_count;


/**
 * @brief Aktivační flag pro hot-path call site.
 *
 * true = alespoň jeden region definovaný (= g_stack_regions_count > 0).
 * Hot loop testuje tento flag PŘED voláním stack_regions_on_sp_change.
 * Při false (= default) = 1 byte read + branch, zero impact.
 *
 * Updatuje se v stack_regions_add / stack_regions_remove (= jediná
 * místa kde se count mění).
 */
extern bool g_stack_regions_active;


/**
 * @brief Přidá nový stack region.
 *
 * Side effects: zapíše do g_stack_regions[g_stack_regions_count++],
 * inkrementuje count, aktualizuje g_stack_regions_active.
 *
 * Watermark se inicializuje na base, counters na 0.
 *
 * @param name             Jméno regionu (cap na STACK_REGION_NAME_MAX-1,
 *                         validace [a-zA-Z0-9_]+, neprázdné). NULL = error.
 * @param base             Vrchol (>= limit + 2, jinak error).
 * @param limit            Dno (< base, jinak error).
 *
 * @return Index 0..STACK_REGIONS_MAX-1 při úspěchu, -1 při chybě
 *         (full, invalid args, name conflict).
 */
extern int stack_regions_add ( const char *name, uint16_t base, uint16_t limit );


/**
 * @brief Edituje existující stack region.
 *
 * V7: změní name/base/limit u regionu @p idx. Resetuje runtime statistiky
 * (watermark = base, push_count = pop_count = 0), protože původní stats
 * patřily k jinému rozsahu adres a nedávaly by smysl.
 *
 * Validace argumentů:
 *  - @p idx musí být v <0, g_stack_regions_count)
 *  - @p name neprázdný, max STACK_REGION_NAME_MAX-1 znaků, regex
 *    [a-zA-Z0-9_]+
 *  - @p base > @p limit (= netriviální region)
 *  - název nesmí kolidovat s jiným regionem (= duplicate name jiný idx)
 *  - rozsah <limit..base> nesmí překrývat s jiným regionem (overlap),
 *    overlap detekce vyloučí @p idx samotný (= edit se sám se sebou
 *    překrývat nesmí kontrolovat).
 *
 * Side effects: zapíše do g_stack_regions[idx] nové name/base/limit a
 * resetuje watermark + counters. Aktivační flag g_stack_regions_active
 * se neměnil (= count zůstává stejný).
 *
 * Hot-path safety: volat jen z EMU vlákna v safepointu (= dbgapi handler).
 * UI vlákno NIKDY nepíše přímo do globálního pole.
 *
 * @param idx    Index regionu k editaci.
 * @param name   Nový label, '\0'-terminated, max STACK_REGION_NAME_MAX-1.
 * @param base   Nový vrchol (= nejvyšší adresa).
 * @param limit  Nové dno (< base).
 * @return true při úspěšné editaci, false při invalid args / duplicate
 *         name / overlap.
 */
extern bool stack_regions_edit ( int idx, const char *name, uint16_t base, uint16_t limit );


/**
 * @brief Detekuje překryv rozsahu <limit..base> s jiným regionem.
 *
 * V7: pomocná funkce pro Edit (volitelně pro Add). Iteruje přes všechny
 * platné regiony kromě @p idx_ignore a vrací index prvního overlapujícího
 * regionu, nebo -1 pokud žádný překryv.
 *
 * Match podmínka: dva intervaly <a.limit..a.base> a <b.limit..b.base> se
 * překrývají, pokud a.limit <= b.base AND a.base >= b.limit. Inkluzivní
 * hranice (= shoda v jediném bajtu = overlap).
 *
 * Pre: base > limit (= jinak nedefinované chování, ale stack_regions_edit
 * to validuje předem).
 *
 * @param idx_ignore Index regionu, který se má z kontroly vyloučit
 *                   (= edit case). Pro novou region (Add) předat -1.
 * @param base       Nový vrchol kandidátského rozsahu.
 * @param limit      Nové dno kandidátského rozsahu.
 * @return Index prvního overlapujícího regionu nebo -1 pokud žádný.
 */
extern int stack_regions_check_overlap ( int idx_ignore, uint16_t base, uint16_t limit );


/**
 * @brief Odebere region na zadaném indexu.
 *
 * Side effects: compact - ostatní regiony se posunou doleva, count--,
 * aktivační flag se přepočte.
 *
 * @param index  0..g_stack_regions_count-1.
 * @return true při úspěchu, false při index mimo rozsah.
 */
extern bool stack_regions_remove ( int index );


/**
 * @brief Resetuje runtime statistiky regionu na default hodnoty.
 *
 * watermark = base, push_count = 0, pop_count = 0. Definice (name, base,
 * limit) se nemění.
 *
 * @param index  0..g_stack_regions_count-1.
 * @return true při úspěchu, false při index mimo rozsah.
 */
extern bool stack_regions_reset_watermark ( int index );


/**
 * @brief Resetuje runtime statistiky všech regionů (= ekvivalent "emu reset").
 *
 * Volá se z mzarch_main_reset. Zachová definice, nuluje watermarks +
 * counters. Ekvivalent "spustit znovu od začátku".
 */
extern void stack_regions_reset_all_stats ( void );


/**
 * @brief Najde region obsahující zadaný SP.
 *
 * Match: limit <= sp <= base. Pokud regions overlapují, vrátí první
 * match v pořadí pole. V1 overlap detection není (= add jen warninguje
 * implicitně přes prvenství při lookup).
 *
 * Volat jen pokud g_stack_regions_active (= jinak zbytečná smyčka).
 *
 * @param sp   Hodnota SP k vyhledání.
 * @return Pointer na st_STACK_REGION při match, NULL při no-match.
 */
extern st_STACK_REGION *stack_regions_find_by_sp ( uint16_t sp );


/**
 * @brief Hot-path hook: SP změnil hodnotu, update watermark + counters.
 *
 * Volá call site z mzarch hot loopu (post-instruction, jen pokud SP
 * změnil hodnotu). Caller MUSÍ test g_stack_regions_active předem
 * (= obvykle sdílí condition s SP_THRESHOLD BP hookem).
 *
 * Logika:
 *  1. find_by_sp(new_sp). Pokud žádný region = return.
 *  2. Pokud new_sp < region->watermark: watermark = new_sp.
 *  3. delta = (int32_t)new_sp - (int32_t)old_sp (= -2 / +2 / jiné):
 *       -2  => push_count++  (= PUSH/CALL/RST/INT-ack)
 *       +2  => pop_count++   (= POP/RET)
 *       other => ignored (= LD SP,nn / EX (SP),HL / INC SP / atd.)
 *     Watermark update se děje vždy bez ohledu na delta.
 *
 * POZN: INT acknowledge dispatch (IM 0/1/2) generuje implicit PUSH PC,
 * tj. delta = -2. push_count tedy zahrnuje i INT push, nikoliv jen
 * user-level PUSH. To je zamýšlené (= reflektuje skutečnou stack
 * aktivitu).
 *
 * Side effects: modifikuje watermark + counters jednoho regionu.
 * Nesahá na definici (base/limit/name).
 *
 * @param old_sp  SP před aktuální instrukcí.
 * @param new_sp  SP po aktuální instrukci.
 */
extern void stack_regions_on_sp_change ( uint16_t old_sp, uint16_t new_sp );


/**
 * @brief V5 hot-path helper: rozhodne, zda zápis na @p addr cílí do
 *        nějakého aktivního stack regionu.
 *
 * Volá se z memory write hooku v mz{800,700,1500}_memory.c uvnitř bloku
 * @c TEST_DEBUGGER_MHMAP_ACTIVE. Výsledek slouží jako klasifikace W vs S
 * pro Memory Heatmap.
 *
 * Caller MUSÍ test @c g_stack_regions_active předem (= jinak vrací false
 * implicitně, smyčka přes nulový count). Funkce sama nemá tuto guard
 * podmínku jako early return aby compiler mohl inline + DCE call site.
 *
 * Linear lookup přes max 8 prvků (= STACK_REGIONS_MAX). Match podmínka:
 * @c limit <= addr <= @c base. Vrátí true při prvním match.
 *
 * @param addr  Bus adresa cílové buňky.
 * @return true, pokud addr leží v <limit..base> alespoň jednoho regionu.
 */
static inline bool stack_regions_classify_write ( uint16_t addr )
{
    for ( int i = 0; i < g_stack_regions_count; i++ ) {
        const st_STACK_REGION *r = &g_stack_regions[ i ];
        if ( addr >= r->limit && addr <= r->base ) {
            return true;
        }
    }
    return false;
}


/**
 * @brief V12: vrátí index regionu obsahujícího danou adresu, nebo -1.
 *
 * Pomocná funkce pro UI vrstvu (Stack Monitor hex tabulka - per-řádek
 * background color podle regionu). Analog @ref stack_regions_classify_write,
 * ale vrací konkrétní index regionu místo boolean flagu.
 *
 * Linear lookup přes max 8 prvků (= STACK_REGIONS_MAX). Match podmínka:
 * @c limit <= addr <= @c base. Vrátí index prvního match (= shoda
 * s @ref stack_regions_find_by_sp při overlapujících regionech).
 *
 * Caller by měl předem testovat @c g_stack_regions_active (= jinak
 * smyčka zbytečná). Funkce sama tuto guard podmínku nemá, aby compiler
 * mohl inline + DCE call site.
 *
 * Thread safety: čte globální storage. Při čtení z UI vlákna nutno
 * vědět, že storage modifikuje pouze EMU vlákno v safepointu - v praxi
 * UI vlákno pracuje s vlastním snapshotem (st_DBGAPI_STACK_REGIONS_LIST_PARAM
 * cache). Tato funkce slouží primárně EMU vláknu / testům.
 *
 * @param addr  Bus adresa cílové buňky.
 * @return Index regionu (0..g_stack_regions_count-1) nebo -1.
 */
static inline int stack_regions_find_by_addr ( uint16_t addr )
{
    for ( int i = 0; i < g_stack_regions_count; i++ ) {
        const st_STACK_REGION *r = &g_stack_regions[ i ];
        if ( addr >= r->limit && addr <= r->base ) {
            return i;
        }
    }
    return -1;
}


/**
 * @brief V6: registruje cfgmain sekci [STACK_REGIONS] pro persistenci
 *        definic regionů.
 *
 * Sekce má strukturu:
 *   count          = N (0..STACK_REGIONS_MAX)
 *   region_<i>_name        = textové jméno
 *   region_<i>_base        = unsigned (0..0xFFFF)
 *   region_<i>_limit       = unsigned (0..0xFFFF)
 *
 * Vždy se registruje plný počet slotů (STACK_REGIONS_MAX), neaktivní
 * sloty mají default hodnoty a při save se zapíší jen pokud i < count.
 *
 * Starší INI mohou obsahovat klíč region_<i>_auto_detect (z V6
 * placeholder, nikdy implementováno). cfgfile parser unknown klíče
 * tiše ignoruje (= fail-soft, viz cfgmodule_parse), takže load
 * starého INI je beze ztráty zpětné kompatibility.
 *
 * Persistence pokrývá pouze definice. Watermark, push_count, pop_count
 * jsou runtime stav a NEpersistují se (= reset při každém startu emu).
 *
 * Volá se z debugger_init() po stack_regions základní inicializaci.
 * Idempotentní - opakované volání se ignoruje.
 *
 * Side effects: po načtení zapíše do g_stack_regions[] / g_stack_regions_count
 * / g_stack_regions_active. Pokud konfigurace neobsahuje sekci (= starý
 * INI), nechá pole prázdné (= zpětná kompatibilita).
 *
 * @pre g_cfgmain inicializován.
 * @pre Volat z EMU vlákna nebo před jeho startem.
 */
extern void stack_regions_cfg_init ( void );


#ifdef __cplusplus
}
#endif

#endif /* STACK_REGIONS_H */
