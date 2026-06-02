/*
 * File:   main_pipe.h
 *
 * Veřejné API pipe transportu MCP backendu (V0.A.4).
 *
 * Hlavní entry-point `mcp_pipe_main(argc, argv)` se volá:
 *  - buď z `src/main.c::main()` při detekci `--pipe` flagu (= aktuální
 *    cesta pro V0.A.4, vyhne se nutnosti separátní binárky),
 *  - nebo z `main()` v samostatné binárce `mz800emu_pipe(.exe)` (=
 *    původní cíl V0.A.4 dle INSTRUKCE; build separátní binárky se v
 *    MSYS2/UCRT64 v této fázi nepodařilo zprovoznit, viz VYSLEDEK).
 *
 * Funkce blokuje volajícího do shutdown signálu (EOF na stdin /
 * shutdown command / SIGINT / SIGTERM), poté provede graceful cleanup
 * a vrátí návratovou hodnotu pro `main()`.
 *
 * Soubor je guardován `MZ800EMU_CFG_MCP_SERVER_ENABLED` - při buildu
 * s `NO_MCP=1` se stane prázdnou translation unit.
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

#ifndef MZ800EMU_MCP_MAIN_PIPE_H
#define MZ800EMU_MCP_MAIN_PIPE_H

#ifdef __cplusplus
extern "C" {
#endif


    /**
     * @brief Hlavní entry-point pipe transportu MCP backendu.
     *
     * Provede inicializaci emulátoru v headless režimu (`--headless` se
     * injectuje do argv), spawnuje stdin reader thread, pošle HELLO
     * message, a blokuje hlavní vlákno na shutdown signálu. Po shutdown
     * provede graceful cleanup (join threadů, destroy mutexů, ...) a
     * vrátí návratovou hodnotu pro `main()`.
     *
     * @param argc  počet CLI argumentů (předaný z volajícího `main`)
     * @param argv  pole CLI argumentů (předané z volajícího `main`)
     * @return EXIT_SUCCESS pokud shutdown proběhl normálně (EOF /
     *         shutdown command / SIGINT), EXIT_FAILURE při chybě
     *         inicializace.
     *
     * @pre  argc > 0, argv != NULL
     * @post při návratu jsou všechny resources cleanupnuté (sdlapp,
     *       iface, dbgapi, mutexy, threadové handle)
     *
     * @note Funkce není znovu volatelná - po prvním volání jsou
     *       globální stavy emulátoru spotřebované.
     */
    int mcp_pipe_main(int argc, char *argv[]);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MZ800EMU_MCP_MAIN_PIPE_H */
