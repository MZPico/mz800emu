/**
 * @file ppi_window.h
 * @brief Debugger: PPI 8255 chip state inspector window (per-chip-panels F1).
 *
 * Real-time snapshot vnitřního stavu Intel 8255 PPI - Port A/B/C, last
 * Control Word, klávesnicová matrice, VKBD autotype state.
 *
 * V F1 fázi prázdné scaffolding okno; obsah se naplní ve fázi F3.
 *
 * Visibilitu drží `g_gui->showPpiStateWindow`.
 *
 * License: GPLv3.
 */

#ifndef PPI_WINDOW_H
#define PPI_WINDOW_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Vykreslí ImGui okno se stavovým snapshotem PPI 8255.
 *
 * @param p_open  Pointer na visibility flag (typicky `&g_gui->showPpiStateWindow`).
 *                Nesmí být NULL.
 */
void imgui_ppi_state_window(bool *p_open);

#ifdef __cplusplus
}
#endif

#endif /* PPI_WINDOW_H */
