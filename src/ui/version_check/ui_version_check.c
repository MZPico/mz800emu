#include <string.h>
#include <strings.h>
#include <glib.h>

#include "version_check/version_check.h"

typedef struct st_UI_VERSION_CHECK_URL
{
    char *tag;
    char *url;
} st_UI_VERSION_CHECK_URL;

/* Mapování branch tagu na URL, kde uživatel najde novou verzi.
 * Po migraci projektu ze SourceForge na GitHub směřujeme uživatele
 * primárně na GitHub Releases. SourceForge zůstává jen jako archiv
 * starých 1.0.x release balíků. */
const st_UI_VERSION_CHECK_URL g_ui_verurl[] = {
    {VERSION_CHECK_MAJOR_BRANCH, "https://github.com/michalhucik/mz800emu/releases"},
    {"devel", "https://github.com/michalhucik/mz800emu/releases"},
    {"win-snapshot", "https://www.ordoz.com/mz800emu/snapshot/"},
    {"2.0-preview", "https://github.com/michalhucik/mz800emu/releases"},
    {NULL, NULL}};

const char *ui_version_check_get_url_by_tag(const char *tag)
{
    const char *ret = NULL;
    int i = 0;
    while (g_ui_verurl[i].tag)
    {
        if (0 == strcasecmp(VERSION_CHECK_MAJOR_BRANCH, g_ui_verurl[i].tag))
        {
            ret = g_ui_verurl[i].url;
        };
        if (0 == strcasecmp(tag, g_ui_verurl[i].tag))
        {
            return g_ui_verurl[i].url;
        };
        i++;
    };
    return ret;
}

char *ui_version_check_create_version_string(guint32 version)
{
    GString *str = g_string_sized_new(0);
    g_string_append_printf(str, "%d.%d.%d", (version >> 24) & 0xff, (version >> 16) & 0xff, (version >> 8) & 0xff);
    if (version & 0xff)
    {
        g_string_append_printf(str, ".%d", version & 0xff);
    };
    return g_string_free(str, FALSE);
}
