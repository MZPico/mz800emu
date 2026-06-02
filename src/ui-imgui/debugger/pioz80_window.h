/**
 * @file pioz80_window.h
 * @brief Debugger: Z80 PIO chip state inspector window (per-chip-panels F1).
 *
 * Real-time snapshot vnitřního stavu Z80 PIO - Port A/B, mode (output/input/
 * bidirectional/control), IO mask, interrupt control words.
 *
 * V F1 fázi prázdné scaffolding okno; obsah se naplní ve fázi F4.
 *
 * Visibilitu drží `g_gui->showPiozStateWindow`.
 *
 * Pozn.: Z80 PIO existuje jen na MZ-800 a MZ-1500 (joystick, parallel
 * port), MZ-700 ji nemá. Per-arch capability check ve F4.
 *
 * License: GPLv3.
 */

#ifndef PIOZ80_WINDOW_H
#define PIOZ80_WINDOW_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Vykreslí ImGui okno se stavovým snapshotem Z80 PIO.
 *
 * @param p_open  Pointer na visibility flag (typicky `&g_gui->showPiozStateWindow`).
 *                Nesmí být NULL.
 */
void imgui_pioz80_state_window(bool *p_open);

#ifdef __cplusplus
}
#endif

#endif /* PIOZ80_WINDOW_H */
