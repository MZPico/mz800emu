/**
 * @file psg_window.h
 * @brief Debugger: PSG SN76489 chip state inspector window (per-chip-panels F1).
 *
 * Real-time snapshot vnitřního stavu PSG SN76489 - per-kanál tone / noise /
 * attenuation, latched channel + register selector.
 *
 * V F1 fázi prázdné scaffolding okno; obsah se naplní ve fázi F5.
 *
 * Visibilitu drží `g_gui->showPsgStateWindow`.
 *
 * Pozn.: MZ-800 a MZ-700 mají jeden PSG, MZ-1500 má stereo (2x PSG).
 * Per-arch instance count se vyřeší ve F5.
 *
 * License: GPLv3.
 */

#ifndef PSG_WINDOW_H
#define PSG_WINDOW_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Vykreslí ImGui okno se stavovým snapshotem PSG SN76489.
 *
 * @param p_open  Pointer na visibility flag (typicky `&g_gui->showPsgStateWindow`).
 *                Nesmí být NULL.
 */
void imgui_psg_state_window(bool *p_open);

#ifdef __cplusplus
}
#endif

#endif /* PSG_WINDOW_H */
