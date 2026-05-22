/*
 * stack_history.h - ring buffer historie hodnot SP pro Stack Monitor V2.
 *
 * Zaznamenává časové průběhy SP (= timestamp + hodnota) v okamžiku každé
 * změny SP (= Option B sampling strategie z autoritativního návrhu sekce
 * 4.6). Slouží Stack Monitor panelu pro vykreslení sparkline grafu
 * (= "kde byl SP v čase") a pro detekci "stack creep" (= pomalý monotónní
 * pokles SP) lineární regresí.
 *
 * Aktivační flag g_stack_history_active dovoluje hot-path call site
 * obejít volání stack_history_record při zero overhead - default = false,
 * 1 byte read + branch v existující SP-change kondici.
 *
 * Threading: zápis výhradně z EMU vlákna (přes stack_history_record volaný
 * v safepointu po každé instrukci). UI vlákno čte přes
 * DBGAPI_CMD_STACK_HISTORY_GET (= bulk copy do caller bufferu, atomický
 * snapshot). Nikdy přímý sdílený přístup z UI vlákna.
 *
 * Velikost ring bufferu = STACK_HISTORY_SIZE (4096 vzorků). Pri změnách
 * SP s typickou frekvencí v Z80 (= každých ~30 T-stavů při PUSH/POP)
 * pokrývá zhruba 120000 T-stavů (= 30 ms reálného CPU času při 3.5 MHz).
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

#ifndef STACK_HISTORY_H
#define STACK_HISTORY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Velikost ring bufferu historie SP (počet vzorků).
 *
 * Hodnota N = 4096 dle návrhu (sekce 4.6 doporučení). FIFO - při přetečení
 * se nejstarší vzorek přepíše novým. Paměťová stopa: 4096 * sizeof(sample)
 * = 4096 * 12 B = 48 KB statické paměti.
 */
#define STACK_HISTORY_SIZE 4096


/**
 * @brief Jeden vzorek historie SP.
 *
 * @field sp      Hodnota SP v okamžiku samplování.
 * @field cycles  Snapshot cpu->total_cycles (monotónně rostoucí
 *                T-state counter od emu resetu) v okamžiku samplování.
 *                Slouží jako X-osa sparkline / lineární regrese.
 *
 * @invariant cycles monotónně neklesající mezi po sobě jdoucími sample
 *            (= total_cycles se v Z80 jen inkrementuje). Po emu reset
 *            counter padne na 0 a sample stream začne znovu - history
 *            reset hook se postará o vyprázdnění bufferu.
 */
typedef struct st_STACK_HISTORY_SAMPLE
{
    uint32_t cycles;
    uint16_t sp;
    uint16_t _pad;  /**< Zarovnání struktury na 8 B (= konzistentní layout). */
} st_STACK_HISTORY_SAMPLE;


/**
 * @brief Aktivační flag pro hot-path call site.
 *
 * true = recording aktivní (= UI Stack panel zapnul "SP history" toggle).
 * Hot loop testuje tento flag v existující SP-change kondici - při false
 * (= default) zero overhead (1 byte read + branch, branch predictor
 * naučí "vždy false").
 *
 * Modifikuje výhradně EMU vlákno přes stack_history_set_active (= dbgapi
 * STACK_HISTORY_ENABLE handler v safepointu).
 */
extern bool g_stack_history_active;


/**
 * @brief Resetuje obsah ring bufferu.
 *
 * Vyprázdní write_idx + count. Definice (= alokace, flag) zůstávají.
 * Volá se z emu reset hooku (mzarch_main_reset) a z dbgapi
 * STACK_HISTORY_RESET. Nevypíná recording (= flag zachován).
 *
 * Side effects: g_stack_history_count = 0, g_stack_history_write_idx = 0.
 */
extern void stack_history_reset ( void );


/**
 * @brief Zapne / vypne recording historie SP.
 *
 * Při enable = false dodatečně provede stack_history_reset (= buffer se
 * vyprázdní, aby další zapnutí začalo s čistým stavem). Při enable = true
 * jen nastaví flag (buffer zachován).
 *
 * Volá se výhradně z EMU vlákna (= dbgapi STACK_HISTORY_ENABLE handler).
 *
 * @param enable  true = zapnout recording, false = vypnout + reset bufferu.
 */
extern void stack_history_set_active ( bool enable );


/**
 * @brief Hot-path: zaznamenej jeden vzorek SP do ring bufferu.
 *
 * Volá call site z mzarch hot loopu (= shoda s breakpoints_enforce_sp_threshold
 * a stack_regions_on_sp_change). Caller musí předem testovat
 * g_stack_history_active (= sdílená kondice s ostatními SP-change
 * consumery). Defenzivně se test opakuje uvnitř pro bezpečnost.
 *
 * Strategie sampling = Option B z návrhu (sekce 4.6): sample jen při změně
 * SP, ne každou instrukci. Detekce změny je v caller (= mzarch hot loop
 * porovnává proti g_mzarch_main.bp_sp_prev).
 *
 * FIFO chování: při count == STACK_HISTORY_SIZE se nejstarší vzorek
 * přepíše. write_idx vždy ukazuje na další slot k zápisu, count saturuje
 * na SIZE.
 *
 * Side effects: 1 store struktury (8 B) + 2 inkrementy (write_idx, count).
 * Žádná alokace, žádný syscall.
 *
 * @param sp      Aktuální hodnota SP (post-instruction).
 * @param cycles  Aktuální cpu->total_cycles (timestamp pro X-osu).
 */
extern void stack_history_record ( uint16_t sp, uint32_t cycles );


/**
 * @brief Vrátí počet validních vzorků v ring bufferu.
 *
 * Po naplnění saturuje na STACK_HISTORY_SIZE. Vhodné pro UI ke zjištění
 * kolik dat je k dispozici (= shrnutí "history full vs. partial").
 *
 * @return 0..STACK_HISTORY_SIZE.
 */
extern uint32_t stack_history_get_count ( void );


/**
 * @brief Vrátí vzorek z ring bufferu lineárním indexem 0..count-1.
 *
 * @param i  Lineární index. 0 = nejstarší zaznamenaný vzorek (oldest),
 *           count-1 = nejnovější (newest). Pokud i >= count, vrací
 *           dummy sample {0, 0}.
 *
 * @return Kopie sample (= bezpečné proti hot-path race-condition během
 *         lookupu, ale ne 100 % atomické pro celý průchod - UI obvykle
 *         dělá bulk snapshot přes STACK_HISTORY_GET místo per-index
 *         volání).
 */
extern st_STACK_HISTORY_SAMPLE stack_history_get_sample ( uint32_t i );


/**
 * @brief Bulk-copy posledních `max_count` vzorků do caller bufferu.
 *
 * Vrací vzorky v pořadí "oldest first" (= sample 0 v dst je nejstarší
 * v okénku, sample N-1 nejnovější). Pokud history obsahuje méně než
 * max_count vzorků, zkopíruje jen tolik, kolik je k dispozici.
 *
 * Slouží STACK_HISTORY_GET handleru: UI alokuje pole o velikosti
 * STACK_HISTORY_SIZE, handler ho v safepointu naplní (= atomický
 * snapshot z pohledu UI vlákna).
 *
 * @param dst        Caller buffer pro vzorky (alokuje UI).
 * @param max_count  Maximální počet vzorků k zápisu (= velikost dst pole).
 *
 * @return Počet skutečně zapsaných vzorků (0..max_count).
 */
extern uint32_t stack_history_copy_recent ( st_STACK_HISTORY_SAMPLE *dst,
                                             uint32_t max_count );


/**
 * @brief Spočítá lineární regresi (slope) hodnoty SP přes posledních
 *        `window_size` vzorků.
 *
 * Slope vyjadřuje "průměrná změna SP na jeden T-state v okénku". Záporná
 * hodnota = SP v čase klesá (= možný stack creep / leak). Kladná hodnota
 * = SP roste (= POP-heavy fáze). Hodnota okolo 0 = SP oscilace bez
 * trendu (normální push/pop balancing).
 *
 * Vzorec: slope = (n * sum(x*y) - sum(x) * sum(y))
 *               / (n * sum(x*x) - sum(x)^2)
 * kde x = cycles vzorku, y = sp vzorku, n = počet vzorků v okénku.
 *
 * Pokud window_size > count, použije se count. Pokud count < 2, vrací 0
 * (= nedostatek dat pro slope).
 *
 * @param window_size  Počet posledních vzorků k použití (typicky 256).
 *
 * @return Slope (SP/cycle) - např. -0.01 = SP klesá o 0.01 jednotek SP
 *         na T-state v průměru. NAN se nevrací (= pri degenerate
 *         denominátoru vrací 0).
 */
extern float stack_history_compute_slope ( uint32_t window_size );


/**
 * @brief V6: registruje cfgmain sekci [STACK_HISTORY] pro persistenci
 *        runtime nastavení (= recording enable flag).
 *
 * Sekce obsahuje:
 *   enabled = bool 0/1
 *
 * Hot-path overhead při enabled = 0 zůstává nulový (= flag se před
 * propagate inicializuje na 0, případné enable=1 ze starého INI se
 * uplatní okamžitě přes stack_history_set_active).
 *
 * Ring buffer obsahu (= vzorky) se NEpersistuje - je to čistý runtime
 * stav, který by stejně nebyl konzistentní napříč restarty (= cycles
 * counter se vždy resetuje na 0 při startu emu).
 *
 * Volá se z debugger_init() po stack_history základní inicializaci.
 * Idempotentní.
 *
 * @pre g_cfgmain inicializován.
 */
extern void stack_history_cfg_init ( void );


#ifdef __cplusplus
}
#endif

#endif /* STACK_HISTORY_H */
