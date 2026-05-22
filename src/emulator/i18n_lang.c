/**
 * @file i18n_lang.c
 * @brief Volba jazyka — konfigurace a runtime přepínání
 */

#include "i18n_lang.h"
#include "cfgmain.h"
#include "i18n.h"
#include "libs/cfgfile/cfgroot.h"
#include "libs/cfgfile/cfgmodule.h"
#include "libs/cfgfile/cfgelement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Globální nastavení - default English (fresh install). Drive byl AUTO
 * (= systémový jazyk), ale pro jednotné UX všech uživatelů je lepší
 * pevný anglický default. User si může změnit v Interface menu. */
st_I18N_LANG_SETTINGS g_i18n_lang_settings = {
    .language = I18N_LANG_EN
};

/* Tabulka jazyků — nativní názvy jako UTF-8 escape sekvence */
const st_I18N_LANG_INFO g_i18n_lang_info[] = {
    { I18N_LANG_AUTO, "auto", "System default",  "AUTO",      "flag-globe" },
    { I18N_LANG_EN,   "en",   "English",         "ENGLISH",   "flag-gb" },
    { I18N_LANG_CS,   "cs",   "\xc4\x8c" "e" "\xc5\xa1" "tina",    "CZECH",    "flag-cz" },
    { I18N_LANG_SK,   "sk",   "Sloven" "\xc4\x8d" "ina",           "SLOVAK",   "flag-sk" },
    { I18N_LANG_JA,   "ja",   "\xe6\x97\xa5" "\xe6\x9c\xac" "\xe8\xaa\x9e", "JAPANESE", "flag-jp" },
    { I18N_LANG_DE,   "de",   "Deutsch",         "GERMAN",    "flag-de" },
    { I18N_LANG_IT,   "it",   "Italiano",        "ITALIAN",   "flag-it" },
    { I18N_LANG_ES,   "es",   "Espa" "\xc3\xb1" "ol",              "SPANISH",  "flag-es" },
    { I18N_LANG_NL,   "nl",   "Nederlands",      "DUTCH",     "flag-nl" },
    { I18N_LANG_FR,   "fr",   "Fran" "\xc3\xa7" "ais",             "FRENCH",   "flag-fr" },
    { I18N_LANG_PL,   "pl",   "Polski",          "POLISH",    "flag-pl" },
    { I18N_LANG_UK,   "uk",   "\xd0\xa3" "\xd0\xba" "\xd1\x80" "\xd0\xb0" "\xd1\x97" "\xd0\xbd" "\xd1\x81" "\xd1\x8c" "\xd0\xba" "\xd0\xb0", "UKRAINIAN", "flag-ua" },
};

/* ========================================================================= */
/*                       Konfigurace (INI)                                   */
/* ========================================================================= */

static void propagatecfg_language(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    g_i18n_lang_settings.language = (en_I18N_LANG)cfgelement_get_keyword_value(elm);
}

static void savecfg_language(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_keyword_value(elm, (int)g_i18n_lang_settings.language);
}

void i18n_lang_config_init(void)
{
    CFGMOD *cmod = cfgroot_register_new_module(g_cfgmain, "LANGUAGE");

    CFGELM *elm;
    elm = cfgmodule_register_new_element(cmod, "language", CFGENTYPE_KEYWORD, I18N_LANG_EN,
                                         I18N_LANG_AUTO, "AUTO",
                                         I18N_LANG_EN, "ENGLISH",
                                         I18N_LANG_CS, "CZECH",
                                         I18N_LANG_SK, "SLOVAK",
                                         I18N_LANG_JA, "JAPANESE",
                                         I18N_LANG_DE, "GERMAN",
                                         I18N_LANG_IT, "ITALIAN",
                                         I18N_LANG_ES, "SPANISH",
                                         I18N_LANG_NL, "DUTCH",
                                         I18N_LANG_FR, "FRENCH",
                                         I18N_LANG_PL, "POLISH",
                                         I18N_LANG_UK, "UKRAINIAN",
                                         -1);
    cfgelement_set_propagate_cb(elm, propagatecfg_language, NULL);
    cfgelement_set_save_cb(elm, savecfg_language, NULL);

    cfgmodule_parse(cmod);
    cfgmodule_propagate(cmod);
}

/* ========================================================================= */
/*                       Runtime přepínání jazyka                            */
/* ========================================================================= */

void i18n_lang_apply(void)
{
    i18n_lang_set(g_i18n_lang_settings.language);
}

void i18n_lang_set(en_I18N_LANG lang)
{
    g_i18n_lang_settings.language = lang;

    if (lang == I18N_LANG_AUTO || lang == I18N_LANG_EN)
    {
        /* AUTO: systémové locale, EN: angličtina (zdrojový jazyk = žádný překlad) */
#ifdef _WIN32
        /* Na Windows nastavíme LANGUAGE="" pro systémové výchozí,
         * nebo "en" pro vynucení angličtiny */
        if (lang == I18N_LANG_EN)
        {
            putenv("LANGUAGE=en");
        }
        else
        {
            putenv("LANGUAGE=");
        }
#else
        if (lang == I18N_LANG_EN)
        {
            setenv("LANGUAGE", "en", 1);
        }
        else
        {
            unsetenv("LANGUAGE");
        }
#endif
    }
    else
    {
        /* Nastavení přes proměnnou prostředí LANGUAGE — gettext ji respektuje
         * na všech platformách bez nutnosti měnit systémové locale */
        const char *code = g_i18n_lang_info[lang].code;

#ifdef _WIN32
        /* Windows putenv("LANGUAGE=cs") */
        char envstr[64];
        snprintf(envstr, sizeof(envstr), "LANGUAGE=%s", code);
        putenv(envstr);
#else
        setenv("LANGUAGE", code, 1);
#endif
    }

    /* Reinicializace gettext — nutné pro nový jazyk */
    setlocale(LC_ALL, "");

    /* Vynutit znovunačtení překladů */
    {
        extern int _nl_msg_cat_cntr;
        ++_nl_msg_cat_cntr;
    }
}
