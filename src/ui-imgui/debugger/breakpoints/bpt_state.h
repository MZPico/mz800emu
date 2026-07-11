/*
 * bpt_state.h — Stav UI okna Breakpoints
 *
 * Sdílený stav pro všechny moduly breakpoints UI (okno, tree, editační dialogy).
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

#ifndef BPT_STATE_H
#define BPT_STATE_H

#include <stdint.h>
#include <stdbool.h>

/* breakpoints.h pulluje <glib.h> uvnitř svého extern "C" bloku (= legacy
 * vzor v jádře emulátoru). Aby bylo včas C++ definováno glib bez C linkage
 * pro template-y v gwin32.h, includujeme glib zde jako první (v C++ kontextu
 * mimo extern "C"). Header guard zajistí, že další include uvnitř extern "C"
 * bude no-op. Stejný vzor používá bpt_edit_panel.cpp. */
#ifdef __cplusplus
#include <glib.h>
#endif

/* Public typy: en_BPT_TYPE, en_BP_ZONE, en_BP_EVENT. Hlavičky obě obsahují
 * vlastní extern "C" guard. */
#include "emulator/debugger/breakpoints.h"
#include "emulator/debugger/bp_event.h"


/* Velikosti edit bufferů pro multiline texty (= condition / action mini-DSL).
 * 1024 znaků je dostatečné i pro nejdelší rozumnou condition (100 byte)
 * i pro 8-10 řádků action skriptu. */
#define BPT_EDIT_EXPR_BUFLEN     1024
#define BPT_EDIT_ACTION_BUFLEN   2048
#define BPT_EDIT_ERR_BUFLEN      256
#define BPT_EDIT_EVENT_NAME_LEN  64


/* Typ položky ve stromu */
typedef enum {
    BPT_ITEM_GROUP,
    BPT_ITEM_EVENT
} BptItemType;


/*
 * Mód action editoru (V1.5 UX).
 *
 * UI-only stav - není perzistován. Při Apply se mode přeloží na concrete
 * action string v bpt->action:
 *   STOP      -> action = NULL (klasický BP, zastaví emu)
 *   LOG_ONLY  -> action = `log "BP <name>"; continue` (auto-generovaný)
 *   CUSTOM    -> action = textbox content (mini-DSL)
 *
 * Při open: detekce mode podle action content (= NULL → STOP, single-line
 * `log "..."; continue` → LOG_ONLY, jinak CUSTOM).
 */
typedef enum {
    BPT_ACTION_MODE_STOP = 0,
    BPT_ACTION_MODE_LOG_ONLY,
    BPT_ACTION_MODE_CUSTOM
} BptActionMode;


/* Jedna položka ve stromu — společný typ pro skupiny i eventy */
typedef struct BptTreeItem {
    BptItemType type;
    int id;             /* ID skupiny nebo eventu */
    int parent;         /* ID rodičovské skupiny, nebo -1 */
} BptTreeItem;


/*
 * Stav editačního panelu (nemodální dockable okno).
 *
 * Single-instance model: další klik na BP/grupu přepíše obsah panelu.
 * Pokud uživatel má rozpracované změny (dirty == true), zobrazí se
 * confirm dialog "Discard changes?" před přepnutím.
 *
 * Pole working copy (str/bool/float) drží UI stav během editace.
 * Apply propíše working copy do g_breakpoints přes 1 sync dbgapi cmd
 * (CMD_BP_UPDATE / _CREATE_WITH_INIT pro BPT_ITEM_EVENT, CMD_BPGRP_UPDATE
 * / _ADD pro BPT_ITEM_GROUP). Cancel zahodí working copy bez propagace.
 */
typedef struct BptEditPanelState {
    bool visible;                   /**< Panel zobrazen */
    BptItemType type;               /**< BPT_ITEM_GROUP nebo BPT_ITEM_EVENT */
    int edit_id;                    /**< ID editované položky, -1 = "new" */
    bool dirty;                     /**< Working copy se liší od g_breakpoints */
    bool initialized_for_id;        /**< Working copy je naplněna pro aktuální edit_id (= zabraňuje re-init při každém frame) */
    int initialized_for_id_value;   /**< ID pro které je inicializováno */

    /* Working copy - společné pro Group i Event */
    bool wc_enabled;
    bool wc_auto_name;              /**< Jen pro EVENT */
    char wc_name[128];
    char wc_addr[7];                /**< Jen pro EVENT - max 6 hex + \0 */
    int wc_parent_group_id;         /**< Jen pro EVENT - cíl grupy (-1 = root) */
    float wc_fg_color[3];
    float wc_bg_color[3];

    /* === Smart BP V1 working copy fields (D.6) === */
    en_BPT_TYPE wc_type;            /**< Typ BP (= dropdown 9 hodnot) */
    char wc_addr_end[7];            /**< Range konec - hex string (jen MEM_R/MEM_W). Prázdné = single point (= addr_end == addr) */
    en_BP_ZONE wc_zone;             /**< Memory zone (= jen pro mem typy) */
    char wc_bank_id[4];             /**< Bank index pro MMEXT_BANK zone, hex string */
    char wc_port[5];                /**< I/O port pro IORQ - hex string max 4 znaky */
    en_BP_EVENT wc_event;           /**< HW event typ (jen HW_EVENT) */
    char wc_event_param[8];         /**< Parametr event (raster:N), dec string */
    en_BP_EVENT_TRIGGER wc_event_trigger; /**< V1.5 HWE: trigger condition pro signal eventy (LOW/HIGH/RISING/FALLING/CHANGED) */
    char wc_sp_threshold[7];        /**< SP threshold hex (jen SP_THRESHOLD) */
    char wc_expr[BPT_EDIT_EXPR_BUFLEN];      /**< Condition expression text */
    char wc_action[BPT_EDIT_ACTION_BUFLEN];  /**< Action mini-DSL skript */
    char wc_expr_err[BPT_EDIT_ERR_BUFLEN];   /**< Poslední parse chyba pro expr (prázdné = OK) */
    char wc_action_err[BPT_EDIT_ERR_BUFLEN]; /**< Poslední parse chyba pro action */
    bool wc_expr_validated;         /**< Po Apply/Test prošlo expr parse (= aktualizovat err panel) */
    bool wc_action_validated;       /**< Po Apply/Test prošlo action parse */
    uint32_t wc_hit_count;          /**< Trigger po N. hitu (0 = každý) */
    uint32_t wc_skip_count;         /**< Skip prvních N hitů */
    bool wc_edge_triggered;         /**< Edge-triggered flag */

    /* === V1.5 UX: Action mode (radio Stop/Log only/Custom) === */
    BptActionMode wc_action_mode;   /**< Mód action editoru (UI-only) */

    /* === V1.5.E: Match modes (RANGE / MASK pro adresové fieldy) === */
    en_BP_MATCH_MODE wc_addr_match_mode; /**< PC_EXEC/MEM_R/MEM_W match mode */
    char wc_addr_mask[7];                /**< AND mask pro addr MASK mode (hex) */
    en_BP_MATCH_MODE wc_port_match_mode; /**< IORQ_R/IORQ_W match mode */
    char wc_port_end[5];                 /**< RANGE upper pro port (max 4 hex) */
    char wc_port_mask[5];                /**< AND mask pro port MASK mode */
    en_BP_PORT_MODE wc_port_mode;        /**< V1.5.A7: 8BIT vs 16BIT IORQ adresace */
    en_BP_MATCH_MODE wc_bank_match_mode; /**< MMEXT_BANK match mode */
    char wc_bank_id_end[4];              /**< RANGE upper pro bank (max 2 hex + nul) */
    char wc_bank_id_mask[4];             /**< AND mask pro bank MASK */
    en_BP_ADDR_SPACE wc_bp_addr_space;   /**< Feature D: CPU_VIEW / BANK_OFFSET (MMEXT_BANK) */
    en_BP_SP_MODE wc_sp_mode;            /**< SP_THRESHOLD mode (SINGLE/WINDOW) */
    char wc_sp_upper[7];                 /**< Upper bound pro SP WINDOW (hex) */

    /* === V1.5.A8: IRQ vector + ISR address filter (BPT_TYPE_IRQ) === */
    bool wc_im2_vector_enabled;          /**< Filter aktivní pro IM2 vector slot */
    char wc_im2_vector_addr[7];          /**< IM2 vector address hex (max 4 + nul) */
    bool wc_im2_isr_enabled;             /**< Filter aktivní pro IM2 ISR target */
    char wc_im2_isr_addr[7];             /**< IM2 ISR address hex (max 4 + nul) */

    /* === V1.6+ 4.4: IM2 vector/ISR match modes (SINGLE/RANGE/MASK) ===
     * Default SINGLE/0000/FFFF zachovává V1.5 SINGLE-only chování. */
    en_BP_MATCH_MODE wc_im2_vector_match_mode; /**< IM2 vector match mode */
    char wc_im2_vector_addr_end[7];      /**< RANGE upper bound hex (max 4 + nul) */
    char wc_im2_vector_mask[7];          /**< MASK bitmask hex (max 4 + nul) */
    en_BP_MATCH_MODE wc_im2_isr_match_mode;    /**< IM2 ISR match mode */
    char wc_im2_isr_addr_end[7];         /**< RANGE upper bound hex (max 4 + nul) */
    char wc_im2_isr_mask[7];             /**< MASK bitmask hex (max 4 + nul) */

    /* === V1.5.A8.5: IRQ IM mode discriminator + IM 0 RST mask === */
    bool wc_im0_enabled;                 /**< IRQ BP fire na IM 0 dispatch */
    bool wc_im1_enabled;                 /**< IRQ BP fire na IM 1 dispatch */
    bool wc_im2_enabled;                 /**< IRQ BP fire na IM 2 dispatch */
    uint8_t wc_im0_rst_mask;             /**< IM 0 RST opcode bitmask (0 = match-all) */

    /* === V1.5.A8.5: IRQ_SIG source mask (BPT_TYPE_IRQ_SIG) === */
    uint8_t wc_irq_sig_source_mask;      /**< Bitmask en_BP_IRQ_SIG_SOURCE */

    /* Live evaluator test - input + výsledek (D.6 volitelné) */
    char wc_test_expr[BPT_EDIT_EXPR_BUFLEN]; /**< Vstup pro Test Eval */
    char wc_test_result[384];                /**< Formátovaný výsledek (dec/hex) nebo chyba.
                                              *   384 = errbuf[256] + "ERR: " prefix + rezerva
                                              *   (GCC -Wformat-truncation= jinak hlasi truncation). */

    /* Pending confirm dialog při přepnutí dirty edit */
    bool show_discard_confirm;
    BptItemType pending_type;
    int pending_edit_id;

    /* Pending confirm dialog při zavírání panelu se ztrátou dirty */
    bool show_close_confirm;
} BptEditPanelState;


/* Stav UI okna breakpointů */
typedef struct BptUIState {
    bool initialized;

    /* Hex input v akční liště */
    char hex_input[7];  /* max 6 hex znaků + '\0' */

    /* Selekce v tree view */
    int selected_id;            /* ID vybrané položky, nebo -1 */
    BptItemType selected_type;  /* Typ vybrané položky */

    /* Cache verze — pro detekci změn v g_breakpoints */
    unsigned cached_version;

    /* Editační panel (nemodální dockable, single-instance).
     * Nahradil dřívější bpt_render_edit_*_dialog modální popup. */
    BptEditPanelState edit_panel;

    /* Flag pro Alt+B guard (stejný vzor jako opened_via_alt_d v debuggeru) */
    bool opened_via_alt_b;

    /* Požadavky na expand/collapse všech skupin (spotřebují se po vykreslení) */
    bool request_expand_all;
    bool request_collapse_all;

    /* Odložené otevření kontextového menu (řeší problém s ID stackem) */
    bool open_context_menu;

    /* Odložené otevření kontextového menu v per-typ tabech (plochá tabulka)
     * a v záložce Groups. Oddělené flagy = oddělené popupy s vlastním obsahem
     * (per-typ: bez Expand/Collapse/Add Group; Groups: jen group operace). */
    bool open_filtered_context;
    bool open_groups_context;

    /* === V1.7: Last triggered BP tracking ===
     * Při DBGAPI_MSG_BREAKPOINT_HIT (= emu pause kvůli BP) dispatcher
     * uloží id zasaženého BP. Slouží pro:
     *   - vizuální highlight řádku v BP list okně
     *   - auto-scroll na ten řádek (= scroll_to_last_triggered flag)
     *   - tooltip "last hit" decoration v disasm gutter (později)
     * Reset na -1 při DBGAPI_MSG_RUNNING (= user pustil Play). */
    int last_triggered_id;                  /**< -1 = žádný, jinak bpt_id posledně zasaženého BP. */
    uint64_t last_triggered_at_frame;       /**< g_gdg.total_elapsed.screens v okamžiku HIT - pro fade-out efekt (volitelné). */
    bool scroll_to_last_triggered;          /**< One-shot: BP list okno na render zavolá SetScrollHereY, pak resetne na false. Set při HIT. */

    /* === V1.7: Pending filter pro BP list okno ===
     * Disasm context menu "Show all BPs at 0xXXXX in BP List" set těchto
     * polí a otevře BP window (g_gui->showBreakpointsWindow). BP window
     * při dalším renderu detekuje pending_filter_active, aplikuje filter
     * (= ukáže jen BP které find_all_by_addr(addr) vrací), pak resetuje
     * pending_filter_active na false. Plná podpora filter UI = Fáze C
     * (Notebook redesign); zatím jen marker. */
    bool pending_filter_active;             /**< true = BP window má aplikovat pending_filter_addr filter. */
    uint16_t pending_filter_addr;           /**< CPU adresa pro filter (= BPs related to this addr). */

    /* === V1.7 C.3.4: Per-tab filter state ===
     * BP list okno má v každém per-typ tabu (EXEC / MEM / IO / IRQ /
     * EVENTS / MISC) vlastní text filter (case-insensitive substring
     * match nad label + condition + target text) a enabled-only flag.
     * Indexace odpovídá en_BPT_TAB_FILTER (= 6 hodnot). Tab "All" má
     * vlastní index pole pro symetrii, ale použití je v tree view
     * V1.7 zatím nepovinné.
     *
     * Hodnoty se neresetují při zavření okna - persist se zapojí v
     * C.3.5 (cfgmain BREAKPOINTS_PANEL sekce). */
#define BPT_TAB_FILTER_COUNT 6              /**< EXEC, MEM, IO, IRQ, EVENTS, MISC */
#define BPT_TAB_FILTER_TEXT_LEN 128
    char    tab_filter_text[BPT_TAB_FILTER_COUNT][BPT_TAB_FILTER_TEXT_LEN]; /**< Per-tab substring filter. */
    bool    tab_filter_enabled_only[BPT_TAB_FILTER_COUNT];                  /**< Per-tab "jen enabled" toggle. */

    /* === V1.7 C.3.5: Aktivní tab + restore-on-startup ===
     * Při změně tabu UI zaktualizuje active_tab. Při restartu cfgmain
     * propaguje hodnotu z .ini do active_tab + nastaví restore_active_tab_once
     * = true. První render BP okna pak forsne BeginTabItem flag
     * ImGuiTabItemFlags_SetSelected na odpovídající tab a flag se
     * vynuluje.
     *
     * Hodnoty: 0 = All (tree view), 1..6 = en_BPT_TAB_FILTER + 1
     * (1=EXEC, 2=MEM, 3=IO, 4=IRQ, 5=EVENTS, 6=MISC). Offset o 1 je
     * kvuli CFGENTYPE_KEYWORD persistence, kde sentinel -1 zabranuje
     * pouzit hodnoty < 0 pro platne taby. Pro tab-to-enum konverzi
     * pouzij BPT_ACTIVE_TAB_TO_FILTER macro. */
    unsigned active_tab;                    /**< 0 = All, 1..6 = en_BPT_TAB_FILTER + 1. */
    bool     restore_active_tab_once;       /**< One-shot trigger pro force-select po startu. */

    /* === V1.E.6.A: pending focus z Activity dvojkliku ===
     * pending_focus_id > 0 = render-loop má najít BP s daným id, scrollnout
     * a aktivovat selection (= otevřít editační panel pro daný BP). Po
     * spotřebě je 0. */
    int      pending_focus_id;              /**< BP ID požadovaný k zafokusování (0 = none). */
} BptUIState;


/**
 * @brief Konverze active_tab unsigned na en_BPT_TAB_FILTER nebo -1 (All).
 *
 * Vrátí -1 pokud active_tab == 0 (= "All" tab), jinak (active_tab - 1)
 * jako en_BPT_TAB_FILTER. Použij pro: porovnání s tab indexem v render
 * loopu, lookup do tab_filter_*[] polí, atd.
 *
 * Pro tab_filter_*[] polí (= 6 hodnot, indexy 0..5) je macro definované
 * tak, aby return value pro persist=0 (All) bylo -1 = caller musí check.
 */
#define BPT_ACTIVE_TAB_TO_FILTER(active_tab) \
    ( ( (active_tab) == 0 ) ? -1 : (int)( (active_tab) - 1 ) )

/** @brief Reverzní konverze: en_BPT_TAB_FILTER hodnota nebo -1 -> persist unsigned. */
#define BPT_FILTER_TO_ACTIVE_TAB(filter) \
    ( ( (filter) < 0 ) ? 0u : (unsigned)( (filter) + 1 ) )


extern BptUIState g_bpt_ui;


#endif /* BPT_STATE_H */
