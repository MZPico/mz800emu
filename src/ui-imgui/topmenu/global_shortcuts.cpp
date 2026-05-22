#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <glib.h>

#ifdef WINDOWS
#include <windows.h>
#endif

#include "libs/imgui/imgui.h"

// Lokalizace
#include "i18n.h"

#include "ui-imgui/bootstrap/myimgui.h"
#include "emulator.h"
#include "mzarch/mzarch_config.h"
#include "mzarch/mzarch_platform_functions.h"
#include "iface/iface_video.h"

#if HAVE_JOY
#include "iface/iface_joy.h"
#endif

#if CFG_HWEXT_HAVE_FDC
#include "hw-generic/fdc/fdc.h"
#endif /* CFG_HWEXT_HAVE_FDC */

#include "hw-generic/pio8255/pio8255.h"
#include "debugger/debugger.h"
#include "ui-imgui/debugger/debugger_state.h"
#include "ui-imgui/debugger/breakpoints/bpt_state.h"
#include "ui-imgui/debugger/dbgapi_helpers.h"

#if HAVE_JOY
#include "hw-generic/joy/joy.h"
#endif

#define TEST_HOTKEYS_DISABLED 0

extern "C"
{
    void imgui_global_shortcuts(void);

    /* Snapshot request funkce (definovány v menu_snapshot.cpp) */
    void snapshot_ui_request_load(void);
    void snapshot_ui_request_save(void);
    void snapshot_ui_request_quickload(void);
    void snapshot_ui_request_quicksave(void);
}

void imgui_global_shortcuts(void)
{
    // Globalni klavesove zkratky aplikovane na cely ImGui

    ImGuiIO &io = ImGui::GetIO();

    /*
     * Reset: F12
     */

    if (ImGui::IsKeyPressed(ImGuiKey_F12, false))
    {
        mzarch_platform_fn_reset_request();
    };

    /*
     * Joy calibration: F11
     * Reset: F11 (only in Windows, if debugger is present)
     */
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
    {
#ifdef WINDOWS
        if (IsDebuggerPresent())
        {
            printf("Debugger is present! F11 - is now remapped as RESET.\n");
            mzarch_platform_fn_reset_request();
        }
        else
        {
#if HAVE_JOY
            iface_joy_get_calibration();
#endif
        };
#else
#if HAVE_JOY
        iface_joy_get_calibration();
#endif
#endif
    };

    /*
     * Zakaze klavesove zkratky Alt + xx, pokud je to pozadovano
     */
    if (TEST_HOTKEYS_DISABLED)
    {
        return;
    };

    // Alt + xx klavesove zkratky
    if (io.KeyAlt)
    {
        /*
         * Alt + Ctrl + Enter
         * ==================
         *
         * Swith between fullscreen and windowed mode
         *
         */
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        {
            if (io.KeyCtrl)
            {
                if (g_iface_video_callbacks->switch_fullscreen)
                {
                    g_iface_video_callbacks->switch_fullscreen();
                }
                else
                {
                    WARN("Switch fullscreen - callback not found!\n");
                };
            };
        };

        /*
         * Alt + C
         * ========
         *
         * Show/Hide Virtual CMT window
         *
         */
        if (ImGui::IsKeyPressed(ImGuiKey_C, false))
        {
            g_gui->showVirtualCmtWindow = !g_gui->showVirtualCmtWindow;
        };

        /*
         * Alt + M: switch between MAX and ( NORMAL or CUSTOM )
         * Alt + Shift + M : switch between CUSTOM and ( NORMAL or MAX )
         */
        if (ImGui::IsKeyPressed(ImGuiKey_M, false))
        {
            if (io.KeyShift)
            {
                // switch between CUSTOM and ( NORMAL or MAX )
                emulator_switch_to_custom_speed();
            }
            else
            {
                // switch between MAX and ( NORMAL or CUSTOM )
                emulator_max_speed(!EMULATOR_TEST_MAX_SPEED);
            };
        };

        /*
         * Alt + N: Force normal speed (100%)
         */
        if (ImGui::IsKeyPressed(ImGuiKey_N, false))
        {
            emulator_switch_to_normal_speed();
        };

        /*
         * Alt + P: Pause/Resume emulation
         *
         * Profiler V1 F5: Alt+Shift+P = toggle CPU Profiler okno (per-function
         * CPU profilace). Shift se kontroluje pred prostym Alt+P aby combo
         * varianta nebyla "snenita" prostou variantou (pattern shoda
         * s Alt+Shift+W pro Watch okno).
         */
        if (ImGui::IsKeyPressed(ImGuiKey_P, false))
        {
#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
            if (io.KeyShift)
            {
                g_gui->showProfilerWindow = !g_gui->showProfilerWindow;
            }
            else
#endif
            {
                dbg_ui_pause_toggle();
            };
        };

        /*
         * Virtual keyboard: Alt + K
         */
        if (ImGui::IsKeyPressed(ImGuiKey_K, false))
        {
            g_gui->showVirtualKeyboardWindow = !g_gui->showVirtualKeyboardWindow;
        };

        /*
         * Fix window aspect ratio by With: Alt + W
         *
         * Watch V1 (Phase C): Alt+Shift+W = toggle Watch okno
         * (user-defined paměťové hlídky). Shift se kontroluje pred
         * prostym Alt+W aby combo varianta nebyla "snenita" prostou
         * variantou (pattern shoda s Alt+Shift+R / Alt+Shift+S / Alt+Shift+H).
         */
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
        {
#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
            if (io.KeyShift)
            {
                g_gui->showWatchWindow = !g_gui->showWatchWindow;
            }
            else
#endif
            {
                if (g_iface_video_callbacks->fix_window_aspect_ratio)
                {
                    g_iface_video_callbacks->fix_window_aspect_ratio('W');
                }
                else
                {
                    WARN("Fix window aspect ratio - callback not found!\n");
                };
            };
        };

        /*
         * Fix window aspect ratio by Height: Alt + H
         *
         * V9: Alt+Shift+H = toggle Stack History samostatne okno (= SP
         * history sparkline plot resize s velikosti okna). Shift se
         * kontroluje pred prostym Alt+H, aby combo varianta nebyla
         * "snenita" prostou variantou. Behavior shoda s Alt+Shift+S pro
         * Stack Regions okno.
         */
        if (ImGui::IsKeyPressed(ImGuiKey_H, false))
        {
#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
            if (io.KeyShift)
            {
                g_gui->showStackHistoryWindow = !g_gui->showStackHistoryWindow;
            }
            else
#endif
            {
                if (g_iface_video_callbacks->fix_window_aspect_ratio)
                {
                    g_iface_video_callbacks->fix_window_aspect_ratio('H');
                }
                else
                {
                    WARN("Fix window aspect ratio - callback not found!\n");
                };
            };
        };

        /*
         * Speed up 1%: Alt + Up
         * Speed up 10%: Alt + Shift + Up
         */
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        {
            int step = (io.KeyShift) ? 10 : 1;
            customspeed_step_up_request(step);
        };

        /*
         * Speed down 1%: Alt + Down
         * Speed down 10%: Alt + Shift + Down
         */
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        {
            int step = (io.KeyShift) ? 10 : 1;
            customspeed_step_down_request(step);
        };

        /*
         * Speed up 100%: Alt + PgUp
         */
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
        {
            customspeed_step_up_request(100);
        };

        /*
         * Speed down 100%: Alt + PgDown
         */
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
        {
            customspeed_step_down_request(100);
        };

#if CFG_HWEXT_HAVE_FDC
        /*
         * Mount/Umount FD0 DSK image: Alt + 1
         */
        if (ImGui::IsKeyPressed(ImGuiKey_1, false))
        {
            int drive_id = 0;
            if (io.KeyShift)
            {
                printf("Eject FD%d DSK image\n", drive_id);
                fdc_umount(drive_id);
            }
            else
            {
                printf("Select FD%d DSK image for mount\n", drive_id);
                fdc_ui_mount(drive_id);
            };
        };

        /*
         * Mount/Umount FD1 DSK image: Alt + 2
         */
        if (ImGui::IsKeyPressed(ImGuiKey_2, false))
        {
            int drive_id = 1;
            if (io.KeyShift)
            {
                printf("Eject FD%d DSK image\n", drive_id);
                fdc_umount(drive_id);
            }
            else
            {
                printf("Select FD%d DSK image for mount\n", drive_id);
                fdc_ui_mount(drive_id);
            };
        };

        /*
         * Mount/Umount FD2 DSK image: Alt + 3
         */
        if (ImGui::IsKeyPressed(ImGuiKey_3, false))
        {
            int drive_id = 2;
            if (io.KeyShift)
            {
                printf("Eject FD%d DSK image\n", drive_id);
                fdc_umount(drive_id);
            }
            else
            {
                printf("Select FD%d DSK image for mount\n", drive_id);
                fdc_ui_mount(drive_id);
            };
        };

        /*
         * Mount/Umount FD3 DSK image: Alt + 4
         */
        if (ImGui::IsKeyPressed(ImGuiKey_4, false))
        {
            int drive_id = 3;
            if (io.KeyShift)
            {
                printf("Eject FD%d DSK image\n", drive_id);
                fdc_umount(drive_id);
            }
            else
            {
                printf("Select FD%d DSK image for mount\n", drive_id);
                fdc_ui_mount(drive_id);
            };
        };
#endif /* CFG_HWEXT_HAVE_FDC */

        /*
         * Snapshot hotkeys:
         * Alt + F6: Save Snapshot
         * Alt + F7: Load Snapshot
         * Alt + F8: Quick Save
         * Alt + F9: Quick Load
         */
        if (ImGui::IsKeyPressed(ImGuiKey_F6, false))
        {
            snapshot_ui_request_save();
        };

        if (ImGui::IsKeyPressed(ImGuiKey_F7, false))
        {
            snapshot_ui_request_load();
        };

        if (ImGui::IsKeyPressed(ImGuiKey_F8, false))
        {
            snapshot_ui_request_quicksave();
        };

        if (ImGui::IsKeyPressed(ImGuiKey_F9, false))
        {
            snapshot_ui_request_quickload();
        };

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
        /*
         * Debugger window: Alt + D
         * Toggle viditelnosti hlavního okna debuggeru (ImGui).
         * Při otevírání nastavíme flag, aby debugger window na tomto framu
         * přeskočil svůj Alt+D handler (jinak by se okno ihned zavřelo).
         */
        if (ImGui::IsKeyPressed(ImGuiKey_D, false))
        {
            if (!g_debugger.active)
                g_dbg_ui.opened_via_alt_d = true;
            debugger_show_hide_main_window();
        };

        /*
         * Breakpoints window: Alt + B
         * Toggle viditelnosti okna breakpointů (ImGui).
         */
        if (ImGui::IsKeyPressed(ImGuiKey_B, false))
        {
            if (!g_gui->showBreakpointsWindow)
                g_bpt_ui.opened_via_alt_b = true;
            g_gui->showBreakpointsWindow = !g_gui->showBreakpointsWindow;
        };

        /*
         * Variables window: Alt + V
         * Toggle viditelnosti $vars panelu (V1.5.B).
         */
        if (ImGui::IsKeyPressed(ImGuiKey_V, false))
        {
            g_gui->showVarsWindow = !g_gui->showVarsWindow;
        };

        /*
         * I/O Ports window: Alt + I
         * Toggle viditelnosti I/O Ports panelu (V1.5.D rework).
         */
        if (ImGui::IsKeyPressed(ImGuiKey_I, false))
        {
            g_gui->showIoWindow = !g_gui->showIoWindow;
        };

        /*
         * CPU Registers window: Alt + Shift + R
         * Toggle viditelnosti CPU Registers panelu (Variant B - samostatné
         * plovoucí okno s registr fileom, flagy a special sekcí).
         *
         * Alt+R bez Shift se kryje s NVIDIA Overlay zkratkou - vyžadujeme
         * Shift aby kombinace nebyla zachycena driverem.
         */
        if (ImGui::IsKeyPressed(ImGuiKey_R, false) && io.KeyShift)
        {
            g_gui->showCpuWindow = !g_gui->showCpuWindow;
        };

        /*
         * Stack Monitor window: Alt + S
         * Toggle viditelnosti Stack Monitor panelu (samostatné plovoucí
         * okno s hex dumpem paměti kolem aktuálního SP).
         *
         * V8: Alt+Shift+S = toggle Stack Regions samostatne okno
         * (= tabulka regionu 1 radek per region, oddelene od Stack
         * Monitor okna). Shift se kontroluje pred prostym Alt+S, aby
         * combo varianta nebyla "snenita" prostou variantou.
         */
        if (ImGui::IsKeyPressed(ImGuiKey_S, false))
        {
            if (io.KeyShift)
            {
                g_gui->showStackRegionsWindow = !g_gui->showStackRegionsWindow;
            }
            else
            {
                g_gui->showStackWindow = !g_gui->showStackWindow;
            };
        };

        /*
         * Memory browser window: Alt + E (placeholder — zatím neimplementováno)
         */

        /*
         * Dissassembler window: zkratka odebrána V1.5.D (= Alt+I nyní I/O Ports)
         */
#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
    }; // Alt + xx klavesove zkratky
}
