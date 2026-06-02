/*
 * File:   mcp_settings_dialog.h
 *
 * Konfigurační dialog pro MCP backend (V0.B.4).
 *
 * Vykresluje modální (ne-modal Begin, ale s NoCollapse) okno pro
 * editaci 4 INI klíčů sekce [MCP]:
 *   - tcp_port    (InputInt s validací 1024..65535)
 *   - bind_addr   (Combo 127.0.0.1 / 0.0.0.0)
 *   - profile     (Combo wild / confined / sandboxed / observer)
 *   - auto_start_tcp (Checkbox)
 *
 * Save tlačítko commituje hodnoty do g_mcp_config + zavolá
 * cfgroot_save(g_cfgmain) pro okamžitý INI persist; pokud TCP
 * server běží, restartuje ho s novými hodnotami. Cancel zahazuje
 * dočasné změny.
 *
 * Pri buildu s NO_MCP_TCP=1 je funkce no-op (žádný UI prvek se
 * nevykreslí, žádný g_gui flag se nesleduje).
 *
 * ----------------------------- License -------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MZ800EMU_UI_MCP_SETTINGS_DIALOG_H
#define MZ800EMU_UI_MCP_SETTINGS_DIALOG_H

#ifdef __cplusplus
extern "C" {
#endif


    /**
     * @brief Vykreslí MCP Settings dialog pokud je g_gui->showMcpSettingsWindow.
     *
     * Volat z main_window.cpp render cesty (po renderu ostatních dialogů).
     * Vnitřně se test na showMcpSettingsWindow + early return - kontrolér
     * nemusí guard.
     *
     * Při buildu s NO_MCP_TCP=1 (kaskáda NO_MCP=1) je tělo prázdné.
     *
     * @pre voláno z hlavního (GUI) vlákna v rámci ImGui frame
     */
    void imgui_mcp_settings_dialog(void);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MZ800EMU_UI_MCP_SETTINGS_DIALOG_H */
