/**
 * @file ide8_window.h
 * @brief Debugger: deklarace IDE8 State inspector okna.
 *
 * Side-effect free ImGui okno pro live view stavu IDE8 radice (8-bit IDE/ATA
 * rozhrani pro 2 HDD - Master a Slave): controller registry + per-drive stav
 * (mount, CHS geometrie, kapacita, posledni prikaz, addressing, busmode,
 * status registr, aktualni blok). Cte vyhradne z `g_ide8`, zadny setter ani
 * write.
 *
 * Render guardovan `CFG_HWEXT_HAVE_IDE8` + `MZ800EMU_CFG_DEBUGGER_ENABLED`
 * v `imgui_ide8_state_window` (stubed implementace pro architektury bez IDE8).
 *
 * License: GPLv3.
 */

#ifndef IDE8_WINDOW_H_INCLUDED
#define IDE8_WINDOW_H_INCLUDED

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Render IDE8 State debugger okno.
     *
     * Vykresli ImGui okno se sekci Controller (sdilene registry + vybrana
     * mechanika) a dvema sekcemi per drive (Master / Slave). Cte aktualni
     * stav z globalniho `g_ide8` pri kazdem volani (kazdy frame); zadny
     * interni cache.
     *
     * @param p_open Pointer na bool s viditelnosti okna (ImGui idiom).
     *               Pri zavreni [X] se nastavi na false.
     *
     * Preconditions: ImGui kontext aktivni (volani mezi NewFrame a Render).
     * Side effects: zadne mimo ImGui draw listu.
     */
    void imgui_ide8_state_window(bool *p_open);

#ifdef __cplusplus
}
#endif

#endif /* IDE8_WINDOW_H_INCLUDED */
