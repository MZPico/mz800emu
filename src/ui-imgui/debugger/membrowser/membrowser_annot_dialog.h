/**
 * @file membrowser_annot_dialog.h
 * @brief ImGui dialog pro přidávání/editaci annotací + auto-load/save (V6).
 *
 * Drží globální pending request (request_open) + popup state. Renderuje
 * modal "Add comment..." s text editorem a color picker.
 *
 * V6 také zařizuje auto-load při startu a auto-save při shutdown přes
 * relative path "membrowser_annotations.txt" v current working directory
 * (= obvykle adresář s mz800emu.exe). Volá se z window register/destroy.
 *
 * Threading: UI-thread only.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_ANNOT_DIALOG_H
#define MEMBROWSER_ANNOT_DIALOG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Vyžádá otevření Add/Edit annotation dialogu.
 *
 * @param region_kind   en_REGION_KIND.
 * @param region_sub_id Disambiguator.
 * @param addr          Offset (typicky pozice kurzoru).
 */
void membrowser_annot_dialog_request_open ( int region_kind, int region_sub_id,
                                              uint32_t addr );


/**
 * @brief Renderuje annotation modal (volá se z mb_window render).
 *
 * Spotřebuje pending request, zobrazí popup s aktuálním textem (pokud
 * existuje), umožní edit / smazat / barva.
 */
void membrowser_annot_dialog_render ( void );


/**
 * @brief Renderuje hover tooltip pro daný (kind, sub_id, addr) pokud
 *        annotace existuje.
 *
 * Caller (hexview) volá tuto fci po ImGui::IsItemHovered() detekci nad
 * řádkem/buňkou. Pokud annotace neexistuje, no-op.
 *
 * @param region_kind   en_REGION_KIND.
 * @param region_sub_id Disambiguator.
 * @param addr          Offset.
 */
void membrowser_annot_dialog_render_tooltip ( int region_kind,
                                                int region_sub_id,
                                                uint32_t addr );


/**
 * @brief Volá se na startu - načte persist file.
 *
 * Idempotentní - opakované volání reset + reload.
 */
void membrowser_annot_dialog_load_on_startup ( void );


/**
 * @brief Volá se při shutdown - uloží persist file.
 */
void membrowser_annot_dialog_save_on_shutdown ( void );


#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_ANNOT_DIALOG_H */
