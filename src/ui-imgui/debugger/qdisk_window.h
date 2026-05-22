/**
 * @file qdisk_window.h
 * @brief Debugger: deklarace QDisk State inspector okna.
 *
 * Side-effect free ImGui okno pro live view chip stavu (SIO kanály A/B),
 * drive stavu (mount, R/O, storage_mode, has_unsaved, position) a pro
 * VIRTUAL režim virt state machine. Čte výhradně z `g_qdisk`, žádný
 * setter ani write.
 *
 * Render guardován `CFG_HWEXT_HAVE_QDISK` + `MZ800EMU_CFG_DEBUGGER_ENABLED`
 * v `imgui_qdisk_state_window` (stubed implementace pro architektury bez QD).
 *
 * License: GPLv3.
 */

#ifndef QDISK_WINDOW_H_INCLUDED
#define QDISK_WINDOW_H_INCLUDED

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Render QDisk State debugger okno.
     *
     * Vykreslí ImGui okno s vícero CollapsingHeader sekcemi (Device, SIO A,
     * SIO B, Drive/Storage, případně Virtual mode). Čte aktuální stav z
     * globálního `g_qdisk` při každém volání (každý frame); žádný interní
     * cache.
     *
     * @param p_open Pointer na bool s viditelností okna (ImGui idiom).
     *               Při zavření [X] se nastaví na false.
     *
     * Preconditions: ImGui kontext aktivní (volání mezi NewFrame a Render).
     * Side effects: žádné mimo ImGui draw listu.
     */
    void imgui_qdisk_state_window(bool *p_open);

#ifdef __cplusplus
}
#endif

#endif /* QDISK_WINDOW_H_INCLUDED */
