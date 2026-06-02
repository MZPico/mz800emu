/*
 * File:   mcp_tools_menu.h
 *
 * Veřejné API pro MCP položky v top-menu "Tools" (V0.A.5).
 *
 * Aktuálně exponuje jedinou funkci `imgui_menu_mcp_tools_render`, která
 * vykreslí submenu "MCP TCP Server" s položkami Start / Stop / status.
 * Volá se z `src/ui-imgui/topmenu/menu_tools.cpp` při sestavování
 * Tools menu.
 *
 * Při buildu s `NO_MCP_TCP=1` (resp. cascade z `NO_MCP=1`) je tělo
 * funkce prázdné (žádný menu entry).
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

#ifndef MZ800EMU_UI_MCP_TOOLS_MENU_H
#define MZ800EMU_UI_MCP_TOOLS_MENU_H

#ifdef __cplusplus
extern "C" {
#endif


    /**
     * @brief Vykreslí položku "MCP TCP Server" v Tools menu.
     *
     * Volá se uvnitř `ImGui::BeginMenu(_L("Tools"))` z
     * `imgui_menu_tools`. Při buildu bez TCP backend (`NO_MCP_TCP=1`
     * nebo cascade `NO_MCP=1`) je tělo prázdné a žádné menu nepřibyde.
     *
     * Submenu nabízí:
     *  - Start (pokud server neběží) - spustí listener na default portu
     *  - Stop (pokud běží) - graceful shutdown listeneru
     *  - Read-only stav: "Listening on 127.0.0.1:PORT" + "Clients: N"
     *
     * Globální handle `g_mcp_tcp_server` se inicializuje lazy při
     * prvním kliknutí na Start.
     *
     * @pre voláno z hlavního (GUI) vlákna v rámci ImGui frame
     */
    void imgui_menu_mcp_tools_render(void);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MZ800EMU_UI_MCP_TOOLS_MENU_H */
