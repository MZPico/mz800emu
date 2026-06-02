/**
 * @file gdg_window_mz700.cpp
 * @brief Debugger: GDG state inspector - MZ-700 specifický render
 *        (gdg-panel F4).
 *
 * MZ-700 GDG nemá žádný platform-specifický stav, který by panel ukazoval
 * (atributová RAM, znakový generátor a cursor blink nejsou součást GDG).
 * Reálný obsah okna pro MZ-700 = sekce "Raster" společná všem architekturám,
 * kterou doplní F6 v `gdg_window.cpp` shell vrstvě.
 *
 * Tato render funkce je proto úmyslně minimalistická - jen informativní
 * text, aby okno nebylo prázdné a uživatel věděl proč nevidí další obsah.
 *
 * Soubor je do překladu zařazen pouze pro MZARCH == 700; v MZ-800 a MZ-1500
 * buildu je celé tělo vyřazené `#if MZARCH == 700`, takže CMake GLOB může
 * soubor bezpečně chytnout pro všechny archy.
 *
 * License: GPLv3.
 */

#include "mzarch/mzarch_config.h"

#if MZARCH == 700

#include "main.h"
#include "libs/sdlapp/sdlapp.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "libs/imgui/imgui.h"

#include "i18n.h"

/**
 * @brief Vykreslí tělo GDG State okna pro MZ-700.
 *
 * Volá se z `gdg_window.cpp` shell vrstvy mezi `ImGui::Begin()` a
 * `ImGui::End()`. MZ-700 GDG nemá platform-specifický state - jen
 * informativní text. Společný raster (scanline / VBLN / HBLN / tempo)
 * doplní F6 ve shell vrstvě nad per-arch render.
 */
void gdg_window_render_mz700(void)
{
    ImGui::TextUnformatted(
        _("MZ-700 GDG has no platform-specific state."));
    ImGui::TextUnformatted(
        _("See common Raster section below for live timing info."));
}

#endif /* MZARCH == 700 */
