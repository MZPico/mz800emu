/**
 * @file ctc_window.h
 * @brief Debugger: CTC 8253 chip state inspector window (per-chip-panels F1).
 *
 * Real-time snapshot vnitřního stavu CTC 8253 - 3 nezávislé čítače / timery.
 * V F1 fázi prázdné scaffolding okno (jen `ImGui::Begin/End` + TODO label),
 * obsah se naplní ve fázi F2.
 *
 * Visibilitu drží `g_gui->showCtcStateWindow`.
 *
 * License: GPLv3.
 */

#ifndef CTC_WINDOW_H
#define CTC_WINDOW_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Vykreslí ImGui okno se stavovým snapshotem CTC 8253.
 *
 * Volá se z `main_window.cpp` per frame, pokud je `*p_open` true. ImGui pak
 * při zavření okna user-em (křížek) nastaví `*p_open` na false.
 *
 * @param p_open  Pointer na visibility flag (typicky `&g_gui->showCtcStateWindow`).
 *                Nesmí být NULL.
 */
void imgui_ctc_state_window(bool *p_open);

#ifdef __cplusplus
}
#endif

#endif /* CTC_WINDOW_H */
