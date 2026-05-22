/**
 * @file   sdlapp_paths.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @brief  Aplikační adresáře (HOME / CFG / WORK) pro SDL3 aplikace.
 *
 * Standardní problém: aplikace potřebuje vědět, kde najít své read-only assety
 * (fonty, ikony, lokalizační .mo, ...) a kam zapsat user config (.ini).
 *
 * Spoléhání na CWD je nespolehlivé — klikací spuštění z file manageru má
 * CWD typicky @c C:\\Windows\\System32 nebo @c / na Linuxu, takže emulátor
 * nenajde svoje assety a zapisuje config do náhodného (často read-only)
 * místa.
 *
 * Tento modul zavádí 3 distinct adresáře:
 *
 * - **HOME_DIR** — adresář, kde leží spuštěná binárka. Detekováno přes
 *   @c GetModuleFileName na Windows a @c /proc/self/exe na Linuxu. V něm
 *   aplikace hledá read-only assety (@c ui_resources/, @c locale/, @c certs/).
 *
 * - **CFG_DIR** — adresář, kam se ukládá user config (@c <app>.ini, ImGui
 *   layout, breakpoints, persistent ramdisk, ...). Default je @c HOME_DIR
 *   (pocket / portable use case — single instalace, side-by-side s exe).
 *   Lze přepnout přes CLI nebo programatically.
 *
 * - **WORK_DIR** — výchozí adresář pro file dialogy. Default je CWD při
 *   spuštění. IGFD (file dialog) si pak last-path pamatuje per-dialog.
 *
 * Všechny adresáře lze přepsat CLI options:
 *
 * - @c --home-dir <dirpath> — override binary dir detection
 * - @c --cfg-dir  <dirpath> — override config dir
 * - @c --work-dir <dirpath> — override CWD pro file dialogy
 *
 * @par Lifecycle:
 * @c sdlapp_paths_new() detekuje paths a aplikuje CLI overrides (přes
 * existující @c sdlapp_options framework — musí být tedy volán až po
 * @c sdlapp_options_init). Vrátí strukturu, kterou volající nakonec uvolní
 * přes @c sdlapp_paths_destroy().
 *
 * @par Licence:
 * GPLv3
 */

#ifndef SDLAPP_PATHS_H
#define SDLAPP_PATHS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Aplikační adresáře.
 *
 * Všechna pole jsou newly-allocated (g_strdup) absolute paths bez koncového
 * separátoru. Po vytvoření jsou read-only pro konzumenty.
 */
typedef struct SdlAppPaths {
    char *home_dir;   /**< Adresář s binárkou (read-only assety). */
    char *cfg_dir;    /**< Adresář pro user config (default = home_dir). */
    char *work_dir;   /**< CWD při spuštění (default file dialogu). */
} SdlAppPaths;

/**
 * @brief Detekuje binary dir, inicializuje paths, aplikuje CLI overrides.
 *
 * Předpoklad: @c sdlapp_options_init() už byl zavolaný. CLI options
 * @c --home-dir / @c --cfg-dir / @c --work-dir se aplikují na default
 * hodnoty (binary dir / home_dir / CWD).
 *
 * Pokud detekce binary dir selže (vzácné — např. /proc nedostupný), použije
 * se CWD jako fallback s warning na stderr.
 *
 * @return Newly-allocated struct (nikdy NULL — při selhání alokace abort).
 */
SdlAppPaths *sdlapp_paths_new(void);

/**
 * @brief Uvolní paths a všechny její řetězce. Bezpečné s NULL.
 *
 * @param paths Pointer na strukturu (může být NULL).
 */
void sdlapp_paths_destroy(SdlAppPaths *paths);

/**
 * @brief Sestaví absolutní cestu z home_dir + relativní cesta.
 *
 * Pokud @p rel je absolutní (např. CLI předal @c "C:/explicit/path"),
 * vrátí jen jeho kopii bez prefixu home_dir.
 *
 * @param paths Pointer na inicializované paths (nesmí být NULL).
 * @param rel   Relativní cesta vůči home_dir (např. @c "ui_resources/font.ttf").
 *              Smí být @c NULL — pak se vrátí kopie samotného home_dir.
 * @return Newly-allocated string. Volající uvolní přes @c g_free().
 */
char *sdlapp_paths_resolve_home(const SdlAppPaths *paths, const char *rel);

/**
 * @brief Sestaví absolutní cestu z cfg_dir + relativní cesta.
 *
 * @param paths Pointer na inicializované paths (nesmí být NULL).
 * @param rel   Relativní cesta vůči cfg_dir.
 * @return Newly-allocated string. Volající uvolní přes @c g_free().
 */
char *sdlapp_paths_resolve_cfg(const SdlAppPaths *paths, const char *rel);

/**
 * @brief Sestaví absolutní cestu z work_dir + relativní cesta.
 *
 * @param paths Pointer na inicializované paths (nesmí být NULL).
 * @param rel   Relativní cesta vůči work_dir.
 * @return Newly-allocated string. Volající uvolní přes @c g_free().
 */
char *sdlapp_paths_resolve_work(const SdlAppPaths *paths, const char *rel);

#ifdef __cplusplus
}
#endif

#endif /* SDLAPP_PATHS_H */
