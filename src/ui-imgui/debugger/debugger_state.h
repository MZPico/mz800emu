/*
 * debugger_state.h — Sdílený stav UI debuggeru
 *
 * Tato struktura drží veškerý UI stav debuggeru, který potřebují
 * jednotlivé sekce (Disassembled, Iconbar, Menu, ...). Je to čistě UI stav,
 * emulátorový stav je v g_debugger (src/emulator/debugger/debugger.h).
 *
 * Životní cyklus:
 * - Inicializace při prvním otevření okna debuggeru
 * - Stav přetrvává i po zavření okna (static)
 * - Reset při volání debugger_ui_state_reset()
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

#ifndef DEBUGGER_STATE_H
#define DEBUGGER_STATE_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Maximální počet disassemblovaných řádků v dolní tabulce.
 * Používá se pro statickou alokaci polí (cache, buffer).
 * Skutečný počet zobrazených řádků je g_dbg_ui.visible_rows,
 * dynamicky počítaný podle výšky okna a velikosti fontu.
 */
#define DBG_DASM_MAX_VISIBLE_ROWS 128

/*
 * Zmenšení fontu v celé středové části debuggeru oproti základní velikosti.
 * Všechny sekce (Disassembled, budoucí Registry, Stack, ...) používají
 * menší font pro úsporu místa. Nastavuje se na jednom místě.
 */
#define DBG_CONTENT_FONT_SIZE_OFFSET (-2.0f)

/*
 * Interval refreshe dat debuggeru v milisekundách.
 * Sekce nekontrolují data každý frame (60×/s), ale jen když kontroler
 * rozhodne, že uplynul dostatečný čas nebo někdo vyžádal okamžitý refresh.
 */
#define DBG_REFRESH_INTERVAL_MS 100.0

/*
 * DbgRefreshController — centralizované řízení refreshe dat debuggeru.
 *
 * Místo aby každá sekce (Disassembled, Registry, Stack, ...) kontrolovala
 * data z emulátoru každý frame, kontroler rozhodne KDY se mají data číst.
 * Sekce se řídí výsledkem (should_refresh). Vlastní cache sekcí (memcmp)
 * zůstává — kontroler jen říká "teď je čas zkontrolovat".
 *
 * Pravidla refreshe:
 * 1. Časový interval — data starší než DBG_REFRESH_INTERVAL_MS se obnoví
 * 2. Explicitní požadavek sekce — navigace, slider, context menu
 * 3. Emulační událost — step/breakpoint/pauza → okamžitý refresh
 */
typedef struct DbgRefreshController
{
    double last_refresh_time;   /* Čas posledního refreshe (ImGui::GetTime()) */
    bool force_refresh;         /* One-shot: někdo požádal o okamžitý refresh */
    bool should_refresh;        /* Výsledek pro tento frame — sekce jen čtou */
} DbgRefreshController;

/*
 * Počet řádků v horní tabulce historie.
 * Historie je ring buffer definovaný v debugger.h (DEBUGGER_HISTORY_LENGTH = 32).
 * Zde definujeme kolik řádků maximálně zobrazíme v UI.
 */
#define DBG_HIST_VISIBLE_ROWS 32

/*
 * DebuggerUIState — sdílený stav UI debuggeru
 *
 * Obsahuje vše, co potřebují jednotlivé sekce pro vykreslování
 * a interakci. Nepřekrývá se s g_debugger — to je stav jádra.
 */
typedef struct DebuggerUIState
{
    /* Příznak, zda byl stav inicializován (nastaví se při prvním otevření) */
    bool initialized;

    /*
     * Adresa focusu v dolní tabulce sekce Disassembled.
     * Focus = adresa instrukce na aktivním (selectovaném) řádku.
     * Šoupátko (slider) přímo odpovídá této hodnotě.
     * Při refreshi se nastaví na aktuální regPC.
     */
    uint16_t focus_addr;

    /*
     * Index selectovaného řádku v dolní tabulce (0-based).
     * -1 = žádný řádek není selectovaný (nemělo by nastat).
     */
    int selected_row;

    /*
     * Adresa instrukce na selectovaném řádku (= rows[selected_row].addr).
     * Aktualizuje se při každém renderu disassembled sekce. Používá se
     * např. pro Run To Cursor (F4) - cílová adresa pro temporary BP.
     * Bez tohoto cache by F4 musel duplikovat disassembly logiku
     * (focus_addr + selected_row × variabilní délka instrukce).
     */
    uint16_t selected_addr;

    /*
     * Aktuální počet viditelných řádků v dolní tabulce.
     * Dynamicky se počítá podle výšky tabulky a velikosti fontu.
     * Omezeno na rozsah 1..DBG_DASM_MAX_VISIBLE_ROWS.
     */
    int visible_rows;

    /*
     * Poměr výšky horní tabulky (historie) vůči celku.
     * Rozsah 0.0–1.0, výchozí 0.35 (35% pro historii, 65% pro disassembly).
     * Uživatel mění posuvníkem (splitter) mezi tabulkami.
     */
    float history_split_ratio;

    /*
     * Příznak, že pauza byla vyvolána kliknutím na interaktivní prvek
     * v debuggeru (ne externě). V takovém případě se zobrazí modální
     * informační okno "Emulation was paused...".
     */
    bool paused_by_debugger_click;

    /*
     * Příznak, že modální info okno "Emulation was paused..." je právě zobrazeno.
     * Nastaví se na true po automatické pauze z animačního režimu,
     * resetuje se po kliknutí na OK.
     */
    bool show_pause_info_modal;

    /*
     * One-shot flag: okno bylo právě otevřeno přes Alt+D z global_shortcuts.
     * Debugger window ho spotřebuje a přeskočí svůj Alt+D handler na tomto framu,
     * aby nedošlo k okamžitému zavření (oba handlery reagují na stejný stisk).
     * Nastavuje global_shortcuts.cpp, spotřebovává debugger_window.cpp.
     */
    bool opened_via_alt_d;

    /*
     * Centralizované řízení refreshe dat — viz DbgRefreshController.
     * Sekce kontrolují should_refresh místo aby četly data každý frame.
     */
    DbgRefreshController refresh;

} DebuggerUIState;

/* Globální instance stavu UI debuggeru */
extern DebuggerUIState g_dbg_ui;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Inicializace stavu UI — volá se při prvním otevření okna */
    void debugger_ui_state_init(void);

    /* Reset stavu UI na výchozí hodnoty */
    void debugger_ui_state_reset(void);

    /*
     * Refresh stavu po přechodu do editačního režimu (pauza).
     * Nastaví focus_addr na aktuální regPC, selected_row na 0.
     */
    void debugger_ui_state_refresh_from_cpu(void);

    /*
     * dbg_refresh_init — inicializace refresh kontroleru.
     * Volá se z debugger_ui_state_init(). Nastaví force_refresh = true
     * (první otevření okna = vždy refresh).
     */
    void dbg_refresh_init(void);

    /*
     * dbg_refresh_request — vyžádání okamžitého refreshe dat.
     * Volají sekce při změně focus_addr, context menu akcích, apod.
     * Nastaví force_refresh = true, zpracuje se v příštím dbg_refresh_tick().
     */
    void dbg_refresh_request(void);

    /*
     * dbg_refresh_tick — vyhodnocení pravidel refreshe pro aktuální frame.
     * Volá se 1× na začátku framu z debugger_window.cpp (před renderingem sekcí).
     * Nastaví should_refresh podle: force_refresh, časový interval, emulační událost.
     * Sekce pak jen čtou g_dbg_ui.refresh.should_refresh.
     */
    void dbg_refresh_tick(void);

    /*
     * dbg_autopause_on_interaction — obslužná rutina automatické pauzy.
     *
     * Volají ji handlery interaktivních prvků debuggeru (Selectable řádky
     * disassembly tabulky, budoucí editory registrů atd.), když uživatel
     * klikne na interaktivní prvek v animačním režimu (emulace běží).
     *
     * Chování:
     * - Pokud emulace běží: pozastaví ji, nastaví flag pro zobrazení
     *   modálního info okna "Emulation was paused..." a vrátí true.
     *   Volající by měl v takovém případě přeskočit normální zpracování
     *   kliku (selekci řádku, editaci apod.), protože uživatel potřebuje
     *   nejdřív potvrdit pauzu přes modal.
     * - Pokud je emulace již v pauze: neudělá nic a vrátí false.
     *   Volající pokračuje normálním zpracováním kliku.
     *
     * Tuto funkci NEVOLAJÍ:
     * - Neinteraktivní prvky (horní tabulka historie — read-only)
     * - Tlačítka iconbaru (mají vlastní handlery pro pauzu/krokování)
     * - Menu položky (mají vlastní akce)
     */
    bool dbg_autopause_on_interaction(void);

    /*
     * dbg_autopause_silent — tichá auto-pauza bez modálního info okna.
     *
     * Varianta dbg_autopause_on_interaction() pro situace, kdy uživatel
     * interakcí explicitně očekává pauzu a chce s prvkem rovnou pracovat
     * (např. in-place edit registru v CPU panelu). Modální okno
     * "Emulation was paused..." by zde rozbilo edit flow:
     * klik na hodnotu registru → otevře InputText → modal vyskočí přes
     * něj → klik OK na modal odebere focus → InputText se zavře dřív,
     * než uživatel stihne psát.
     *
     * Chování:
     * - Pokud emulace běží: pozastaví ji, nastaví paused_by_debugger_click
     *   (sémantika "debugger pauznul"), ale NEnastaví show_pause_info_modal.
     *   Vrací true.
     * - Pokud je emulace již v pauze: neudělá nic, vrací false.
     *
     * Tuto funkci by měly volat jen ty interakce, kde modal je rušivý
     * (= edit registru). Pro ostatní (Selectable v disassembly atd.) se
     * dál používá dbg_autopause_on_interaction() s modálem jako zpětnou
     * vazbou.
     */
    bool dbg_autopause_silent(void);

#ifdef __cplusplus
}
#endif

#endif /* DEBUGGER_STATE_H */
