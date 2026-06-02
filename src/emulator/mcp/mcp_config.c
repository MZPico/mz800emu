/*
 * File:   mcp_config.c
 *
 * Implementace MCP konfigurace - registrace do cfgmain INI systému,
 * default hodnoty, validace, propagate/save callbacks (V0.B.4).
 *
 * Modul drží globální st_MCP_CONFIG g_mcp_config a registruje sekci
 * [MCP] v cfgmain s těmito klíči:
 *
 *   tcp_port        UNSIGNED  default 23800, range 1024-65535
 *   bind_address    KEYWORD   "127.0.0.1" (default) / "0.0.0.0"
 *   profile         KEYWORD   "wild" (default) / "confined" /
 *                             "sandboxed" / "observer"
 *   auto_start_tcp  BOOL      default 0
 *
 * Použití CFGENTYPE_KEYWORD pro bind_address a profile dává nám
 * automatickou validaci (neznámá hodnota -> fallback na default) a
 * čitelný INI výstup.
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

#include "mcp_config.h"

#ifdef MZ800EMU_CFG_MCP_TCP_ENABLED

#include <string.h>
#include <stdio.h>
#include <glib.h>

#include "cfgmain.h"
#include "libs/cfgfile/cfgroot.h"
#include "libs/cfgfile/cfgmodule.h"
#include "libs/cfgfile/cfgelement.h"
#include "tcp_server.h"

/* ====================================================================== */
/* Globální stav                                                          */
/* ====================================================================== */

/**
 * @brief Globální konfigurace MCP. Default hodnoty před INI propagate.
 *
 * Při startu cfgmain_init zavolá mcp_config_init, který nastaví defaults
 * (přes register_new_element default values) a registruje propagate
 * callbacks. Po cfgmodule_parse + cfgroot_propagate jsou hodnoty
 * z INI; pokud sekce [MCP] neexistuje, defaults zůstanou.
 */
st_MCP_CONFIG g_mcp_config = {
    .tcp_port         = MCP_TCP_SERVER_DEFAULT_PORT,
    .bind_addr        = MCP_BIND_LOCALHOST,
    .profile          = MCP_PROFILE_WILD,
    .auto_start_tcp   = false,
    .action_log_path  = NULL,

    /* Python wrapper log defaults - log vypnutý.
     * `wrapper_log_path` / `wrapper_log_rotate_when` zůstávají NULL;
     * Python wrapper si dosadí "" -> default. */
    .wrapper_log_level        = MCP_WRAPPER_LOG_LEVEL_OFF,
    .wrapper_log_path         = NULL,
    .wrapper_log_rotate_kind  = MCP_WRAPPER_LOG_ROTATE_NONE,
    .wrapper_log_rotate_size_mb = 10,
    .wrapper_log_rotate_when  = NULL,
    .wrapper_log_rotate_keep  = 5,
};


/* ====================================================================== */
/* Helpery - string konverze                                              */
/* ====================================================================== */

const char *mcp_config_bind_addr_str(en_MCP_BIND_ADDR ba)
{
    switch (ba)
    {
        case MCP_BIND_ALL_INTERFACES: return "0.0.0.0";
        case MCP_BIND_LOCALHOST:      /* fallthrough */
        default:                      return "127.0.0.1";
    }
}


const char *mcp_config_profile_str(en_MCP_PROFILE p)
{
    switch (p)
    {
        case MCP_PROFILE_CONFINED:  return "confined";
        case MCP_PROFILE_SANDBOXED: return "sandboxed";
        case MCP_PROFILE_OBSERVER:  return "observer";
        case MCP_PROFILE_WILD:      /* fallthrough */
        default:                    return "wild";
    }
}


en_MCP_PROFILE mcp_config_profile_from_str(const char *s)
{
    if (!s) return MCP_PROFILE_WILD;
    if (g_ascii_strcasecmp(s, "confined")  == 0) return MCP_PROFILE_CONFINED;
    if (g_ascii_strcasecmp(s, "sandboxed") == 0) return MCP_PROFILE_SANDBOXED;
    if (g_ascii_strcasecmp(s, "observer")  == 0) return MCP_PROFILE_OBSERVER;
    return MCP_PROFILE_WILD;
}


en_MCP_BIND_ADDR mcp_config_bind_addr_from_str(const char *s)
{
    if (!s) return MCP_BIND_LOCALHOST;
    if (g_ascii_strcasecmp(s, "0.0.0.0") == 0) return MCP_BIND_ALL_INTERFACES;
    if (g_ascii_strcasecmp(s, "*")       == 0) return MCP_BIND_ALL_INTERFACES;
    if (g_ascii_strcasecmp(s, "127.0.0.1") == 0) return MCP_BIND_LOCALHOST;
    if (g_ascii_strcasecmp(s, "localhost") == 0) return MCP_BIND_LOCALHOST;
    return MCP_BIND_LOCALHOST;
}


const char *mcp_config_wrapper_log_level_str(en_MCP_WRAPPER_LOG_LEVEL l)
{
    switch (l)
    {
        case MCP_WRAPPER_LOG_LEVEL_ERROR:   return "error";
        case MCP_WRAPPER_LOG_LEVEL_WARNING: return "warning";
        case MCP_WRAPPER_LOG_LEVEL_INFO:    return "info";
        case MCP_WRAPPER_LOG_LEVEL_DEBUG:   return "debug";
        case MCP_WRAPPER_LOG_LEVEL_OFF:     /* fallthrough */
        default:                            return "off";
    }
}


const char *mcp_config_wrapper_log_rotate_kind_str(en_MCP_WRAPPER_LOG_ROTATE_KIND k)
{
    switch (k)
    {
        case MCP_WRAPPER_LOG_ROTATE_SIZE: return "size";
        case MCP_WRAPPER_LOG_ROTATE_TIME: return "time";
        case MCP_WRAPPER_LOG_ROTATE_NONE: /* fallthrough */
        default:                          return "none";
    }
}


/* ====================================================================== */
/* Propagate callbacky (INI -> g_mcp_config)                              */
/* ====================================================================== */

static void propagatecfg_tcp_port(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    unsigned v = cfgelement_get_unsigned_value(elm);
    /* Min/max validace už proběhla v cfgelement parseru (CFGENTYPE_UNSIGNED
     * 1024..65535). Pro jistotu pojistka. */
    if (v < 1024 || v > 65535) v = MCP_TCP_SERVER_DEFAULT_PORT;
    g_mcp_config.tcp_port = (int)v;
}


static void propagatecfg_bind_address(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    int kw = cfgelement_get_keyword_value(elm);
    if (kw < 0 || kw >= MCP_BIND__COUNT) kw = MCP_BIND_LOCALHOST;
    g_mcp_config.bind_addr = (en_MCP_BIND_ADDR)kw;
}


static void propagatecfg_profile(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    int kw = cfgelement_get_keyword_value(elm);
    if (kw < 0 || kw >= MCP_PROFILE__COUNT) kw = MCP_PROFILE_WILD;
    g_mcp_config.profile = (en_MCP_PROFILE)kw;
}


static void propagatecfg_auto_start_tcp(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    g_mcp_config.auto_start_tcp = cfgelement_get_bool_value(elm) ? true : false;
}


/**
 * @brief V1.D.2.C - propagate INI [MCP] action_log_path -> g_mcp_config.
 *
 * Prázdný řetězec se interpretuje jako disabled (= NULL v
 * g_mcp_config.action_log_path). Předchozí hodnota se uvolní přes
 * `g_free`, nová se naklonuje přes `g_strdup`.
 */
static void propagatecfg_action_log_path(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    char *txt = cfgelement_get_text_value(elm);
    g_free(g_mcp_config.action_log_path);
    if (txt && txt[0]) {
        g_mcp_config.action_log_path = g_strdup(txt);
    } else {
        g_mcp_config.action_log_path = NULL;
    }
}


/**
 * @brief Propagate INI [MCP] wrapper_log_level -> g_mcp_config.
 *
 * KEYWORD validace v cfgelement parseru zaručuje rozsah 0..__COUNT-1;
 * dodatečná pojistka na fallback OFF.
 */
static void propagatecfg_wrapper_log_level(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    int kw = cfgelement_get_keyword_value(elm);
    if (kw < 0 || kw >= MCP_WRAPPER_LOG_LEVEL__COUNT) {
        kw = MCP_WRAPPER_LOG_LEVEL_OFF;
    }
    g_mcp_config.wrapper_log_level = (en_MCP_WRAPPER_LOG_LEVEL)kw;
}


/**
 * @brief Propagate INI [MCP] wrapper_log_path -> g_mcp_config.
 *
 * Stejná sémantika jako `propagatecfg_action_log_path`: empty string
 * -> NULL v g_mcp_config (wrapper interpretuje jako "use default").
 */
static void propagatecfg_wrapper_log_path(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    char *txt = cfgelement_get_text_value(elm);
    g_free(g_mcp_config.wrapper_log_path);
    if (txt && txt[0]) {
        g_mcp_config.wrapper_log_path = g_strdup(txt);
    } else {
        g_mcp_config.wrapper_log_path = NULL;
    }
}


/**
 * @brief Propagate INI [MCP] wrapper_log_rotate_kind -> g_mcp_config.
 */
static void propagatecfg_wrapper_log_rotate_kind(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    int kw = cfgelement_get_keyword_value(elm);
    if (kw < 0 || kw >= MCP_WRAPPER_LOG_ROTATE_KIND__COUNT) {
        kw = MCP_WRAPPER_LOG_ROTATE_NONE;
    }
    g_mcp_config.wrapper_log_rotate_kind = (en_MCP_WRAPPER_LOG_ROTATE_KIND)kw;
}


/**
 * @brief Propagate INI [MCP] wrapper_log_rotate_size_mb -> g_mcp_config.
 *
 * Rozsah 1..10240 (= 10 GiB strop, dál nemá smysl pro log).
 */
static void propagatecfg_wrapper_log_rotate_size_mb(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    unsigned v = cfgelement_get_unsigned_value(elm);
    if (v < 1 || v > 10240) v = 10;
    g_mcp_config.wrapper_log_rotate_size_mb = (int)v;
}


/**
 * @brief Propagate INI [MCP] wrapper_log_rotate_when -> g_mcp_config.
 *
 * Tolerantně přijme libovolný neprázdný string; validaci dělá Python
 * wrapper proti `TimedRotatingFileHandler`. Empty -> NULL.
 */
static void propagatecfg_wrapper_log_rotate_when(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    char *txt = cfgelement_get_text_value(elm);
    g_free(g_mcp_config.wrapper_log_rotate_when);
    if (txt && txt[0]) {
        g_mcp_config.wrapper_log_rotate_when = g_strdup(txt);
    } else {
        g_mcp_config.wrapper_log_rotate_when = NULL;
    }
}


/**
 * @brief Propagate INI [MCP] wrapper_log_rotate_keep -> g_mcp_config.
 *
 * Rozsah 0..1000 (= horní limit pro rozumný backupCount).
 */
static void propagatecfg_wrapper_log_rotate_keep(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    unsigned v = cfgelement_get_unsigned_value(elm);
    if (v > 1000) v = 1000;
    g_mcp_config.wrapper_log_rotate_keep = (int)v;
}


/* ====================================================================== */
/* Save callbacky (g_mcp_config -> INI element)                           */
/* ====================================================================== */

static void savecfg_tcp_port(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_unsigned_value(elm, (unsigned)g_mcp_config.tcp_port);
}


static void savecfg_bind_address(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_keyword_value(elm, (int)g_mcp_config.bind_addr);
}


static void savecfg_profile(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_keyword_value(elm, (int)g_mcp_config.profile);
}


static void savecfg_auto_start_tcp(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_bool_value(elm, g_mcp_config.auto_start_tcp ? 1 : 0);
}


/**
 * @brief V1.D.2.C - save g_mcp_config.action_log_path -> INI element.
 *
 * NULL nebo prázdná hodnota se uloží jako prázdný řetězec (= disabled).
 */
static void savecfg_action_log_path(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_text_value(elm,
        g_mcp_config.action_log_path ? g_mcp_config.action_log_path : "");
}


/**
 * @brief Save g_mcp_config.wrapper_log_level -> INI element (KEYWORD).
 */
static void savecfg_wrapper_log_level(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_keyword_value(elm, (int)g_mcp_config.wrapper_log_level);
}


/**
 * @brief Save g_mcp_config.wrapper_log_path -> INI element (TEXT).
 *
 * NULL se uloží jako empty string (= "wrapper použij default").
 */
static void savecfg_wrapper_log_path(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_text_value(elm,
        g_mcp_config.wrapper_log_path ? g_mcp_config.wrapper_log_path : "");
}


/**
 * @brief Save g_mcp_config.wrapper_log_rotate_kind -> INI element (KEYWORD).
 */
static void savecfg_wrapper_log_rotate_kind(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_keyword_value(elm,
        (int)g_mcp_config.wrapper_log_rotate_kind);
}


/**
 * @brief Save g_mcp_config.wrapper_log_rotate_size_mb -> INI element (UNSIGNED).
 */
static void savecfg_wrapper_log_rotate_size_mb(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_unsigned_value(elm,
        (unsigned)g_mcp_config.wrapper_log_rotate_size_mb);
}


/**
 * @brief Save g_mcp_config.wrapper_log_rotate_when -> INI element (TEXT).
 */
static void savecfg_wrapper_log_rotate_when(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_text_value(elm,
        g_mcp_config.wrapper_log_rotate_when ? g_mcp_config.wrapper_log_rotate_when : "midnight");
}


/**
 * @brief Save g_mcp_config.wrapper_log_rotate_keep -> INI element (UNSIGNED).
 */
static void savecfg_wrapper_log_rotate_keep(void *e, void *data)
{
    (void)data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *)e;
    cfgelement_set_unsigned_value(elm,
        (unsigned)g_mcp_config.wrapper_log_rotate_keep);
}


/* ====================================================================== */
/* Init                                                                   */
/* ====================================================================== */

void mcp_config_init(void)
{
    CFGMOD *cmod = cfgroot_register_new_module(g_cfgmain, "MCP");

    CFGELM *elm;

    /* tcp_port: UNSIGNED s validačním rozsahem 1024..65535. */
    elm = cfgmodule_register_new_element(
        cmod, "tcp_port", CFGENTYPE_UNSIGNED,
        (int)MCP_TCP_SERVER_DEFAULT_PORT, 1024, 65535);
    cfgelement_set_propagate_cb(elm, propagatecfg_tcp_port, NULL);
    cfgelement_set_save_cb(elm, savecfg_tcp_port, NULL);

    /* bind_address: KEYWORD - 2 hodnoty. Default LOCALHOST = 0.
     * Variadic: default, pak páry (int, char*) ukončené -1. */
    elm = cfgmodule_register_new_element(
        cmod, "bind_address", CFGENTYPE_KEYWORD,
        (int)MCP_BIND_LOCALHOST,
        (int)MCP_BIND_LOCALHOST,      "127.0.0.1",
        (int)MCP_BIND_ALL_INTERFACES, "0.0.0.0",
        -1);
    cfgelement_set_propagate_cb(elm, propagatecfg_bind_address, NULL);
    cfgelement_set_save_cb(elm, savecfg_bind_address, NULL);

    /* profile: KEYWORD - 4 hodnoty. Default WILD = 0. */
    elm = cfgmodule_register_new_element(
        cmod, "profile", CFGENTYPE_KEYWORD,
        (int)MCP_PROFILE_WILD,
        (int)MCP_PROFILE_WILD,      "wild",
        (int)MCP_PROFILE_CONFINED,  "confined",
        (int)MCP_PROFILE_SANDBOXED, "sandboxed",
        (int)MCP_PROFILE_OBSERVER,  "observer",
        -1);
    cfgelement_set_propagate_cb(elm, propagatecfg_profile, NULL);
    cfgelement_set_save_cb(elm, savecfg_profile, NULL);

    /* auto_start_tcp: BOOL default 0 (false). */
    elm = cfgmodule_register_new_element(
        cmod, "auto_start_tcp", CFGENTYPE_BOOL, 0);
    cfgelement_set_propagate_cb(elm, propagatecfg_auto_start_tcp, NULL);
    cfgelement_set_save_cb(elm, savecfg_auto_start_tcp, NULL);

    /* V1.D.2.C - action_log_path: TEXT default "" (= disabled). Pokud
     * uživatel v INI nastaví non-empty cestu, Activity log appenduje
     * každý zaznamenaný entry do souboru (best-effort, ignore write
     * errors). */
    elm = cfgmodule_register_new_element(
        cmod, "action_log_path", CFGENTYPE_TEXT, "");
    cfgelement_set_propagate_cb(elm, propagatecfg_action_log_path, NULL);
    cfgelement_set_save_cb(elm, savecfg_action_log_path, NULL);

    /* Python wrapper log konfigurace - 6 klíčů. C kód jen perzistuje;
     * vlastní application čte `mcp-server/mcp_server.py` při svém startu.
     *
     * wrapper_log_level: KEYWORD 5 hodnot, default OFF. */
    elm = cfgmodule_register_new_element(
        cmod, "wrapper_log_level", CFGENTYPE_KEYWORD,
        (int)MCP_WRAPPER_LOG_LEVEL_OFF,
        (int)MCP_WRAPPER_LOG_LEVEL_OFF,     "off",
        (int)MCP_WRAPPER_LOG_LEVEL_ERROR,   "error",
        (int)MCP_WRAPPER_LOG_LEVEL_WARNING, "warning",
        (int)MCP_WRAPPER_LOG_LEVEL_INFO,    "info",
        (int)MCP_WRAPPER_LOG_LEVEL_DEBUG,   "debug",
        -1);
    cfgelement_set_propagate_cb(elm, propagatecfg_wrapper_log_level, NULL);
    cfgelement_set_save_cb(elm, savecfg_wrapper_log_level, NULL);

    /* wrapper_log_path: TEXT default "" (= wrapper použij default cestu). */
    elm = cfgmodule_register_new_element(
        cmod, "wrapper_log_path", CFGENTYPE_TEXT, "");
    cfgelement_set_propagate_cb(elm, propagatecfg_wrapper_log_path, NULL);
    cfgelement_set_save_cb(elm, savecfg_wrapper_log_path, NULL);

    /* wrapper_log_rotate_kind: KEYWORD 3 hodnoty, default NONE. */
    elm = cfgmodule_register_new_element(
        cmod, "wrapper_log_rotate_kind", CFGENTYPE_KEYWORD,
        (int)MCP_WRAPPER_LOG_ROTATE_NONE,
        (int)MCP_WRAPPER_LOG_ROTATE_NONE, "none",
        (int)MCP_WRAPPER_LOG_ROTATE_SIZE, "size",
        (int)MCP_WRAPPER_LOG_ROTATE_TIME, "time",
        -1);
    cfgelement_set_propagate_cb(elm, propagatecfg_wrapper_log_rotate_kind, NULL);
    cfgelement_set_save_cb(elm, savecfg_wrapper_log_rotate_kind, NULL);

    /* wrapper_log_rotate_size_mb: UNSIGNED default 10, rozsah 1..10240. */
    elm = cfgmodule_register_new_element(
        cmod, "wrapper_log_rotate_size_mb", CFGENTYPE_UNSIGNED,
        10, 1, 10240);
    cfgelement_set_propagate_cb(elm, propagatecfg_wrapper_log_rotate_size_mb, NULL);
    cfgelement_set_save_cb(elm, savecfg_wrapper_log_rotate_size_mb, NULL);

    /* wrapper_log_rotate_when: TEXT default "midnight". */
    elm = cfgmodule_register_new_element(
        cmod, "wrapper_log_rotate_when", CFGENTYPE_TEXT, "midnight");
    cfgelement_set_propagate_cb(elm, propagatecfg_wrapper_log_rotate_when, NULL);
    cfgelement_set_save_cb(elm, savecfg_wrapper_log_rotate_when, NULL);

    /* wrapper_log_rotate_keep: UNSIGNED default 5, rozsah 0..1000. */
    elm = cfgmodule_register_new_element(
        cmod, "wrapper_log_rotate_keep", CFGENTYPE_UNSIGNED,
        5, 0, 1000);
    cfgelement_set_propagate_cb(elm, propagatecfg_wrapper_log_rotate_keep, NULL);
    cfgelement_set_save_cb(elm, savecfg_wrapper_log_rotate_keep, NULL);

    /* Načti hodnoty z INI souboru a propaguj je do g_mcp_config.
     * Per-module parse + propagate je v mz800new běžný pattern
     * (viz i18n_lang_config_init); cfgroot_propagate samotný se
     * v projektu globálně nevolá. Pokud INI sekce [MCP] neexistuje,
     * elementy zůstanou na default hodnotách a propagate je
     * deterministicky aplikuje. */
    cfgmodule_parse(cmod);
    cfgmodule_propagate(cmod);
}


const char *mcp_config_get_action_log_path(void)
{
    return g_mcp_config.action_log_path;
}

#endif /* MZ800EMU_CFG_MCP_TCP_ENABLED */
