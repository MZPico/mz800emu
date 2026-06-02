/**
 * @file membrowser_fileio.h
 * @brief Load BIN / Save BIN file dialogy pro Memory Browser.
 *
 * Wrap nad ImGuiFileDialog (IGFD). Umožňuje uživateli dumpnout obsah
 * vybraného regionu do .bin souboru, nebo opačně nahrát .bin do
 * regionu (jen pro writable regiony). I/O chodí přes dbgapi_regions
 * adapter (membrowser_io), tj. respektuje banking a R/O ochranu.
 *
 * Lifecycle:
 *   - membrowser_fileio_open_save(region_id) ulozí region_id + otevře
 *     IGFD save dialog v dalším frame.
 *   - membrowser_fileio_open_load(region_id) dtto pro Load.
 *   - membrowser_fileio_render_dialogs() volej per frame z hlavního
 *     okna, drží stav dialogů + zpracuje IsOk() callbacky.
 *
 * Stav je file-static singleton - V0 podporuje jen jednu Load + jednu
 * Save operaci současně (singleton instance Memory Browser).
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_FILEIO_H
#define MEMBROWSER_FILEIO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Otevře Save BIN dialog pro daný region.
 *
 * Nastavi interni state + při dalším render volání zobrazí file chooser.
 * Pokud region_id < 0 nebo region neexistuje, nic se neděje.
 *
 * @param region_id ID regionu z dbgapi_regions_enumerate.
 */
void membrowser_fileio_open_save ( int region_id );

/**
 * @brief Otevře Load BIN dialog pro daný region.
 *
 * Stejný princip jako save - po confirm se .bin obsah zapíše do regionu
 * přes dbgapi_regions_write. Pokud region je R/O, vrátí error toast.
 *
 * @param region_id ID regionu z dbgapi_regions_enumerate.
 */
void membrowser_fileio_open_load ( int region_id );

/**
 * @brief Vykreslí / zpracuje case otevřených dialogů (Load + Save).
 *
 * Voláno per frame z membrowser_view_render. Pokud žádný dialog není
 * otevřen, no-op.
 *
 * @pre Voláno mezi NewFrame a EndFrame.
 */
void membrowser_fileio_render_dialogs ( void );

#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_FILEIO_H */
