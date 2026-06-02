/**
 * @file membrowser_fill_dialog.h
 * @brief Fill dialog + undo/redo akce - ImGui modal + dispatch (V6).
 *
 * Drží globální "pending fill request" a renderuje modální popup pro
 * konfiguraci fill operace nad aktuálním regionem. Také implementuje
 * undo/redo dispatch (write zpět z @ref membrowser_undo + opačný push do
 * druhého stacku).
 *
 * Lifetime: globální singletony - pending request je per-process scalar,
 * žádná per-instance state (uživatel typicky pracuje s jedním fill dialogem
 * najednou). Modal se renderuje vždy v rámci posledního aktivního mb okna.
 *
 * Threading: UI-thread only.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_FILL_DIALOG_H
#define MEMBROWSER_FILL_DIALOG_H

#include <stdint.h>
#include <stdbool.h>

#include "hex_view_backend.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Vyžádá otevření Fill dialogu pro daný region key + start adresu.
 *
 * Volá se z hexview context menu (RMB → "Fill..."). Pending request je
 * spotřebován při příštím renderu hostujícím @ref membrowser_fill_dialog_render.
 *
 * @param region_kind   en_REGION_KIND aktuálního regionu.
 * @param region_sub_id Disambiguator.
 * @param start_offset  Start adresa (typicky pozice kurzoru).
 */
void membrowser_fill_dialog_request_open ( int region_kind, int region_sub_id,
                                            uint64_t start_offset );


/**
 * @brief Renderuje ImGui modal pro Fill dialog (volá se z mb_window render).
 *
 * Spotřebuje pending request (= volá ImGui::OpenPopup). Backend pointer
 * je třeba pro read-before-write (undo snapshot) + vlastní write_bytes.
 *
 * @param be            Aktuální hex view backend (NULL = no-op).
 * @param instance_idx  Memory Browser instance index (pro recently-edited
 *                      mark po úspěšném fill; -1 = neoznačovat).
 *
 * @pre Voláno z aktivního ImGui Begin/End scope (typicky uvnitř mb okna).
 */
void membrowser_fill_dialog_render ( const st_HEX_VIEW_BACKEND *be,
                                      int instance_idx );


/**
 * @brief Dispatch undo akce pro aktuální region.
 *
 * Pop top undo záznam, snapshot current bytes pro redo, write back undo bytes.
 * Po úspěšném undo navíc označí vrácený range bytů jako "recently edited"
 * pro vizuální badge v hex view (= "tento byte byl právě nedávno změněn").
 *
 * @param be            Aktuální hex view backend (NULL = no-op).
 * @param region_kind   en_REGION_KIND aktuálního regionu.
 * @param region_sub_id Disambiguator.
 * @param instance_idx  Memory Browser instance index (pro recently-edited
 *                      storage; -1 = neoznačovat).
 * @return true pokud bylo co undo, false pokud stack prázdný / chyba.
 */
bool membrowser_fill_dialog_do_undo ( const st_HEX_VIEW_BACKEND *be,
                                       int region_kind, int region_sub_id,
                                       int instance_idx );


/**
 * @brief Dispatch redo akce pro aktuální region (symetrické undo).
 *
 * @param be            Aktuální hex view backend.
 * @param region_kind   en_REGION_KIND aktuálního regionu.
 * @param region_sub_id Disambiguator.
 * @param instance_idx  Memory Browser instance index (pro recently-edited
 *                      mark; -1 = neoznačovat).
 * @return true pokud bylo co redo.
 */
bool membrowser_fill_dialog_do_redo ( const st_HEX_VIEW_BACKEND *be,
                                       int region_kind, int region_sub_id,
                                       int instance_idx );


/**
 * @brief Vrátí poslední status message (lokalizovaný anglický řetězec)
 *        pro status bar zobrazení.
 *
 * Reportuje úspěch / chybu posledního undo/redo/fill volání. Lifetime:
 * statický buffer, platí do dalšího volání jakékoliv fill_dialog operace.
 *
 * @return Pointer na statický řetězec nebo NULL pokud žádný status.
 */
const char *membrowser_fill_dialog_last_status ( void );


/**
 * @brief Vymaže poslední status (po zobrazení v UI).
 */
void membrowser_fill_dialog_clear_status ( void );


#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_FILL_DIALOG_H */
