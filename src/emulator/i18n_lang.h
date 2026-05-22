/**
 * @file i18n_lang.h
 * @brief Volba jazyka — konfigurace a runtime přepínání
 */

#ifndef I18N_LANG_H
#define I18N_LANG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Podporované jazyky */
typedef enum {
    I18N_LANG_AUTO = 0, /* systémový jazyk */
    I18N_LANG_EN,       /* English */
    I18N_LANG_CS,       /* Čeština */
    I18N_LANG_SK,       /* Slovenčina */
    I18N_LANG_JA,       /* 日本語 */
    I18N_LANG_DE,       /* Deutsch */
    I18N_LANG_IT,       /* Italiano */
    I18N_LANG_ES,       /* Español */
    I18N_LANG_NL,       /* Nederlands */
    I18N_LANG_FR,       /* Français */
    I18N_LANG_PL,       /* Polski */
    I18N_LANG_UK,       /* Українська */
    I18N_LANG_COUNT
} en_I18N_LANG;

/* Informace o jazyce */
typedef struct {
    en_I18N_LANG id;
    const char *code;        /* "cs", "de", "ja", ... */
    const char *native_name; /* "Čeština", "Deutsch", ... */
    const char *keyword;     /* pro INI: "CZECH", "GERMAN", ... */
    const char *flag_image;  /* název obrázku vlajky: "flag-cz", "flag-de", ... (NULL = žádná) */
} st_I18N_LANG_INFO;

/* Globální nastavení jazyka */
typedef struct {
    en_I18N_LANG language;   /* aktuálně zvolený jazyk */
} st_I18N_LANG_SETTINGS;

extern st_I18N_LANG_SETTINGS g_i18n_lang_settings;
extern const st_I18N_LANG_INFO g_i18n_lang_info[];

/* Inicializace konfigurace jazyka — volat z cfgmain_init() */
extern void i18n_lang_config_init(void);

/* Aplikace zvoleného jazyka — volat po cfgmain_init() a i18n_init() */
extern void i18n_lang_apply(void);

/* Přepnutí jazyka za běhu */
extern void i18n_lang_set(en_I18N_LANG lang);

/* Počet jazyků (bez AUTO) */
#define I18N_LANG_REAL_COUNT (I18N_LANG_COUNT - 1)

#ifdef __cplusplus
}
#endif

#endif /* I18N_LANG_H */
