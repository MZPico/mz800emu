/*
 * dbg_stack_panel.h - Stack monitor panel pro debugger.
 *
 * V0: surový hex dump paměti v okolí aktuálního SP. Word-oriented
 * tabulka (32 řádků nad SP + 8 pod SP, default) s marker sloupcem
 * `>` na řádku odpovídajícím aktuální hodnotě SP. Lichý SP fallback
 * na byte-oriented zobrazení (= jeden řádek = 1 bajt).
 *
 * V1: přidán koncept stack regionů + low-water mark. Hlavička obsahuje
 * dropdown pro výběr aktivního regionu (= "current region focus"),
 * Depth se počítá proti vybranému regionu, marker `=` v hex dump
 * tabulce zobrazuje pozici watermark, side panel vpravo ukazuje
 * tabulku regionů (Name / Base / Limit / SP_pct / Min_SP / Actions),
 * tlačítko "+ Add region from current SP" otevírá modal dialog.
 *
 * Layout (V1, Variant B - samostatné plovoucí okno):
 *
 *   +--- Stack Monitor ----------------------------------------+
 *   | SP: 10D2h  Depth: 30 B  Region: [system v] [Reset W]     |
 *   | [Set BP from SP-256]  [+ Add region from current SP]     |
 *   +-------------------+-------------------------------------+
 *   | Addr  Byte  Word  | Regions                              |
 *   | 10F0h ..    ..    | Name    Base   Limit  SP%  Min_SP    |
 *   | 10EEh CD 4A CD4Ah | system  10F0h  1000h  18%  10C4h     |
 *   | ...               | game    E000h  D000h  --   E000h     |
 *   | 10D6h 80 40 4080h |                                       |
 *   | 10D4h 3E 12 123Eh |                                       |
 *   | 10D2h AB CD ABCDh > [Reset W] [Del]                       |
 *   | 10D0h .. ..       =                                       |
 *   +-------------------+-------------------------------------+
 *
 * V1 ne-implementuje: SP history sparkline, return-addr decoder
 * proti callstack, memory clobber detect, CDL "S" klasifikace,
 * persistence regionů do BP ini. To je V2+.
 *
 * Refresh: pres DbgRefreshController (g_dbg_ui.refresh.should_refresh)
 * - interval 100 ms + force pri pauze/step. Panel posila DBGAPI_CMD_
 * STACK_DUMP s adresou (= SP + lines_above * 2) a delkou (= 40 * 2 B).
 *
 * Stable ID strategie: tabulka ma staticky ID `###stack_table`, okno
 * (= caller wrapper) ma `###stack_window` - tri hashe pro pripadny
 * dynamicky title bez ztraty pozice/velikosti v ini.
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

#ifndef DBG_STACK_PANEL_H
#define DBG_STACK_PANEL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration ImVec2 ekvivalentu pro C linkage (helper pouze).
 * Public render helpery pouzivaji float w/h misto ImVec2 (= jednoduchejsi
 * C/C++ rozhrani). */

/**
 * @brief Vykresli Stack panel do aktualniho ImGui kontejneru.
 *
 * Predpoklada se, ze caller (= stack_window.cpp wrapper) jiz vytvoril
 * top-level okno pres ImGui::Begin a panel obsadi celou dostupnou
 * plochu (= header + tabulka).
 *
 * Data ctve pres dbgapi (CMD_STACK_DUMP) v okamzicich kdy
 * g_dbg_ui.refresh.should_refresh == true. Mezi refreshi pouziva
 * cache (panel_state.buf, panel_state.sp_now, ...).
 *
 * V1 read-only pro pamet, ale registr regionu lze pridavat / mazat
 * z UI (race-safe pres dbgapi CMD_STACK_REGIONS_*). Klik handlery
 * na radky hex dumpu = V2 (Focus DA / disassembly jump).
 *
 * Tlacitko "Set BP from SP-256" pridava nove SP_THRESHOLD BPT pres
 * dbg_ui_bp_add() (race-safe vs EMU vlakno). Tlacitko "+ Add region
 * from current SP" otevre modal dialog s prednapljnnymi hodnotami
 * (base = current SP, limit = base - 256).
 *
 * @pre Volat pouze z UI vlakna mezi NewFrame a EndFrame ImGui,
 *      uvnitr ImGui::Begin() okna.
 */
void dbg_stack_panel_render(void);


/**
 * @brief V6: registruje cfgmain sekci [STACK_PANEL] pro persistenci UI
 *        preferencí.
 *
 * Sekce obsahuje:
 *   lock_sp_center      = bool 0/1   (V2 deprecated, V10 migrate only)
 *   sp_lines_above      = unsigned   (V10 pozice SP řádku, 0..K_LINES_TOTAL-1)
 *   show_regions_window = bool 0/1   (V8)
 *   show_history_window = bool 0/1   (V9)
 *
 * V10: klíč lock_sp_center je deprecated. Při načtení staré INI s
 * lock_sp_center=1 se sp_lines_above migrate na K_LINES_TOTAL/2. Save
 * vždy zapisuje lock_sp_center=0 (= novou hodnotu drží sp_lines_above).
 *
 * Volá se z debugger_init() po cfgroot_register_new_module návratu pro
 * "DEBUGGER" modul. Idempotentní.
 *
 * Side effects: po načtení aplikuje hodnoty do panel state struktury
 * g_stack. history_enabled se sync z emu při prvním refresh, ne přes
 * tuto sekci (vidi separátní [STACK_HISTORY] sekci).
 *
 * @pre g_cfgmain inicializován.
 */
void dbg_stack_panel_cfg_init(void);


/* ========================================================================= */
/*  V8 - Public API pro Stack Regions samostatne okno                        */
/* ========================================================================= */

/**
 * @brief V8: per-frame refresh dat o regionech + SP history pro Stack
 *        Regions okno (= bez hex dump).
 *
 * Volat na zacatku stack_regions_window_render kazdy frame. Pokud je
 * refresh tick aktivni, posle dbgapi CMD_STACK_REGIONS_LIST + (volitelne)
 * STACK_HISTORY_GET. Self-rate-limit pres frontu. Idempotentni v ramci
 * frame - pokud uz dbg_stack_panel_render() zavolal refresh v temz framu,
 * druhe volani je no-op (refresh tick uz neni 'should').
 *
 * Tato funkce muze byt volana bez dbg_stack_panel_render - umoznuje
 * Stack Regions oknu funkcnost i kdyz hlavni Stack Monitor okno je
 * zavrene.
 */
void dbg_stack_panel_frame_refresh(void);


/**
 * @brief V8: pocet regionu v cache (= UI snapshot).
 *
 * Vraci 0 pokud cache neni jeste platna (= prvni refresh neproběhl).
 *
 * @return Pocet validnich regionu (0..DBGAPI_STACK_REGIONS_MAX).
 */
int dbg_stack_panel_regions_count(void);


/**
 * @brief V8: vystavi pointer na region info v cache pro radek tabulky.
 *
 * Pointer je platny do dalsiho refresh ticku. Pro per-frame data je
 * to dostatecne (caller cte hodnoty hned v iteraci tabulky).
 *
 * @param idx  Index 0..count-1.
 * @return Pointer na st_DBGAPI_STACK_REGION_INFO (cast pres void* protoze
 *         dbgapi typ neni v UI header), nebo NULL pri idx mimo rozsah.
 */
const void *dbg_stack_panel_get_region(int idx);


/**
 * @brief V8: aktualni SP precteny pri poslednim refreshi regionu.
 *
 * Pouziva se v Stack Regions okne pro vypocet SP%. Stejny zdroj
 * jako sticky header hlavniho okna (= regions.sp_now z LIST snapshotu).
 *
 * @return SP (0..0xFFFF).
 */
uint16_t dbg_stack_panel_sp_now(void);


/**
 * @brief V8: index vybraneho regionu (= dropdown v hlavnim Stack Monitor okne).
 *
 * Stack Regions okno tento index pouziva pro zvyrazneni "aktivniho"
 * radku v tabulce. -1 = zadny vybran.
 */
int dbg_stack_panel_get_selected(void);


/**
 * @brief V8: nastavi vybrany region (= sync s dropdownem hlavniho okna).
 *
 * @param idx  Novy selected index, nebo -1 pro "(none)".
 */
void dbg_stack_panel_set_selected(int idx);


/**
 * @brief V8: vyzaduje otevreni Add region modalu z externiho okna.
 *
 * Predvyplnu hodnoty stejne jako interni tlacitko v hlavnim Stack Monitor
 * okne (base = current SP, limit = base - 256). Modal se otevre v dalsim
 * frame (= OpenPopup je delegovany na render funkci).
 *
 * Modal je renderovan v dbg_stack_panel_render_modals() - caller musi tuto
 * funkci volat kazdy frame, aby modal po OpenPopup obdrzel sve BeginPopupModal
 * volani. Stack Regions okno toto plni za hlavni Stack Monitor okno, kdyz
 * je zavrene.
 */
void dbg_stack_panel_request_add_modal(void);


/**
 * @brief V8: vyzaduje otevreni Edit region modalu pro dany region.
 *
 * Predvyplni hodnoty z aktualni cache region[idx]. Stejny effekt jako
 * klik na [E] tlacitko per-region card v hlavnim okne (V7).
 *
 * @param idx  Index regionu (0..count-1). Pri mimo rozsah no-op.
 */
void dbg_stack_panel_request_edit_modal(int idx);


/**
 * @brief V8: vyzaduje reset watermark + counters pro dany region.
 *
 * Stejny effekt jako klik na [R] tlacitko v hlavnim okne.
 *
 * @param idx  Index regionu (0..count-1).
 */
void dbg_stack_panel_request_reset_region(int idx);


/**
 * @brief V8: vyzaduje smazani regionu.
 *
 * Stejny effekt jako klik na [X] tlacitko v hlavnim okne.
 *
 * @param idx  Index regionu (0..count-1).
 */
void dbg_stack_panel_request_remove_region(int idx);


/**
 * @brief V8: vykresli mini-sparkline trendu SP samples filtrovanych na
 *        rozsah regionu.
 *
 * Sdileny render helper s V2 logikou (= filter z history_samples na
 * range <limit..base>, ImGui::PlotLines). Pri vypnutem recordingu /
 * prazdne historii vykresli TextDisabled("--") namisto plotu.
 *
 * Volat uvnitr aktualni table cell - obsazi celou pozadovanou velikost.
 *
 * @param region_idx  Index regionu v cache (0..count-1).
 * @param width       Sirka plotu v px.
 * @param height      Vyska plotu v px.
 */
void dbg_stack_panel_render_region_trend(int region_idx,
                                          float width, float height);


/* ========================================================================= */
/*  V9 - Public API pro Stack History samostatne okno                        */
/* ========================================================================= */

/**
 * @brief V9: vykresli jadro SP history sparkline plotu do zadane plochy.
 *
 * Sdileny render helper bez CollapsingHeader, bez info textu a bez
 * Selected info bloku - jen vlastni plot rectangle:
 *   - pozadi + ramecek
 *   - polyline pres history_samples
 *   - vertikalni event markery (push/pop/other) nad polyline
 *   - hover crosshair + tooltip s idx/SP/cycle/delta
 *   - klik LMB = toggle vyberu vzorku
 *   - persistentni zluty crosshair pro vybrany vzorek
 *
 * Pri history_enabled == false, prazdne cache nebo width/height < min vykresli
 * placeholder text. Helper sam alokuje invisible button + drawlist primitives.
 *
 * Volat uvnitr ImGui kontextu, pres ImGui::GetCursorScreenPos() ziska top-left.
 * Caller je odpovedny za rezervaci dostatecne plochy nad / pod helperem.
 *
 * @param width   Sirka plot area v px (orezana na minimum 80.0f).
 * @param height  Vyska plot area v px (= marker strip + gap + polyline).
 *                Orezana na minimum 30.0f (= 8 marker + 2 gap + 20 plot).
 *
 * Side effects: zapise do g_stack.selected_history_idx pri kliku.
 *               Data ctena z g_stack.history_samples + history_count.
 *
 * @pre Volat pouze z UI vlakna mezi NewFrame a EndFrame ImGui.
 */
void dbg_stack_panel_render_history_plot(float width, float height);


/**
 * @brief V9: vykresli info radek pod SP history sparkline plotem.
 *
 * Obsahuje (vsech radky):
 *   - "Samples: N  first SP=XXh @ N  last SP=XXh @ N"
 *   - "Slope: %.6f SP/cycle (creep!)" pripadne bez (creep!)
 *   - Selected info pokud g_stack.selected_history_idx >= 0:
 *     "Selected: idx=N SP=XXh cycle=N  [Clear]"
 *   - Show events checkbox
 *
 * Pri history_enabled == false nebo prazdne cache vykresli prislusny
 * placeholder text (= hint). Idempotentni - jen Text/Checkbox volani.
 *
 * @param clear_id_suffix  Suffix pro stable ImGui ID Clear tlacitka
 *                         (= povinny non-NULL retezec, napr. "main",
 *                          "win"). Zajisti unique ID pri renderu z vice
 *                         oken.
 * @param show_open_history_btn  V9.1: pokud true, vpravo zarovnane
 *                         [Stack History] tlacitko otevre + focusne
 *                         samostatne Stack History okno. Hlavni Stack
 *                         Monitor okno predava true, Stack History okno
 *                         (= jsme jiz uvnitr) predava false.
 *
 * Side effects: pres Show events checkbox zapisuje do g_stack.show_events.
 *               Clear button maze g_stack.selected_history_idx na -1.
 *               [Stack History] btn (show_open_history_btn) modifikuje
 *               g_gui->showStackHistoryWindow + ImGui focus.
 *
 * @pre clear_id_suffix != NULL. Volat z UI vlakna.
 */
void dbg_stack_panel_render_history_info(const char *clear_id_suffix,
                                          bool show_open_history_btn);


/**
 * @brief V9: vrati true, pokud je SP history recording zapnut (= shadow z UI).
 *
 * Stack History okno pres toto zjisti, zda ma vubec vykreslit plot nebo
 * hint "Enable SP history in Stack Monitor". Pristup pres helper (a ne
 * pres globalni promennou) izoluje implementaci od interniho stavu panelu.
 */
bool dbg_stack_panel_history_enabled(void);


/**
 * @brief V9.1: zapne/vypne SP history recording (= sdileny helper).
 *
 * Externi okna (Stack History) volaji misto duplicity dbgapi CMDRQ kodu.
 * Caller nejprve nastavi shadow g_stack.history_enabled na cilovou
 * hodnotu, pak vola tento helper - pri full fronte se shadow rollbackne.
 *
 * @param enable  true = zapnout recording, false = vypnout + reset bufferu.
 *
 * @pre Volat z UI vlakna mezi NewFrame a EndFrame.
 */
void dbg_stack_panel_set_history_enabled(bool enable);


/**
 * @brief V8: vykresli modaly Add/Edit, pokud byly otevreny.
 *
 * Volat kazdy frame z kazdeho okna ktere muze pozadat o otevreni
 * modalu (= dbg_stack_panel_render i stack_regions_window_render).
 * Modal interne kontroluje stav g_stack.add_modal_open /
 * g_stack.edit_modal_open a BeginPopupModal je no-op pokud popup neni
 * aktivni. Idempotentni v ramci framu - prvni volani modaly vykresli,
 * druhe je no-op (BeginPopupModal vrati false, pokud uz ho prvni volani
 * zavrelo).
 */
void dbg_stack_panel_render_modals(void);


#ifdef __cplusplus
}
#endif

#endif /* DBG_STACK_PANEL_H */
