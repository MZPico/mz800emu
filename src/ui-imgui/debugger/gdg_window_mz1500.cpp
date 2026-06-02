/**
 * @file gdg_window_mz1500.cpp
 * @brief Debugger: GDG state inspector - MZ-1500 specifický render
 *        (gdg-panel F5 obsah - PCG state).
 *
 * MZ-1500 jediné specifikum oproti MZ-700 je PCG (Programmable Character
 * Generator). Panel ukazuje:
 *   - Mode & Priority: DMD bit 0 (MZ-700 vs MZ-1500 PCG mód) a bit 1
 *     (BPF = Background-PCG-Foreground vs BFP = Background-Foreground-PCG).
 *     PCG overlay se uplatní jen pokud MZ-1500 mód aktivní; priorita
 *     určuje, zda PCG je nad nebo pod foreground vrstvou.
 *   - PCG bank mapping: SPEC pole z `g_memory.map` určuje, která PCG
 *     banka (1/2/3) je aktuálně viditelná pro CPU v D000-EFFF (jen
 *     pokud ROM_UPPER aktivní). PCG data jsou jinak vždy renderovaná
 *     z `g_memory.PCG` (3 banky x 8 KB), banking ovlivňuje jen CPU
 *     přístup, ne GDG rendering.
 *   - PCG usage: počet znakových pozic s aktivním PCG overlay (bit 3
 *     v PCGHI VRAM byte). Indikátor zda aplikace PCG vůbec využívá.
 *   - PCG palette: 8 IGRB indexů (port 0xF1), barevný overlay PCG
 *     pixelů. Index 0 znamená "PCG pixel transparent" v shrunk renderer
 *     logice (viz `mz1500_framebuffer.c`).
 *
 * Data source: výhradně přes `gdg_mirror_snapshot()` (side-effect free,
 * F2 + F5 mirror API). Žádné `gdg_write_*()`, žádný state mutation.
 *
 * Co panel NEzobrazuje (vědomě):
 *   - Per-position attribute RAM dump (CHAR/ATTR/PCGLO/PCGHI per buňka)
 *     - patří VRAM browser / Memory Map okno.
 *   - Dump PCG pattern dat (3 banky x 8 KB) - patří Memory Map okno
 *     (mhmap má `pcg-1`/`pcg-2`/`pcg-3` regiony).
 *   - Border (regBOR) - MZ-1500 nemá border port, vždy 0.
 *   - Raster pozice - přijde ve F6 (společná sekce pro 700/800/1500).
 *
 * License: GPLv3.
 */

#include "mzarch/mzarch_config.h"

#if MZARCH == 1500

#include "main.h"
#include "libs/sdlapp/sdlapp.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "libs/imgui/imgui.h"

#include "i18n.h"

#include <stdio.h>
#include <stdint.h>

extern "C"
{
#include "emulator/mzarch/mz1500/gdg/mz1500_gdg_mirror.h"
#include "emulator/display.h"
}

/* ========================================================================
 * Konstanty a label tabulky
 * ====================================================================== */

/**
 * @brief Lidsky čitelná jména SPEC mapování D000-EFFF.
 *
 * Index = `pcg_spec_id` 0..4: 0 = žádné (VRAM/RAM), 1 = CGROM, 2-4 = PCG1-3.
 * N_() značí pro gettext extraction; runtime překlad přes _() v render.
 */
static const char *const c_spec_id_names[5] = {
    N_("none (VRAM / RAM)"), /* 0 */
    N_("CGROM"),             /* 1 */
    N_("PCG bank 1"),        /* 2 */
    N_("PCG bank 2"),        /* 3 */
    N_("PCG bank 3"),        /* 4 */
};

/**
 * @brief Velikost jednoho PCG palette swatch ve gridu (px).
 *
 * Stejný styl jako MZ-800 panel, jen 8 swatchů v jedné řadě.
 */
static const ImVec2 c_pcg_swatch_size = ImVec2(36.0f, 36.0f);

/* ========================================================================
 * Pomocné funkce
 * ====================================================================== */

/**
 * @brief Vrátí ImVec4 (RGBA float) ze surového 24-bit RGB packed uint32.
 *
 * Vstup je formát `0x00RRGGBB` (= jako v `g_display_predef_colors[]`).
 * Alfa je vždy 1.0 (immutable swatches).
 *
 * @param rgb24  0xRRGGBB hodnota.
 * @return ImVec4 v rozsahu 0..1.
 */
static ImVec4 rgb24_to_imvec4(uint32_t rgb24)
{
    float r = (float)((rgb24 >> 16) & 0xFFu) / 255.0f;
    float g = (float)((rgb24 >> 8)  & 0xFFu) / 255.0f;
    float b = (float)((rgb24)       & 0xFFu) / 255.0f;
    return ImVec4(r, g, b, 1.0f);
}

/**
 * @brief Vrátí aktuální IGRB → RGB tabulku podle nastaveného color schema.
 *
 * Color schema (Normal/Grayscale/Green) je live nastavení uživatele -
 * panel ukazuje barvy jak je vidí na displeji, ne pevné "kanonické" RGB.
 *
 * @return Pointer na 16-prvkové pole `uint32_t` (0xRRGGBB).
 */
static const uint32_t *current_color_table(void)
{
    switch (g_display.color_schema)
    {
        case DISPLAY_GRAYSCALE:
            return g_display_predef_grays;
        case DISPLAY_GREEN:
            return g_display_predef_greens;
        case DISPLAY_NORMAL:
        default:
            return g_display_predef_colors;
    }
}

/* ========================================================================
 * Render sekce
 * ====================================================================== */

/**
 * @brief Vykreslí "Mode & Priority" sekci - dekód DMD bitů.
 *
 * Bit 0 (MODE) říká, zda je GDG v MZ-700 kompatibilním modu nebo v PCG
 * modu. Bit 1 (PRIORITY) určuje pořadí vrstev: BPF (background → PCG →
 * foreground) nebo BFP (background → foreground → PCG).
 *
 * @param snap  Snapshot mirror struktura.
 */
static void render_mode_section(const st_GDG_MIRROR_MZ1500 *snap)
{
    ImGui::Text("%s: 0x%02X",
                _("DMD"),
                (unsigned)snap->regDMD);

    if (snap->dmd_mode1500)
    {
        ImGui::Text("%s: %s",
                    _("Mode"),
                    _("MZ-1500 PCG overlay active"));
    }
    else
    {
        ImGui::Text("%s: %s",
                    _("Mode"),
                    _("MZ-700 compat (no PCG overlay)"));
    }

    ImGui::Text("%s: %s",
                _("Layer priority"),
                snap->dmd_pmode_bfp
                    ? _("BFP (background -> foreground -> PCG)")
                    : _("BPF (background -> PCG -> foreground)"));
}

/**
 * @brief Vykreslí "PCG bank mapping" sekci.
 *
 * Ukazuje, která PCG banka je viditelná v CPU adresním prostoru
 * D000-EFFF (= SPEC pole `g_memory.map`). Mapování je aktivní jen
 * pokud ROM_UPPER (= horní oblast přepnutá na ROM/SPEC).
 *
 * @param snap  Snapshot mirror struktura.
 */
static void render_pcg_mapping_section(const st_GDG_MIRROR_MZ1500 *snap)
{
    unsigned spec = snap->pcg_spec_id;
    const char *spec_name = (spec < 5u)
                                ? _(c_spec_id_names[spec])
                                : "?";

    /* Table layout: 2 sloupce (label + value). Vzor jako Raster sekce
     * v gdg_window.cpp. Hint texty pod tabulkou zůstanou inline jako
     * jednoduché řádky (nemají label:value strukturu). */
    if (ImGui::BeginTable("##GDGMZ1500PCGMapTable", 2,
                          ImGuiTableFlags_SizingFixedFit
                              | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("%s:", _("CPU view of D000-EFFF"));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(snap->pcg_spec_active
                                   ? spec_name
                                   : _("VRAM / RAM (SPEC inactive)"));

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("%s:", _("SPEC id"));
        ImGui::TableNextColumn(); ImGui::Text("%u", spec);

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("%s:", _("PCG bank size"));
        ImGui::TableNextColumn(); ImGui::Text("0x%X B", snap->pcg_bank_size);

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("%s:", _("PCG bank count"));
        ImGui::TableNextColumn(); ImGui::Text("%u", snap->pcg_bank_count);

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(_("Note: SPEC mapping affects only CPU access."));
    ImGui::TextUnformatted(_("GDG always reads all 3 PCG banks for rendering."));
}

/**
 * @brief Vykreslí "PCG usage" sekci - počet pozic s aktivním PCG overlay.
 *
 * Spočítá bit 3 (PCG enable) v PCGHI VRAM regionu (0xC00-0xFFF).
 * 0 = aplikace PCG nepoužívá, > 0 = některé buňky mají PCG overlay.
 *
 * @param snap  Snapshot mirror struktura.
 */
static void render_pcg_usage_section(const st_GDG_MIRROR_MZ1500 *snap)
{
    ImGui::Text("%s: %u / %u",
                _("Cells with PCG overlay"),
                snap->pcg_active_cells,
                1024u);
    if (snap->pcg_active_cells == 0u)
    {
        ImGui::TextUnformatted(
            _("No PCG overlay in VRAM - app uses CGROM characters only."));
    }
    else if (snap->dmd_mode1500 == 0u)
    {
        ImGui::TextUnformatted(
            _("PCG enable bits set in VRAM but DMD mode = MZ-700"));
        ImGui::TextUnformatted(
            _("(PCG overlay is inactive in this frame)."));
    }
}

/**
 * @brief Vykreslí "PCG palette" sekci - 8 IGRB swatchů.
 *
 * Paleta z portu 0xF1 (`mode1500_color[8]`). Index 0 v rendereru znamená
 * "PCG pixel transparent" (viz `mz1500_framebuffer.c`), zbylých 7 indexů
 * (1..7) je 3-bit kód poskládaný z MSB bitů PCG bank 0/1/2.
 *
 * Klik na swatch otevře popup s informací o indexu a RGB.
 *
 * @param snap  Snapshot mirror struktura.
 */
static void render_pcg_palette_section(const st_GDG_MIRROR_MZ1500 *snap)
{
    const uint32_t *table = current_color_table();

    /* Static state pro popup - drží index naposledy klinutého swatche. */
    static int s_popup_index = -1;

    /* Push style: tight spacing pro grid swatchů. */
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

    for (int idx = 0; idx < 8; ++idx)
    {
        int igrb = snap->mode1500_color[idx] & 0x0F;
        ImVec4 c = rgb24_to_imvec4(table[igrb]);

        char btn_id[32];
        snprintf(btn_id, sizeof(btn_id), "##pcg_pal_%d", idx);
        if (ImGui::ColorButton(btn_id, c,
                               ImGuiColorEditFlags_NoTooltip
                                   | ImGuiColorEditFlags_NoAlpha,
                               c_pcg_swatch_size))
        {
            s_popup_index = idx;
            ImGui::OpenPopup("##pcg_palette_popup");
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("PCG color %d -> IGRB %u (0x%X)",
                              idx, (unsigned)igrb, (unsigned)igrb);
        }
        if (idx < 7)
            ImGui::SameLine();
    }

    ImGui::PopStyleVar();

    /* Popup s detailem zvolené barvy. */
    if (ImGui::BeginPopup("##pcg_palette_popup"))
    {
        if (s_popup_index >= 0 && s_popup_index < 8)
        {
            int igrb = snap->mode1500_color[s_popup_index] & 0x0F;
            uint32_t rgb = table[igrb];
            ImGui::Text("%s: %d",
                        _("PCG color index"), s_popup_index);
            ImGui::Text("IGRB = %u (0x%X)",
                        (unsigned)igrb, (unsigned)igrb);
            ImGui::Text("RGB: 0x%06X", rgb & 0x00FFFFFFu);
            ImGui::Text("R=%u  G=%u  B=%u",
                        (unsigned)((rgb >> 16) & 0xFFu),
                        (unsigned)((rgb >> 8)  & 0xFFu),
                        (unsigned)((rgb)       & 0xFFu));
            if (s_popup_index == 0)
            {
                ImGui::Spacing();
                ImGui::TextUnformatted(
                    _("Color 0 = transparent in PCG overlay logic"));
                ImGui::TextUnformatted(
                    _("(see mz1500_framebuffer.c renderer)."));
            }
        }
        ImGui::EndPopup();
    }

    /* Souhrnný výpis pod gridem. */
    ImGui::Spacing();
    ImGui::Text("mode1500_color[0..7] = %d, %d, %d, %d, %d, %d, %d, %d",
                snap->mode1500_color[0],
                snap->mode1500_color[1],
                snap->mode1500_color[2],
                snap->mode1500_color[3],
                snap->mode1500_color[4],
                snap->mode1500_color[5],
                snap->mode1500_color[6],
                snap->mode1500_color[7]);
}

/* ========================================================================
 * Entry point
 * ====================================================================== */

/**
 * @brief Vykreslí tělo GDG State okna pro MZ-1500.
 *
 * Volá se z `gdg_window.cpp` shell vrstvy mezi `ImGui::Begin()` a
 * `ImGui::End()`. Side-effect free: jen čte přes `gdg_mirror_snapshot()`.
 *
 * Sekce (CollapsingHeader, defaultně otevřené):
 *   - Mode & Priority (DMD dekód)
 *   - PCG bank mapping (SPEC mapping D000-EFFF)
 *   - PCG usage (počet aktivních PCG buněk v VRAM)
 *   - PCG palette (8 IGRB swatchů)
 *
 * Raster pozice + status signály budou doplněny ve F6 (společná sekce
 * pro 700/800/1500).
 */
void gdg_window_render_mz1500(void)
{
    st_GDG_MIRROR_MZ1500 snap;
    gdg_mirror_snapshot(&snap);

    if (ImGui::CollapsingHeader(_L("Mode & Priority###GDGMZ1500Mode"),
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        render_mode_section(&snap);
    }

    if (ImGui::CollapsingHeader(_L("PCG bank mapping###GDGMZ1500PCGMap"),
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        render_pcg_mapping_section(&snap);
    }

    if (ImGui::CollapsingHeader(_L("PCG usage###GDGMZ1500PCGUsage"),
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        render_pcg_usage_section(&snap);
    }

    if (ImGui::CollapsingHeader(_L("PCG palette###GDGMZ1500PCGPalette"),
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        render_pcg_palette_section(&snap);
    }
}

#endif /* MZARCH == 1500 */
