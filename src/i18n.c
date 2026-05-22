/**
 * @file   i18n.c
 * @brief  Implementace i18n_init() — bind gettext domain proti home_dir/locale.
 *
 * Hledá locale/ relativně k @c g_sdlapp->paths->home_dir. V dist/ je
 * @c locale/ vedle binárky, ve vývoji v repo není a fallback na
 * @c src/locale/ kde @c make i18n-mo-auto generuje .mo soubory.
 *
 * Kompiluje se jako samostatný .c (na rozdíl od inline v i18n.h),
 * protože potřebuje glib a sdlapp_paths headers.
 */

#include "i18n.h"
#include <glib.h>
#include <sys/stat.h>
#include "libs/sdlapp/sdlapp.h"

extern SdlApp *g_sdlapp;

void i18n_init(void)
{
    setlocale(LC_ALL, "");

    char *localedir = sdlapp_paths_resolve_home(g_sdlapp->paths, "locale");
    struct stat st;
    if (stat(localedir, &st) != 0) {
        /* Vývoj v repo: HOME_DIR je repo root, .mo jsou v src/locale/ */
        char *fallback = sdlapp_paths_resolve_home(g_sdlapp->paths, "src/locale");
        if (stat(fallback, &st) == 0) {
            g_free(localedir);
            localedir = fallback;
        } else {
            g_free(fallback);
        }
    }
    bindtextdomain(I18N_TEXTDOMAIN, localedir);
    g_free(localedir);

    /* ImGui vyžaduje UTF-8; na Windows by gettext jinak vracel cp1250 */
    bind_textdomain_codeset(I18N_TEXTDOMAIN, "UTF-8");
    textdomain(I18N_TEXTDOMAIN);
}
