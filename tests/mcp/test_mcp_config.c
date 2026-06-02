/**
 * @file   test_mcp_config.c
 * @brief  Unity testy pro MCP konfiguraci (V0.B.4).
 *
 * Pokrývá:
 *   - default hodnoty po mcp_config_init bez existujícího INI
 *   - load z existujícího INI s validními hodnotami
 *   - validace - port mimo rozsah / neznámý profile / neznámá bind = fallback
 *   - save -> reload roundtrip
 *   - pure helpery (string<->enum konverze)
 *
 * Test type = LIB - linkuje cfgmain + cfgfile + mcp_config přes emu core.
 *
 * @par Licence:
 * GPL-3.0-or-later. Viz licence header v mcp_config.c.
 */

#include "mztest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "emulator/cfgmain.h"
#include "emulator/mcp/mcp_config.h"
#include "libs/cfgfile/cfgroot.h"
#include "libs/cfgfile/cfgmodule.h"


/* Cesta k dočasnému INI souboru pro testy. Test framework headless
 * mode neinstanciuje g_sdlapp ani normální cfgmain_init flow, takže
 * si tu připravíme vlastní mini cfgroot pro round-trip. */
static const char *TMP_INI = "tests/data/tmp/test_mcp_config.ini";


void setUp(void)
{
    /* Reset g_mcp_config na defaults před každým testem. */
    g_mcp_config.tcp_port = 0;
    g_mcp_config.bind_addr = (en_MCP_BIND_ADDR)99; /* invalid */
    g_mcp_config.profile = (en_MCP_PROFILE)99;
    g_mcp_config.auto_start_tcp = true;
}


void tearDown(void)
{
    remove(TMP_INI);
}


/* ====================================================================== */
/* Pure helpery - string <-> enum konverze                                 */
/* ====================================================================== */

void test_bind_addr_str_roundtrip(void)
{
    TEST_ASSERT_EQUAL_STRING("127.0.0.1",
        mcp_config_bind_addr_str(MCP_BIND_LOCALHOST));
    TEST_ASSERT_EQUAL_STRING("0.0.0.0",
        mcp_config_bind_addr_str(MCP_BIND_ALL_INTERFACES));
}


void test_bind_addr_from_str(void)
{
    TEST_ASSERT_EQUAL_INT(MCP_BIND_LOCALHOST,
        mcp_config_bind_addr_from_str("127.0.0.1"));
    TEST_ASSERT_EQUAL_INT(MCP_BIND_LOCALHOST,
        mcp_config_bind_addr_from_str("localhost"));
    TEST_ASSERT_EQUAL_INT(MCP_BIND_ALL_INTERFACES,
        mcp_config_bind_addr_from_str("0.0.0.0"));
    TEST_ASSERT_EQUAL_INT(MCP_BIND_ALL_INTERFACES,
        mcp_config_bind_addr_from_str("*"));
    /* NULL a neznámé hodnoty -> default LOCALHOST. */
    TEST_ASSERT_EQUAL_INT(MCP_BIND_LOCALHOST,
        mcp_config_bind_addr_from_str(NULL));
    TEST_ASSERT_EQUAL_INT(MCP_BIND_LOCALHOST,
        mcp_config_bind_addr_from_str("garbage"));
    TEST_ASSERT_EQUAL_INT(MCP_BIND_LOCALHOST,
        mcp_config_bind_addr_from_str(""));
}


void test_profile_str_roundtrip(void)
{
    TEST_ASSERT_EQUAL_STRING("wild",      mcp_config_profile_str(MCP_PROFILE_WILD));
    TEST_ASSERT_EQUAL_STRING("confined",  mcp_config_profile_str(MCP_PROFILE_CONFINED));
    TEST_ASSERT_EQUAL_STRING("sandboxed", mcp_config_profile_str(MCP_PROFILE_SANDBOXED));
    TEST_ASSERT_EQUAL_STRING("observer",  mcp_config_profile_str(MCP_PROFILE_OBSERVER));
}


void test_profile_from_str(void)
{
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_WILD,
        mcp_config_profile_from_str("wild"));
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_CONFINED,
        mcp_config_profile_from_str("confined"));
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_SANDBOXED,
        mcp_config_profile_from_str("sandboxed"));
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_OBSERVER,
        mcp_config_profile_from_str("observer"));
    /* Case insensitive. */
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_CONFINED,
        mcp_config_profile_from_str("CONFINED"));
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_OBSERVER,
        mcp_config_profile_from_str("Observer"));
    /* NULL a neznámé -> default WILD. */
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_WILD,
        mcp_config_profile_from_str(NULL));
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_WILD,
        mcp_config_profile_from_str("hardcore"));
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_WILD,
        mcp_config_profile_from_str(""));
}


/* ====================================================================== */
/* Init - default hodnoty bez existujícího INI                            */
/* ====================================================================== */

void test_init_defaults_without_ini(void)
{
    /* Bez existujícího INI - cfgmodule_parse selže (fail-soft), elementy
     * zůstanou na default hodnotách registrovaných v cfgmodule_register_new_element,
     * propagate je aplikuje na g_mcp_config. */
    remove(TMP_INI);

    g_cfgmain = cfgroot_new(TMP_INI);
    mcp_config_init();

    TEST_ASSERT_EQUAL_INT(23800, g_mcp_config.tcp_port);
    TEST_ASSERT_EQUAL_INT(MCP_BIND_LOCALHOST, g_mcp_config.bind_addr);
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_WILD, g_mcp_config.profile);
    TEST_ASSERT_FALSE(g_mcp_config.auto_start_tcp);

    cfgroot_destroy(g_cfgmain);
    g_cfgmain = NULL;
}


/* ====================================================================== */
/* Load - INI s validními hodnotami                                       */
/* ====================================================================== */

void test_load_from_valid_ini(void)
{
    /* Vyrobíme tmp INI se sekcí [MCP] obsahující validní hodnoty
     * mimo defaults.
     *
     * POZOR: cfgfile parser číselných hodnot CFGENTYPE_UNSIGNED čte
     * VŽDY jako hex (s nebo bez prefixu "0x"). cfgelement_save zapisuje
     * jako "0x%02x". Pro INI vstup tedy musíme zadávat hex - 0x5FB4
     * = 24500 dec. */
    g_mkdir_with_parents("tests/data/tmp", 0755);
    FILE *fp = fopen(TMP_INI, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp,
        "\n"
        "[MCP]\n"
        "tcp_port = 0x5fb4\n"          /* 24500 dec */
        "bind_address = 0.0.0.0\n"
        "profile = confined\n"
        "auto_start_tcp = 1\n"
        "\n");
    fclose(fp);

    g_cfgmain = cfgroot_new(TMP_INI);
    mcp_config_init();

    TEST_ASSERT_EQUAL_INT(24500, g_mcp_config.tcp_port);
    TEST_ASSERT_EQUAL_INT(MCP_BIND_ALL_INTERFACES, g_mcp_config.bind_addr);
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_CONFINED, g_mcp_config.profile);
    TEST_ASSERT_TRUE(g_mcp_config.auto_start_tcp);

    cfgroot_destroy(g_cfgmain);
    g_cfgmain = NULL;
}


/* ====================================================================== */
/* Validace - port mimo rozsah                                            */
/* ====================================================================== */

void test_load_port_out_of_range_fallback(void)
{
    /* tcp_port = 0x50 (= 80 dec, privilegovaný) - mimo rozsah
     * 1024..65535, cfgelement validátor odmítne a element zůstane
     * na defaultu. */
    g_mkdir_with_parents("tests/data/tmp", 0755);
    FILE *fp = fopen(TMP_INI, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "\n[MCP]\ntcp_port = 0x50\n\n");   /* 80 dec, < 1024 */
    fclose(fp);

    g_cfgmain = cfgroot_new(TMP_INI);
    mcp_config_init();

    /* Default port = 23800 (cfgelement_set_unsigned_value vyhodnotí
     * 80 jako mimo rozsah a zachová default). */
    TEST_ASSERT_EQUAL_INT(23800, g_mcp_config.tcp_port);

    cfgroot_destroy(g_cfgmain);
    g_cfgmain = NULL;
}


/* ====================================================================== */
/* Validace - neznámé klíčové slovo                                       */
/* ====================================================================== */

void test_load_unknown_profile_fallback(void)
{
    /* profile = nonsense - cfgelement_keyword odmítne, propagate
     * callback dostane invalid keyword (typicky -1 nebo default).
     * V mcp_config.c propagatecfg_profile pak fallbackuje na WILD. */
    g_mkdir_with_parents("tests/data/tmp", 0755);
    FILE *fp = fopen(TMP_INI, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "\n[MCP]\nprofile = hardcore\n\n");
    fclose(fp);

    g_cfgmain = cfgroot_new(TMP_INI);
    mcp_config_init();

    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_WILD, g_mcp_config.profile);

    cfgroot_destroy(g_cfgmain);
    g_cfgmain = NULL;
}


/* ====================================================================== */
/* Save -> reload roundtrip                                               */
/* ====================================================================== */

void test_save_reload_roundtrip(void)
{
    /* 1. Init s default INI (neexistuje). */
    remove(TMP_INI);
    g_mkdir_with_parents("tests/data/tmp", 0755);

    g_cfgmain = cfgroot_new(TMP_INI);
    mcp_config_init();

    /* 2. Změň g_mcp_config a save. */
    g_mcp_config.tcp_port = 25000;
    g_mcp_config.bind_addr = MCP_BIND_ALL_INTERFACES;
    g_mcp_config.profile = MCP_PROFILE_OBSERVER;
    g_mcp_config.auto_start_tcp = true;

    cfgroot_save(g_cfgmain);
    cfgroot_destroy(g_cfgmain);
    g_cfgmain = NULL;

    /* 3. Reload do čerstvého cfgroot, ověř že hodnoty jsou tam. */
    /* Reset g_mcp_config na nesmysly, aby propagate musel projevit. */
    g_mcp_config.tcp_port = 1;
    g_mcp_config.bind_addr = MCP_BIND_LOCALHOST;
    g_mcp_config.profile = MCP_PROFILE_WILD;
    g_mcp_config.auto_start_tcp = false;

    g_cfgmain = cfgroot_new(TMP_INI);
    mcp_config_init();

    TEST_ASSERT_EQUAL_INT(25000, g_mcp_config.tcp_port);
    TEST_ASSERT_EQUAL_INT(MCP_BIND_ALL_INTERFACES, g_mcp_config.bind_addr);
    TEST_ASSERT_EQUAL_INT(MCP_PROFILE_OBSERVER, g_mcp_config.profile);
    TEST_ASSERT_TRUE(g_mcp_config.auto_start_tcp);

    cfgroot_destroy(g_cfgmain);
    g_cfgmain = NULL;
}


/* ====================================================================== */
/* Test runner                                                            */
/* ====================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Pure helpers */
    RUN_TEST(test_bind_addr_str_roundtrip);
    RUN_TEST(test_bind_addr_from_str);
    RUN_TEST(test_profile_str_roundtrip);
    RUN_TEST(test_profile_from_str);

    /* Init / load / validate / save */
    RUN_TEST(test_init_defaults_without_ini);
    RUN_TEST(test_load_from_valid_ini);
    RUN_TEST(test_load_port_out_of_range_fallback);
    RUN_TEST(test_load_unknown_profile_fallback);
    RUN_TEST(test_save_reload_roundtrip);

    return UNITY_END();
}
