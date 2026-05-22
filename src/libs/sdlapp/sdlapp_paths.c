/**
 * @file   sdlapp_paths.c
 * @brief  Implementace HOME / CFG / WORK directory pro SDL3 aplikace.
 *
 * Detekce binary dir:
 * - Windows: @c GetModuleFileNameW (UTF-16, převod na UTF-8 přes glib)
 * - Linux:   @c /proc/self/exe symlink přes @c readlink
 *
 * Path resolution používá @c g_path_is_absolute pro detekci absolutních
 * cest (cross-platform), výsledek sestaví přes @c g_build_filename.
 *
 * @par Licence:
 * GPLv3
 */

#include "sdlapp_paths.h"
#include "sdlapp_options.h"

#include <glib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

/* ----------------------------------------------------------------------------
 * Detekce cesty k běžící binárce.
 * Vrací newly-allocated absolute path (g_free), nebo NULL při selhání.
 * ---------------------------------------------------------------------------- */
#ifdef _WIN32
static char *detect_executable_path_win32(void)
{
    /* Windows má cesty potenciálně delší než MAX_PATH s long path support.
     * Začneme s rozumnou velikostí a v případě truncation rozšíříme. */
    DWORD bufsize = 1024;
    wchar_t *wbuf = NULL;
    DWORD len;

    while (1) {
        wbuf = g_malloc(bufsize * sizeof(wchar_t));
        len = GetModuleFileNameW(NULL, wbuf, bufsize);
        if (len == 0) {
            /* Selhání API */
            g_free(wbuf);
            return NULL;
        }
        if (len < bufsize - 1) {
            /* Plná cesta načtená */
            break;
        }
        /* Truncated — zvětšit buffer a zkusit znovu */
        g_free(wbuf);
        bufsize *= 2;
        if (bufsize > 32768) {
            /* Pojistka — Windows MAX path s long path support je 32767 */
            return NULL;
        }
    }

    /* Konverze UTF-16 → UTF-8 */
    GError *err = NULL;
    char *utf8 = g_utf16_to_utf8((const gunichar2 *)wbuf, len, NULL, NULL, &err);
    g_free(wbuf);
    if (!utf8) {
        if (err) {
            g_warning("sdlapp_paths: g_utf16_to_utf8 failed: %s", err->message);
            g_error_free(err);
        }
        return NULL;
    }
    return utf8;
}
#else
static char *detect_executable_path_linux(void)
{
    /* /proc/self/exe je symlink na běžící binárku. readlink vrací počet
     * zapsaných bajtů bez NUL terminator. */
    char buf[PATH_MAX + 1];
    ssize_t len = readlink("/proc/self/exe", buf, PATH_MAX);
    if (len <= 0) {
        return NULL;
    }
    buf[len] = '\0';
    return g_strdup(buf);
}
#endif

/**
 * @brief Detekuje adresář, kde leží spuštěná binárka.
 *
 * Při selhání detekce vrátí @c NULL — volající má fallback na CWD.
 */
static char *detect_home_dir(void)
{
#ifdef _WIN32
    char *exe_path = detect_executable_path_win32();
#else
    char *exe_path = detect_executable_path_linux();
#endif
    if (!exe_path) {
        return NULL;
    }

    /* Adresář bez koncového separátoru */
    char *dir = g_path_get_dirname(exe_path);
    g_free(exe_path);
    return dir;
}

/* ----------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------- */

SdlAppPaths *sdlapp_paths_new(void)
{
    SdlAppPaths *paths = g_malloc0(sizeof(SdlAppPaths));

    /* HOME_DIR: detekce binárky, fallback CWD při selhání */
    paths->home_dir = detect_home_dir();
    if (!paths->home_dir) {
        paths->home_dir = g_get_current_dir();
        g_warning("sdlapp_paths: binary dir detection failed, fallback to CWD: %s",
                  paths->home_dir);
    }

    /* CFG_DIR: default = HOME_DIR */
    paths->cfg_dir = g_strdup(paths->home_dir);

    /* WORK_DIR: CWD při spuštění */
    paths->work_dir = g_get_current_dir();

    /* CLI overrides — sdlapp_option_value vrací NULL, pokud option nebyl předán.
     * Pokud je předaná hodnota relativní, ponecháme ji jak je (resolve_*
     * funkce ji pak přiloží k base — ale CLI override by neměla být relativní).
     * Pro jistotu logujeme warning u relativní cesty. */
    const char *opt_home = sdlapp_option_value("--home-dir");
    if (opt_home) {
        if (!g_path_is_absolute(opt_home)) {
            g_warning("sdlapp_paths: --home-dir is not an absolute path: %s", opt_home);
        }
        g_free(paths->home_dir);
        paths->home_dir = g_strdup(opt_home);
        /* CFG_DIR sleduje home_dir, dokud ho nepřepneme samostatně */
        g_free(paths->cfg_dir);
        paths->cfg_dir = g_strdup(opt_home);
    }

    const char *opt_cfg = sdlapp_option_value("--cfg-dir");
    if (opt_cfg) {
        if (!g_path_is_absolute(opt_cfg)) {
            g_warning("sdlapp_paths: --cfg-dir is not an absolute path: %s", opt_cfg);
        }
        g_free(paths->cfg_dir);
        paths->cfg_dir = g_strdup(opt_cfg);
    }

    const char *opt_work = sdlapp_option_value("--work-dir");
    if (opt_work) {
        if (!g_path_is_absolute(opt_work)) {
            g_warning("sdlapp_paths: --work-dir is not an absolute path: %s", opt_work);
        }
        g_free(paths->work_dir);
        paths->work_dir = g_strdup(opt_work);
    }

    return paths;
}

void sdlapp_paths_destroy(SdlAppPaths *paths)
{
    if (!paths) return;
    g_free(paths->home_dir);
    g_free(paths->cfg_dir);
    g_free(paths->work_dir);
    g_free(paths);
}

/* Společná pomocná funkce pro resolve_*. */
static char *resolve_against(const char *base, const char *rel)
{
    if (!rel) {
        return g_strdup(base);
    }
    if (g_path_is_absolute(rel)) {
        return g_strdup(rel);
    }
    return g_build_filename(base, rel, NULL);
}

char *sdlapp_paths_resolve_home(const SdlAppPaths *paths, const char *rel)
{
    g_return_val_if_fail(paths != NULL, NULL);
    return resolve_against(paths->home_dir, rel);
}

char *sdlapp_paths_resolve_cfg(const SdlAppPaths *paths, const char *rel)
{
    g_return_val_if_fail(paths != NULL, NULL);
    return resolve_against(paths->cfg_dir, rel);
}

char *sdlapp_paths_resolve_work(const SdlAppPaths *paths, const char *rel)
{
    g_return_val_if_fail(paths != NULL, NULL);
    return resolve_against(paths->work_dir, rel);
}
