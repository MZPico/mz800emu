/**
 * @file membrowser_hexview.h
 * @brief Core hex table renderer (backend-agnostic).
 *
 * Renderuje scrollable hex tabulku přes ImGuiListClipper. Bere
 * st_HEX_VIEW_BACKEND pointer - NESmí znát konkrétní backend
 * implementaci (žádný direct dbgapi_regions_* call zde!).
 *
 * Edit logic je součástí - in-cell InputText při click. Edit
 * dispatch (validate + write) chodí přes backend.write_bytes.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_HEXVIEW_H
#define MEMBROWSER_HEXVIEW_H

#include <stdint.h>

#include "hex_view_backend.h"
#include "membrowser_state.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Volitelné PC/SP marker info pro highlight v hex view.
 *
 * Memory Browser jen pro Logical Z80 region (PC/SP jsou Z80 adresy);
 * pro ostatní regiony se předá pc_sp_valid=false a marker col i highlight
 * se neukáže. Pole pc/sp jsou 16-bit Z80 adresy a porovnávají se přímo
 * proti row_addr v rámci regionu (= identita s Z80 logickou adresou
 * v case Logical region).
 *
 * Lifetime: volající (membrowser_window) plní per-frame ze
 * g_mzarch_main.cpu->pc/sp; struct je read-only argument.
 */
typedef struct st_MEMBROWSER_PCSP_INFO
{
    bool pc_sp_valid;       /**< Marker info se vůbec uplatní (= Logical region). */
    bool show_pc;           /**< Zvýraznit PC řádek + 'P' marker. */
    bool show_sp;           /**< Zvýraznit SP řádek + 'S' marker. */
    uint16_t pc;            /**< Aktuální PC. */
    uint16_t sp;            /**< Aktuální SP. */
} st_MEMBROWSER_PCSP_INFO;

/**
 * @brief V1 extras - kontext potřebný pro Layers/Symbol overlay/context menu.
 *
 * Předáváno odděleně od st_HEX_VIEW_BACKEND aby budoucí non-emu konzumenti
 * (Media Browser, Glyph Editor) mohli volat hexview bez znalosti dbgapi
 * region kontextu - prostě předají NULL/0.
 *
 * @invariant region_kind = 0 (= REGION_KIND_LOGICAL) je validní default
 *            pokud caller nechce layer features (= žádný effect při
 *            disabled layers).
 */
typedef struct st_MEMBROWSER_HEXVIEW_EXTRAS {
    int       region_id;       /**< Pro snapshot lookup (-1 = disabled). */
    int       region_kind;     /**< en_REGION_KIND. */
    int       sub_id;          /**< Disambiguator. */
    uint32_t  logical_base;    /**< Pro symbol lookup; 0xFFFFFFFF = mimo Z80. */
} st_MEMBROWSER_HEXVIEW_EXTRAS;

/**
 * @brief Renderuje hex tabulku ve scrollable kontejneru.
 *
 * Předpokládá, že je již volán z aktivního ImGui kontextu (typicky
 * uvnitř child window nebo s explicit avail size).
 *
 * @param st       Per-window state (cursor, encoding, bytes_per_row, ...).
 * @param be       Backend adapter (read/write/total_size).
 * @param avail_h  Dostupná výška scroll regionu v pixelech.
 *
 * @pre st != NULL, be != NULL, be->read_bytes != NULL, be->total_size != NULL.
 * @post Žádný side-effect na backend kromě edit-commit (be->write_bytes).
 */
void membrowser_hexview_render ( st_MEMBROWSER_STATE *st,
                                 const st_HEX_VIEW_BACKEND *be,
                                 float avail_h,
                                 const st_MEMBROWSER_PCSP_INFO *pcsp );

/**
 * @brief Vrátí aktuální edit error zprávu (toast).
 *
 * Při neúspěšném ASCII edit commit (znak nelze reprezentovat v aktivním
 * encoding) nebo write_bytes selhání nastaví hexview interní flag a
 * zprávu. Volající (membrowser_window.cpp bottom bar) ji zobrazí jako
 * varovný banner.
 *
 * @return Pointer na statický řetězec (lifetime po dobu UI vlákna)
 *         nebo NULL pokud žádná aktivní chyba.
 */
const char *membrowser_hexview_get_edit_error ( void );

/**
 * @brief Vynuluje edit error toast.
 *
 * Volá se po zobrazení banneru, případně z UI tlačítka "dismiss" - pro
 * V0-leftovers stačí auto-dismiss při dalším úspěšném edit.
 */
void membrowser_hexview_clear_edit_error ( void );

/**
 * @brief V1 varianta renderu s PC/SP info A extras kontextem.
 *
 * Pokud @p pcsp = NULL, PC/SP markery se nezobrazí.
 * Pokud @p extras = NULL, layers/symbols/context menu se nezobrazí.
 * Obě sady jsou ortogonální - lze předat libovolnou kombinaci. Když jsou
 * obě NULL, chování shodné s membrowser_hexview_render(st, be, avail_h, NULL).
 *
 * @param st       Per-window state.
 * @param be       Backend adapter.
 * @param avail_h  Dostupná výška.
 * @param pcsp     V0-leftovers PC/SP marker info (NULL = bez markeru).
 * @param extras   V1 layer/symbol/context-menu kontext (NULL = bez layers).
 */
void membrowser_hexview_render_ex ( st_MEMBROWSER_STATE *st,
                                     const st_HEX_VIEW_BACKEND *be,
                                     float avail_h,
                                     const st_MEMBROWSER_PCSP_INFO *pcsp,
                                     const st_MEMBROWSER_HEXVIEW_EXTRAS *extras );

/**
 * @brief Zpracuje globální hex view klávesové zkratky (F2, Esc, Tab).
 *
 * V1-polish-2: dříve bylo zpracování těchto zkratek uvnitř
 * hexview_handle_edit_input (na úrovni hex view child window). Pokud byl
 * layers panel otevřený, hex view běžel v BeginChild scope a IsWindowFocused
 * pro child vracelo false pokud byl focus na hlavním okně (běžný stav).
 * F2 pak nikdy nezareagovalo. Navíc IsAnyItemActive() brzdil dispatch při
 * otevřeném Combo / aktivním InputText (Goto, Search).
 *
 * Nyní voláno z membrowser_view_render hned po Begin() (top-level mb okno).
 * Test IsWindowFocused(ChildWindows) zachytí i child windows hex view jako
 * focused. F2/Esc/Tab nejsou v InputText queue (function/control keys),
 * takže nemůžou kolizovat s aktivním InputText boxem.
 *
 * - F2:  toggle st->edit_enabled (ekvivalent kliku na Edit tlačítko).
 * - Esc: vypne st->edit_enabled pokud byl ON.
 * - Tab: v edit režimu přepne HEX <-> ASCII (jen pokud writable region).
 *
 * @param st        Per-window state.
 * @param writable  HW writable flag aktuálního regionu (pro odmítnutí F2
 *                  toggle u RO regionu - vizuálně by se ihned zase vypnul).
 *
 * @pre Voláno z aktivního ImGui Begin/End scope hlavního mb okna.
 * @post Změny st->edit_enabled / st->edit_mode / st->edit_nibble dle stisků.
 */
void membrowser_hexview_handle_global_shortcuts ( st_MEMBROWSER_STATE *st,
                                                   bool writable );

/**
 * @brief Zaregistruje ImGui ID toolbar InputText prvku, který má prioritu
 *        nad ASCII edit dispatch.
 *
 * V1-polish-4: ASCII edit používá @c io.InputQueueCharacters (= layout-aware,
 * respektuje národní rozložení klávesnice včetně diakritiky), ale toolbar
 * InputText widgety (Goto HEX/DEC, Search pattern) ji v každém frame
 * vyprazdňují, pokud drží ActiveID. Hexview dispatch musí poznat, že
 * jeden z nich má focus, a v takovém případě klávesy nekonzumovat.
 *
 * Volá se ihned po render InputText v okně scope (kde lze přečíst
 * @c ImGui::GetItemID() pro daný widget). Registrace je per-frame
 * platná - před každým hex view dispatchem se konzumuje aktuální seznam.
 *
 * @param id  ImGuiID (32-bit hash) toolbar InputText prvku. Hodnota 0
 *            (= reserved Imgui empty ID) se ignoruje.
 *
 * @pre Voláno v UI vlákně, ze scope hlavního mb okna.
 * @post Při dalším @c membrowser_hexview_render_ex se aktivní toolbar
 *       InputText bere v úvahu a ASCII dispatch se přeskočí.
 */
void membrowser_hexview_register_toolbar_input ( unsigned int id );

/**
 * @brief Vymaže seznam registrovaných toolbar InputText IDs.
 *
 * Volá se na začátku každého hex view render frame (typicky před
 * Begin toolbar render). Hexview funkce ji volá automaticky po konzumaci
 * - v normálním provozu volajícího nezajímá.
 */
void membrowser_hexview_reset_toolbar_inputs ( void );

#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_HEXVIEW_H */
