/*
 * dbg_disassembled.h — Sekce Disassembled hlavního okna debuggeru
 *
 * Nejdůležitější sekce debuggeru zobrazující disassemblovaný kód.
 * Skládá se ze tří částí:
 *
 * 1. HORNÍ TABULKA (HISTORIE)
 *    Ring buffer posledních vykonaných instrukcí. Pouze pro čtení.
 *    Po refreshi se automaticky scrolluje na nejnovější záznam.
 *
 * 2. DOLNÍ TABULKA (DISASSEMBLY)
 *    Disassemblovaná paměť od adresy focusu. Interaktivní:
 *    - Jednořádkový selektor (vždy 1 řádek aktivní = focus)
 *    - Navigace klávesnicí (UP/DOWN/PgUp/PgDown)
 *    - Context menu (Set as Breakpoint, Set as PC, Focus To..., etc.)
 *    - Double click / Enter → Inline Assembler
 *
 * 3. ŠOUPÁTKO (SLIDER)
 *    Vertikální slider (0x0000–0xFFFF) řídící adresu focusu.
 *    Mění obsah dolní tabulky.
 *
 * Mezi horní a dolní tabulkou je horizontální posuvník (splitter),
 * kterým uživatel nastavuje poměr výšek.
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

#include <stdint.h>
#include <stdbool.h>

#ifndef DBG_DISASSEMBLED_H
#define DBG_DISASSEMBLED_H

/**
 * @brief Opaque handle na instanci sekce Disassembled.
 *
 * Definice je v dbg_disassembled.cpp - volající nesahá přímo na fieldy.
 * Existuje globální singleton "main" instance, kterou používá hlavní
 * okno debuggeru. Sekundární okna (plán č. 7) si vytvoří vlastní
 * instance přes dbg_disasm_view_create().
 */
typedef struct DisassembledView DisassembledView;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Vytvoří novou instanci sekce Disassembled.
 *
 * Alokuje strukturu DisassembledView, vynuluje cache a nastaví výchozí
 * hodnoty stavu. Hlavní singleton instance ("main") sdílí UI stav s
 * g_dbg_ui (focus_addr, selected_row, ...) - sekundární instance budou
 * mít vlastní lokální storage (zatím není exposed pro plán č. 7).
 *
 * @param enable_history    true = renderovat horní tabulku historie + splitter
 * @param window_id         stabilní ID instance ("main", "2", ..., "5"),
 *                          držený řetězec musí žít po celou dobu existence
 *                          instance (statický string je OK)
 * @return                  ukazatel na novou instanci, NULL při chybě alokace
 *
 * @note Auto-follow PC řídí per-instance dynamic flag follow_pc (default
 *       ON pro "main", OFF pro sekundární). Uživatel ho přepíná v hlavičce
 *       view. Hlavní instance používá shim dbg_disassembled_render(), který
 *       získá lazy-vytvořený singleton "main".
 */
DisassembledView *dbg_disasm_view_create(bool enable_history,
                                         const char *window_id);

/**
 * @brief Uvolní instanci a vynuluje její buffery.
 *
 * @param self ukazatel na instanci, smí být NULL (no-op)
 */
void dbg_disasm_view_destroy(DisassembledView *self);

/**
 * @brief Vykreslí instanci sekce Disassembled do aktuálního ImGui kontejneru.
 *
 * @param self           instance (nesmí být NULL)
 * @param section_height dostupná výška sekce v pixelech
 *
 * @pre Volat pouze z UI vlákna mezi začátkem a koncem framu ImGui.
 */
void dbg_disasm_view_render(DisassembledView *self, float section_height);

/**
 * @brief Spocita doporucenou vychozi sirku okna obsahujiciho disasm view.
 *
 * Vraci dynamickou hodnotu zalozenou na aktualnim ImGui style + aktualnim
 * fontu (CalcTextSize). Vypocet zakladni komponenty:
 *
 *   - hlavicka disasm sekce: text entry "Address or symbol" + 2 checkboxy
 *     (Follow PC, T-states) + spacing
 *   - vertikalni slider (~22 px)
 *   - window padding + border + scrollbar
 *
 * Hodnota se pouziva v `SetNextWindowSize(..., ImGuiCond_FirstUseEver)`
 * pri prvnim spusteni okna pro vychozi sirku.
 *
 * @return doporucena sirka v pixelech (typicky 480..640 px)
 *
 * @pre Volat pouze z UI vlakna mezi NewFrame a EndFrame ImGui.
 */
float dbg_disasm_view_compute_default_width(bool include_table_extras);

/**
 * @brief Zjistí, zda zadaná adresa leží v některém aktuálně rendrovaném
 *        řádku dolní tabulky (= viditelné instrukci včetně mid-instruction
 *        pozice pro vícebajtové opcody).
 *
 * Cache se aktualizuje při každém renderu - vrací stav z posledního
 * dokončeného framu.
 *
 * @param self instance (nesmí být NULL)
 * @param addr cílová adresa
 * @return     true pokud addr leží uvnitř některého řádku
 */
bool dbg_disasm_view_is_addr_visible(const DisassembledView *self,
                                     uint16_t addr);

/**
 * @brief Vrátí pointer na hlavní DisassembledView instanci (= "main"
 *        v hlavním Debugger okně), nebo NULL pokud ještě nebyla
 *        vytvořena (= debugger_window se ještě neotevřel ani jednou).
 *
 * Lifetime: instance je file-static singleton, žije po celou session.
 * Caller pointer nesmí free.
 *
 * Použití: integrace z jiných UI modulů (= Bookmarks, sekundární okna)
 * které potřebují focus na main view bez závislosti na file-static
 * implementačním detailu.
 *
 * @return pointer na main view nebo NULL
 */
DisassembledView *dbg_disasm_view_get_main(void);

/**
 * @brief Nastaví focus_addr instance.
 *
 * Slouží zejména pro restore stavu po startu (load z config). Resetuje
 * selected_row na 0 a vyžádá refresh disassembly cache. Pro hlavní
 * instanci ("main") zapisuje do g_dbg_ui.focus_addr (= viditelné i
 * konzumentům čtoucím g_dbg_ui přímo).
 *
 * @param self instance (nesmí být NULL)
 * @param addr nová adresa focusu
 */
void dbg_disasm_view_set_focus_addr(DisassembledView *self, uint16_t addr);

/**
 * @brief Vrací aktuální focus_addr instance.
 *
 * Slouží zejména pro save stavu (export do config). Pro hlavní instanci
 * vrací g_dbg_ui.focus_addr.
 *
 * @param self instance (nesmí být NULL)
 * @return     adresa focusu
 */
uint16_t dbg_disasm_view_get_focus_addr(const DisassembledView *self);

/**
 * @brief Aplikuje uživatelské focus na danou adresu v této instanci.
 *
 * Vysokoúrovňová varianta @ref dbg_disasm_view_set_focus_addr určená
 * pro user-iniciované akce (= context menu, Focus to dialog, bookmarks,
 * další UI cesty kde uživatel explicitně skočil na adresu).
 *
 * Provádí navíc oproti low-level setteru:
 *   - Auto-disable @c follow_pc pokud je zapnutý a emulátor NENÍ v pauze.
 *     Bez toho by Follow PC v dalším framu okamžitě přepsal focus_addr
 *     zpět na PC a uživatelovo focus by zmizelo. Pokud je emulátor
 *     v pauze, follow_pc se zachovává (= nemá bezprostřední side effect
 *     a uživatel ho explicitně OFF nechtěl).
 *   - Set focus_addr a selected_row=0
 *   - dbg_refresh_request() (= force cache rebuild v dalším framu)
 *
 * Pro low-level set bez auto-disable (= interní animation tick, slider
 * drag, klávesová navigace) použij @ref dbg_disasm_view_set_focus_addr.
 *
 * @param self instance (smí být NULL = no-op)
 * @param addr cílová adresa
 */
void dbg_disasm_view_focus_to(DisassembledView *self, uint16_t addr);

/**
 * @brief Zajistí otevření cílové disasm instance + focus na adresu.
 *
 * Vysokoúrovňový helper pro "Show in Disassembly #N" menu položky a
 * bookmarks. Sjednocuje "ensure window open" + "focus + auto-disable
 * follow_pc" do jednoho volání nezávisle na cílovém slotu.
 *
 * Mapování slotů:
 *   - @c slot_idx == 0 = main DisassembledView (= disasm v hlavním
 *     debug okně). Pokud je hlavní debug okno zavřené, otevře ho přes
 *     @c debugger_show_main_window. Následně @ref dbg_disasm_view_focus_to
 *     na main instanci (pokud už existuje).
 *   - @c slot_idx 1..4 = sekundární okna Disassembly #2..#5. Deleguje
 *     na @c dbg_extra_disasm_show_window(slot_idx + 1, addr), který
 *     interně řeší ensure-open, persisted_focus pro lazy create
 *     i auto-disable follow_pc na běžící instanci.
 *
 * Mimo rozsah 0..4 = no-op.
 *
 * @param slot_idx 0 = main, 1..4 = extra
 * @param addr     cílová adresa
 */
void dbg_disasm_show_in_slot(int slot_idx, uint16_t addr);

/**
 * @brief Nastaví per-instance flag follow_pc.
 *
 * Pokud true, instance auto-jumps na PC při běhu emulace (= auto-follow).
 * Platí pro všechny instance (hlavní i sekundární) - každá má vlastní
 * follow_pc flag. Default: ON pro "main", OFF pro sekundární okna.
 *
 * @param self    instance (nesmí být NULL)
 * @param enable  nová hodnota flagu
 */
void dbg_disasm_view_set_follow_pc(DisassembledView *self, bool enable);

/**
 * @brief Vrátí aktuální hodnotu per-instance flagu follow_pc.
 *
 * @param self instance (nesmí být NULL)
 * @return     true pokud auto-follow PC povoleno
 */
bool dbg_disasm_view_get_follow_pc(const DisassembledView *self);

/**
 * @brief Nastaví per-instance flag show_tstates.
 *
 * Pokud true, render přidá 5. sloupec T-states do dolní disasm tabulky.
 * Default OFF; uživatel přepíná z hlavičky disasm view.
 *
 * @param self    instance (nesmí být NULL)
 * @param enable  nová hodnota flagu
 */
void dbg_disasm_view_set_show_tstates(DisassembledView *self, bool enable);

/**
 * @brief Vrátí aktuální hodnotu per-instance flagu show_tstates.
 *
 * @param self instance (nesmí být NULL)
 * @return     true pokud T-states sloupec povolen
 */
bool dbg_disasm_view_get_show_tstates(const DisassembledView *self);

/* ---------------------------------------------------------------------------
 * Backward-compat C-API - tenké obaly nad singleton hlavní instancí.
 *
 * Hlavní okno debuggeru (debugger_window.cpp) volá tyto funkce přímo,
 * bez explicitní instance. Singleton "main" se vytvoří lazy při první
 * kompletní cestě dbg_disassembled_render().
 * ------------------------------------------------------------------------- */

/**
 * @brief Vykreslí hlavní singleton instanci sekce Disassembled.
 *
 * Lazy-inicializuje singleton při prvním volání. Volá se z debugger_window.cpp
 * uvnitř středové části okna.
 *
 * @param section_height dostupná výška sekce v pixelech
 */
void dbg_disassembled_render(float section_height);

/**
 * @brief Registrace cfgmain klíčů pro persist hlavní disasm instance.
 *
 * Zaregistruje klíče @c disasm_main_follow_pc a @c disasm_main_show_tstates
 * v zadaném cfgmain modulu (obvykle DEBUGGER) s výchozími hodnotami:
 *   - follow_pc = 1 (hlavní okno follow PC - UI checkbox bude disabled)
 *   - show_tstates = 0
 *
 * Při loadu cfgmain z .ini se hodnoty uloží do interního static
 * úložiště modulu. Při lazy-create singleton hlavní instance v
 * @c dbg_disassembled_render se pak aplikují přes setters DisassembledView.
 * V každém framu se naopak aktuální hodnoty čtou z instance zpět do
 * static úložiště - shutdown save zapíše aktuální stav.
 *
 * Volat z @c debugger_init() po vytvoření CFGMOD modulu DEBUGGER.
 *
 * @param cmod CFGMODULE pointer (= "DEBUGGER" modul z debugger_init)
 */
void dbg_disassembled_register_cfg(void *cmod);

/**
 * @brief Per-frame inicializace sdíleného UI stavu debuggeru pro disasm sekce.
 *
 * Zajistí, že před prvním renderem libovolné disasm view (hlavní okno
 * nebo sekundární #2-5) v aktuálním ImGui framu je provedeno:
 *   - jednorázová inicializace `g_dbg_ui` (debugger_ui_state_init),
 *   - jeden běh refresh kontroleru (dbg_refresh_tick), který nastaví
 *     `g_dbg_ui.refresh.should_refresh` pro celý frame.
 *
 * Funkce je idempotentní v rámci jednoho framu - opakované volání
 * v témže framu (detekce přes ImGui::GetFrameCount()) je no-op. To
 * umožňuje volat ji bezpodmínečně z více míst (debugger_window i
 * dbg_extra_disasm_render_all) bez duplicitního refreshe.
 *
 * Bez této funkce by sekundární disasm okno otevřené bez hlavního
 * okna debuggeru nikdy neviděl `should_refresh == true` (refresh tick
 * se historicky volal jen z imgui_debugger_window) a jeho cache by
 * zůstala nulová - tabulka by zobrazovala samé `0x00`.
 *
 * @pre Volat pouze z UI vlákna mezi začátkem a koncem ImGui framu.
 */
void dbg_disassembled_frame_init(void);

/**
 * @brief Test viditelnosti adresy na hlavní singleton instanci.
 *
 * Tenký obal nad dbg_disasm_view_is_addr_visible() pro hlavní instanci.
 * Pokud singleton ještě neexistuje (před prvním renderem), vrátí false.
 *
 * @param addr cílová adresa
 * @return     true pokud addr leží uvnitř některého rendrovaného řádku
 */
bool dbg_dasm_is_addr_visible(uint16_t addr);


/**
 * Lightweight reprezentace jedné disassemblované instrukce. Používá se
 * pro Code Preview v Edit Breakpoint panelu a obecně tam, kde caller
 * potřebuje on-demand disasm bez sdílené cache rendrované disasm tabulky.
 */
typedef struct {
    uint16_t addr;          /**< Adresa instrukce */
    int length;             /**< Délka v bajtech (1-4) */
    uint8_t bytes[4];       /**< Raw bajty (jen prvních length validních) */
    char mnemonic[64];      /**< Zformátovaná mnemonika (lowercase) */
} DbgDasmLine;

/**
 * Disassembluje jednu instrukci na adrese a zapíše do out. Čte paměť
 * přes debugger_dasm_read_cb (= bezpečná read cesta bez vedlejších
 * efektů). Vždy vrací true (chybové stavy nejsou - přečte cokoliv,
 * v nejhorším z80ex_dasm vrátí "??" pro neznámý opcode).
 *
 * Stateless - nepoužívá žádný stav instance DisassembledView.
 */
bool dbg_dasm_get_line(uint16_t addr, DbgDasmLine *out);

#ifdef __cplusplus
}
#endif

#endif /* DBG_DISASSEMBLED_H */
