/**
 * @file gdg_window.h
 * @brief Debugger: GDG (Graphic Display Generator) state inspector window
 *        (gdg-panel F1 scaffold).
 *
 * GDG je custom video LSI obvod společný pro Sharp MZ-700, MZ-800 a MZ-1500.
 * Per-arch state struktura se liší (jiné módy, palety, atribut RAM), proto je
 * okno implementováno jako shell (`gdg_window.cpp`) + per-arch renderery
 * (`gdg_window_mz800.cpp`, `gdg_window_mz700.cpp`, `gdg_window_mz1500.cpp`)
 * a dispatch běží přes `#if MZARCH ==`.
 *
 * Ve fázi F1 (= toto) všechny per-arch renderery vykreslují pouze placeholder
 * "TODO" label. Obsah doplní fáze F3 (MZ-800), F4 (MZ-700) a F5 (MZ-1500).
 *
 * Visibilitu drží `g_gui->showGdgStateWindow`.
 *
 * License: GPLv3.
 */

#ifndef GDG_WINDOW_H
#define GDG_WINDOW_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Vykreslí ImGui okno se stavovým snapshotem GDG.
 *
 * Per-arch dispatch je uvnitř implementace (`gdg_window.cpp`). Volá se z
 * `main_window.cpp` per frame, pokud je `*p_open` true. ImGui pak při
 * zavření okna user-em (křížek) nastaví `*p_open` na false.
 *
 * @param p_open  Pointer na visibility flag (typicky `&g_gui->showGdgStateWindow`).
 *                Nesmí být NULL.
 */
void imgui_gdg_state_window(bool *p_open);

#ifdef __cplusplus
}
#endif

#endif /* GDG_WINDOW_H */
