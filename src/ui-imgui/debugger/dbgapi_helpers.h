/*
 * dbgapi_helpers.h - vrstva tenkých UI helperů nad dbgapi CMDRQ kanálem
 *
 * UI vlákno (= ImGui handlery, SDL klávesové callbacky) by mělo komunikovat
 * s emulátorem pouze přes dbgapi frontu CMDRQ. Tyto helpery zaobalují
 * dbgapi_ui_submit_cmd_sync() pro nejčastější příkazy a skrývají alokaci
 * parametrických struktur i timeout konstanty.
 *
 * Vlastnictví paměti:
 * - Helpery alokují parametrické struktury na stacku (lokální proměnné).
 *   submit_cmd_sync() je synchronní, takže po návratu jsou data zase
 *   volně použitelná - žádný heap, žádný caller-side cleanup.
 * - Pro CMD_MEM_WRITE caller předává buffer; helper si ho jen mapuje
 *   do st_DBGAPI_MEM_PARAM (nezalokovává kopii).
 *
 * Thread safety:
 * - Volat výhradně z UI vlákna (= SDL event loop, ImGui render). Helpery
 *   blokují UI vlákno do doručení odpovědi z EMU vlákna (default timeout
 *   DBG_UI_DEFAULT_TIMEOUT_MS).
 *
 * Návratová hodnota:
 * - true  = příkaz byl úspěšně doručen i zpracován
 * - false = timeout / fronta plná / dbgapi se ukončuje / EMU vrátil chybu
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
#ifndef UI_DBGAPI_HELPERS_H
#define UI_DBGAPI_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "emulator/debugger/dbgapi_cmdrq.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Default timeout pro synchronní CMDRQ submit z UI vlákna.
 *
 * 200 ms odpovídá ~10 frame periodám 50 Hz emulace. Pokrývá běžné
 * případy (= drain queue per-frame screen_done event), avšak je
 * dost krátký aby UI nezaseklo při skutečném emu deadlocku.
 *
 * [neověřeno měřením] - empirická hodnota, A.3.5 acceptance.
 */
#define DBG_UI_DEFAULT_TIMEOUT_MS 200

/* ============================================================================
 * Řízení emulace
 * ============================================================================ */

/**
 * @brief Pozastaví emulaci přes CMD_PAUSE.
 *
 * @return true při úspěchu, false při chybě / timeoutu.
 */
bool dbg_ui_pause(void);

/**
 * @brief Spustí emulaci přes CMD_RUN.
 *
 * @return true při úspěchu, false při chybě / timeoutu.
 */
bool dbg_ui_run(void);

/**
 * @brief Toggle pauza emulace - pomocný shortcut pro Alt+P / menu items.
 *
 * Přečte aktuální stav přes CMD_IS_RUNNING, pošle PAUSE nebo RUN podle
 * potřeby. Dvě CMDRQ za jednu operaci.
 *
 * @return true při úspěchu obou operací, false jinak.
 */
bool dbg_ui_pause_toggle(void);

/**
 * @brief Reset emulátoru přes CMD_RESET.
 *
 * Asynchronní - reset se provede v příští iteraci mzarch_main. Helper
 * jen čeká na potvrzení vložení do fronty.
 *
 * @return true při úspěchu, false při chybě / timeoutu.
 */
bool dbg_ui_reset(void);

/* ============================================================================
 * Krokování
 * ============================================================================ */

/**
 * @brief Step Into přes CMD_STEP_INTO - jeden krok přes aktuální instrukci.
 *
 * @return true při úspěchu, false při chybě / timeoutu.
 */
bool dbg_ui_step_into(void);

/**
 * @brief Step Over přes CMD_STEP_OVER - přeskočí CALL/RST/blokové instrukce.
 *
 * @return true při úspěchu, false při chybě / timeoutu.
 */
bool dbg_ui_step_over(void);

/**
 * @brief Run To Address přes CMD_RUN_TO - běh do dané adresy.
 *
 * @param addr Cílová adresa (16-bit).
 * @return true při úspěchu, false při chybě / timeoutu.
 */
bool dbg_ui_run_to(uint16_t addr);

/* ============================================================================
 * Z80 registry
 * ============================================================================ */

/**
 * @brief Zápis do Z80 registru přes CMD_SET_REG.
 *
 * @param reg_id Identifikátor registru (z80_reg_t casted na uint8_t).
 * @param value  Nová 16-bit hodnota.
 * @return true při úspěchu, false při chybě / timeoutu.
 */
bool dbg_ui_set_reg(uint8_t reg_id, uint16_t value);

/* ============================================================================
 * Paměť
 * ============================================================================ */

/**
 * @brief Zápis bloku paměti přes CMD_MEM_WRITE.
 *
 * Caller poskytuje vstupní buffer. Helper sestaví st_DBGAPI_MEM_PARAM
 * a předá ho dispatcheru. Buffer musí žít po celou dobu volání (= je
 * synchronní, takže do návratu helperu).
 *
 * @param addr Cílová adresa.
 * @param buf  Vstupní data (vlastní caller).
 * @param len  Počet bajtů k zápisu.
 * @return true při úspěchu, false při chybě / timeoutu / NULL parametry.
 */
bool dbg_ui_mem_write(uint16_t addr, const uint8_t *buf, uint16_t len);

/* ============================================================================
 * Breakpointy
 * ============================================================================ */

/**
 * @brief Přidá execution BP přes CMD_BP_ADD.
 *
 * @param addr   Adresa BP.
 * @param out_id Pokud != NULL, sem se zapíše přidělené ID nového BP.
 * @return true při úspěchu, false při chybě / timeoutu.
 */
bool dbg_ui_bp_add(uint16_t addr, int *out_id);

/**
 * @brief Odebere BP podle ID přes CMD_BP_REMOVE.
 *
 * @param id ID breakpointu.
 * @return true při úspěchu, false při chybě / timeoutu / neexistujícím ID.
 */
bool dbg_ui_bp_remove(int id);

/**
 * @brief Selektivní update polí existujícího BP přes CMD_BP_UPDATE.
 *
 * Plochý snapshot polí + bitmask update_mask řídí které aplikovat. Volá
 * EMU thread, který iteruje mask + volá existující `breakpoints_set_*()`.
 * Caller drží alokaci `p` + jeho stringů do návratu (sync cmd).
 *
 * @param p Vstupní payload (id existujícího BP, update_mask, fieldy).
 * @return true při úspěchu, false pokud BP neexistuje / timeout.
 */
bool dbg_ui_bp_update(const st_DBGAPI_BP_UPDATE_PARAM *p);

/**
 * @brief Quick toggle enabled flag přes CMD_BP_SET_ENABLED.
 *
 * Forwarder, žádný side-effect na ostatní pole. Použití: BP list checkbox,
 * disasm right-click toggle, drag-drop tree.
 *
 * @param id      ID existujícího BP.
 * @param enabled Nový stav.
 * @return true při úspěchu, false pokud BP neexistuje / timeout.
 */
bool dbg_ui_bp_set_enabled(int id, bool enabled);

/**
 * @brief Quick reparent BP do skupiny přes CMD_BP_SET_PARENT.
 *
 * Forwarder, žádný side-effect. Použití: drag-drop přesun mezi skupinami,
 * "Clear parent" v context menu.
 *
 * @param id        ID existujícího BP.
 * @param parent_id -1 = root (bez skupiny), jinak ID existující skupiny.
 * @return true při úspěchu, false pokud BP neexistuje / timeout.
 */
bool dbg_ui_bp_set_parent(int id, int parent_id);

/**
 * @brief Atomický create + init polí přes CMD_BP_CREATE_WITH_INIT.
 *
 * Volá `breakpoints_add_auto(addr, name, parent)` na EMU vlákně. Po
 * úspěchu naplní `p->id` přiděleným ID + aplikuje update_mask přes
 * stejnou helper cestu co BP_UPDATE. Caller MUSÍ nastavit `p->id = -1`
 * na vstupu (= invariant CREATE).
 *
 * Pokud `out_id != NULL`, sem se zapíše přidělené ID (= zkratka pro
 * čtení z `p->id` po návratu).
 *
 * @param p      Vstupní payload (id=-1, update_mask + fieldy).
 *               Po návratu p->id obsahuje přidělené ID nebo zůstává -1.
 * @param out_id Volitelný výstup nového ID.
 * @return true při úspěchu, false pokud create selhal / timeout.
 */
bool dbg_ui_bp_create_with_init(st_DBGAPI_BP_UPDATE_PARAM *p, int *out_id);

/**
 * @brief Přidá novou skupinu BP přes CMD_BPGRP_ADD.
 *
 * @param name   Jméno nové skupiny.
 * @param parent -1 = root, jinak ID existující rodičovské skupiny.
 * @param out_id Pokud != NULL, sem se zapíše přidělené ID nové skupiny.
 * @return true při úspěchu, false při chybě / timeoutu.
 */
bool dbg_ui_bpgrp_add(const char *name, int parent, int *out_id);

/**
 * @brief Odebere skupinu BP podle ID přes CMD_BPGRP_REMOVE.
 *
 * @param id ID existující skupiny.
 * @return true při úspěchu, false pokud skupina neexistuje / timeout.
 */
bool dbg_ui_bpgrp_remove(int id);

/**
 * @brief Selektivní update polí existující skupiny přes CMD_BPGRP_UPDATE.
 *
 * Plochý snapshot enabled/name/colors/parent + bitmask `update_mask` řídí
 * které aplikovat. Caller drží alokaci `p` + jeho `name` do návratu.
 *
 * @param p Vstupní payload (id existující skupiny, update_mask, fieldy).
 * @return true při úspěchu, false pokud skupina neexistuje / timeout.
 */
bool dbg_ui_bpgrp_update(const st_DBGAPI_BPGRP_UPDATE_PARAM *p);

#ifdef __cplusplus
}
#endif

#endif /* UI_DBGAPI_HELPERS_H */
