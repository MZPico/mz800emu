/**
 * @file membrowser_pcg.h
 * @brief PCG glyph editor pro MZ-1500 (V6).
 *
 * Samostatné ImGui okno editující jednu PCG banku (8 KB = 1024 chars,
 * každý char = 8 bajtů, 1 bajt = 1 řádek 8 pixelů). Banky 1..3 přepínané
 * comboboxem v okně.
 *
 * Aktivace přes membrowser kontextové menu (RMB nad bytem v PCG_1500
 * regionu) nebo Debugger menu → PCG editor. Otevřeno = g_gui->showPcgEditor.
 *
 * Akce: per-pixel click toggle, Inverse, Mirror H, Mirror V, Rotate 90 CW.
 * Edit přes membrowser_io_make_emu_backend (PCG bank má vlastní region kind).
 *
 * Threading: UI-thread only.
 *
 * Limit V6:
 *  - jen MZ-1500 (PCG_1500 region kind)
 *  - jeden char per view; "all chars grid" overview přidat v V6.1
 *  - žádné copy/paste mezi chars (Inverse/Mirror jen na current char)
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_PCG_H
#define MEMBROWSER_PCG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Render PCG editor window.
 *
 * Top-level ImGui Begin/End - není podokno membrowseru. p_open je
 * pointer na bool flag z g_gui (= toggle z menu).
 *
 * @param p_open  Pointer na bool - nastavený na false uživatelem zavře.
 */
void membrowser_pcg_window_render ( bool *p_open );


/**
 * @brief Vyžádá fokus a nastavení bank/char z externího kódu (např.
 *        hexview RMB nad PCG bytem).
 *
 * Setuje target_bank a target_char_idx; při příštím renderu okno vyfocusuje
 * a přepne na zadaný char.
 *
 * @param bank_idx       0..2 (= banky 1..3).
 * @param char_idx       0..255.
 */
void membrowser_pcg_window_focus_at ( int bank_idx, int char_idx );


#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_PCG_H */
