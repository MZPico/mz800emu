/*
 * test_unicard.c - unit testy pro Unicard / UNIMGR
 *
 * Pokrývá:
 *   - smoke: cmdRESET, cmdREV, cmdGETCWD
 *   - parser-equivalence: cmdOPEN s param_format "BS"
 *
 * Test komunikuje s unicard přes IORQ na portech 0x50 (CMD) a 0x51 (DATA),
 * stejně jako reálný Z80 kód. Pracovní SD root je vytvořen v dočasném
 * adresáři (g_dir_make_tmp) a po každém testu se rekurzivně smaže.
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "hw-generic/unicard/unicard.h"
#include "hw-generic/unicard/unicard_sfn.h"
#include "hw-generic/unicard/unimgr_commands.h"
#include "baseui/baseui_tools.h"
#include "emulator/mzarch/mzarch_platform.h"

/* Porty unicard (interní mapování unimgr_write_byte/unimgr_read_byte). */
#define UNICARD_PORT_CMD   0x50
#define UNICARD_PORT_DATA  0x51

/* Status master bity (1. bajt statusu). */
#define UNICARD_STS_BUSY      0x01  /* PARAMRQ - čekáme parametry */
#define UNICARD_STS_DOUTRQ    0x02  /* máme výstupní data */
#define UNICARD_STS_READDIR   0x04
#define UNICARD_STS_READ      0x08
#define UNICARD_STS_WRITE     0x10
#define UNICARD_STS_EOF       0x20
#define UNICARD_STS_ERROR     0x80

/* Jméno souboru Unicard Manageru na SD kartě je per-architektura + per-FW.
 * Default FW je uc3 -> per-mzarch MGR V2.11b. uc1 (= MZ-800 only)
 * instaluje MGR V2.4 (mgr.mzf). Musí ladit s unicard_init_sd dispatch. */
#if MZARCH == 800
    #define TEST_UNICARD_MGR_MZF      UNICARD_UNIMGR_MGR800_MZF
#elif MZARCH == 700
    #define TEST_UNICARD_MGR_MZF      UNICARD_UNIMGR_MGR700_MZF
#else
    #define TEST_UNICARD_MGR_MZF      UNICARD_UNIMGR_MGR1500_MZF
#endif
#define TEST_UNICARD_MGR_MZF_PATH     "/" UNICARD_UNIMGR_DIR "/" TEST_UNICARD_MGR_MZF

/* Per-test SD root (tmpdir vytvořený v setUp, smazaný v tearDown). */
static char *s_test_sd_root = NULL;


/* Rekurzivní smazání adresáře pomocí GLib.
 * Vrací 0 při úspěchu, -1 při chybě. */
static int rmtree(const char *path)
{
    if (!path || !g_file_test(path, G_FILE_TEST_EXISTS)) {
        return 0;
    }

    if (g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
        GError *err = NULL;
        GDir *d = g_dir_open(path, 0, &err);
        if (!d) {
            if (err) g_error_free(err);
            return -1;
        }
        const gchar *name;
        while ((name = g_dir_read_name(d)) != NULL) {
            char *child = g_build_filename(path, name, NULL);
            rmtree(child);
            g_free(child);
        }
        g_dir_close(d);
        return g_rmdir(path);
    }
    return g_remove(path);
}


void setUp(void)
{
    /* Vytvořit unikátní tmpdir pro tento test. */
    GError *err = NULL;
    s_test_sd_root = g_dir_make_tmp("mztest-unicard-XXXXXX", &err);
    if (!s_test_sd_root) {
        if (err) g_error_free(err);
        TEST_FAIL_MESSAGE("Nepodařilo se vytvořit tmpdir pro SD root");
        return;
    }

    /* Po debugger-fixes-3 auto-init běží jen pokud SD root NEexistuje
     * (= chrání existující uživatelská data). g_dir_make_tmp ale tmpdir
     * vytvořil = init by se přeskočil a runtime soubory by nevznikly.
     * Reservované jméno použijeme, ale samotný adresář smažeme, ať
     * unicard_set_connected uvidí "prvotní setup" stav a provede plnou
     * inicializaci. */
    g_rmdir(s_test_sd_root);

    /* Nastavit SD root + připojit unicard. unicard_set_connected má
     * early-return pokud g_unicard.connected == conn (= no-op), takže
     * pokud cfgmain default startuje CONNECTED, nedošlo by k volání
     * unicard_init_sd (= struktura SD by nevznikla, první test fail).
     *
     * Vždy nejdřív disconnect, pak connect = guarantee re-init. */
    unicard_set_sd_root_dirpath(s_test_sd_root);
    unicard_set_connected(UNICARD_CONNECTION_DISCONNECTED);
    /* Deterministický FW pro testy = uc3 (nezávisle na per-platform defaultu
     * prvního spuštění, který je na MZ-800 uc1). uc1-specifické testy si FW
     * přepnou samy (set_fw + restore). */
    unicard_set_fw(UNICARD_FW_UC3);
    unicard_set_connected(UNICARD_CONNECTION_CONNECTED);
}


void tearDown(void)
{
    /* Odpojit unicard - zavře otevřené file/dir handly. */
    unicard_set_connected(UNICARD_CONNECTION_DISCONNECTED);

    /* Smazat tmpdir rekurzivně. */
    if (s_test_sd_root) {
        rmtree(s_test_sd_root);
        g_free(s_test_sd_root);
        s_test_sd_root = NULL;
    }
}


/* ================================================================
 * Helpery pro IORQ na unicard portech
 * ================================================================ */

/* Poslat command byte na CMD port (0x50). */
static void uc_send_cmd(uint8_t cmd)
{
    unicard_write_byte(UNICARD_PORT_CMD, cmd);
}

/* Poslat parametr na DATA port (0x51). */
static void uc_send_data(uint8_t data)
{
    unicard_write_byte(UNICARD_PORT_DATA, data);
}

/* Poslat string + terminátor (libovolný znak < 0x20). */
static void uc_send_str(const char *s)
{
    while (*s) {
        uc_send_data((uint8_t)*s++);
    }
    uc_send_data(0x0d); /* terminátor */
}

/* Přečíst byte z DATA portu. */
static uint8_t uc_recv_data(void)
{
    return unicard_read_byte(UNICARD_PORT_DATA);
}

/* Přečíst N-tý byte statusu (0..3).
 * Pošle cmdSTSR (resetuje sts pointer), pak N+1 čtení CMD portu. */
static uint8_t uc_read_status_byte(uint8_t idx)
{
    unicard_write_byte(UNICARD_PORT_CMD, cmdSTSR);
    uint8_t v = 0x00;
    for (uint8_t i = 0; i <= idx; i++) {
        v = unicard_read_byte(UNICARD_PORT_CMD);
    }
    return v;
}

/* Master status byte (1. bajt). */
static uint8_t uc_master_status(void)
{
    return uc_read_status_byte(0);
}


/* ================================================================
 * SMOKE TESTY
 * ================================================================ */

/* setUp/tearDown samy o sobě musí projít bez padání - SD root se
 * vytvoří, unicard se připojí, vznikne struktura /unicard/ s obsahem. */
void test_unicard_setup_creates_sd_structure(void)
{
    char *unimgr_dir = g_build_filename(s_test_sd_root, UNICARD_UNIMGR_DIR, NULL);
    TEST_ASSERT_TRUE_MESSAGE(g_file_test(unimgr_dir, G_FILE_TEST_IS_DIR),
                             "Adresář /unicard musí existovat po set_connected");
    g_free(unimgr_dir);

    char *mgr_mzf = g_build_filename(s_test_sd_root, UNICARD_UNIMGR_DIR,
                                     TEST_UNICARD_MGR_MZF, NULL);
    TEST_ASSERT_TRUE_MESSAGE(g_file_test(mgr_mzf, G_FILE_TEST_IS_REGULAR),
                             "Soubor " TEST_UNICARD_MGR_MZF_PATH " musí být zkopírován");
    g_free(mgr_mzf);
}


/* cmdRESET - master status musí být po RESET bez ERROR a bez BUSY/DOUTRQ. */
void test_unicard_smoke_cmdRESET(void)
{
    uc_send_cmd(cmdRESET);

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "Po cmdRESET nesmí být ERROR");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_BUSY,
                                    "Po cmdRESET nesmí být BUSY");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_DOUTRQ,
                                    "Po cmdRESET nesmí být DOUTRQ");
}


/* cmdREV - vrací firmware revision string ukončený 0x0d. */
void test_unicard_smoke_cmdREV(void)
{
    uc_send_cmd(cmdREV);

    /* Status musí mít DOUTRQ a žádný ERROR. */
    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "Po cmdREV nesmí být ERROR");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_DOUTRQ, sts & UNICARD_STS_DOUTRQ,
                                    "Po cmdREV musí být DOUTRQ=1");

    /* Vyčíst revision string a ověřit, že je neprázdný a končí 0x0d. */
    char buf[64];
    size_t n = 0;
    while (n < sizeof(buf) - 1) {
        uint8_t b = uc_recv_data();
        buf[n++] = (char)b;
        if (b == 0x0d) break;
    }
    buf[n] = '\0';

    TEST_ASSERT_TRUE_MESSAGE(n >= 2, "Revision string musí mít aspoň 1 znak + 0x0d");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x0d, (uint8_t)buf[n - 1],
                                    "Revision string musí končit 0x0d");

    /* Po vyčtení DOUTRQ shozeno. */
    sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_DOUTRQ,
                                    "Po vyčtení DOUTRQ nesmí být DOUTRQ");
}


/* Pošle cmdGETCWD a ověří, že výstup (CWD string + 0x0d) == expected.
 * NEresetuje CWD - použitelné i pro test po cmdCHDIR. */
static void recv_cmdGETCWD_expect(const char *expected)
{
    uc_send_cmd(cmdGETCWD);

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "Po cmdGETCWD nesmí být ERROR");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_DOUTRQ, sts & UNICARD_STS_DOUTRQ,
                                    "Po cmdGETCWD musí být DOUTRQ=1");

    char buf[32];
    size_t n = 0;
    while (n < sizeof(buf) - 1) {
        uint8_t b = uc_recv_data();
        buf[n++] = (char)b;
        if (b == 0x0d) break;
    }
    buf[n] = '\0';

    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, buf, "CWD format mismatch");
}

/* cmdGETCWD - po RESET je CWD "/" (uc1) resp. "0:/" (uc3) + 0x0d. */
static void check_cmdGETCWD_expect(const char *expected)
{
    uc_send_cmd(cmdRESET);
    recv_cmdGETCWD_expect(expected);
}

void test_unicard_smoke_cmdGETCWD(void)
{
    /* uc3 (default): CWD s drive prefixem "0:". Dostupné pro všechny
     * mzarch. */
    unicard_set_fw(UNICARD_FW_UC3);
    check_cmdGETCWD_expect("0:/\x0d");

    /* uc1: CWD bez prefixu. Jen na MZ-800 (na jiných platformách
     * unicard_set_fw(UC1) je no-op a FW zůstane UC3). */
    if (g_mzarch_platform_numeric == 800) {
        unicard_set_fw(UNICARD_FW_UC1);
        check_cmdGETCWD_expect("/\x0d");
        /* Vrátit default pro další testy. */
        unicard_set_fw(UNICARD_FW_UC3);
    }
}


/* Vejde do /sub (cmdCHDIR) a ověří, že chdir neselhal.
 * Předpoklad: /sub na SD existuje. Cesta je absolutní, takže je odolná
 * vůči případnému resetu CWD při přepnutí FW (disconnect/reconnect). */
static void chdir_to_sub_ok(void)
{
    uc_send_cmd(cmdCHDIR);
    uc_send_str("/sub");
    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "cmdCHDIR /sub nesmí ERROR");
}

/* Regrese (bugreport 0010): cmdGETCWD po vstupu do podadresáře MUSÍ vrátit
 * single-slash cestu - "0:/sub" (uc3) resp. "/sub" (uc1) - shodně s reálnou
 * Unikartou. Dřív emu path-normalizér produkoval vedoucí dvojité lomítko
 * ("0://sub" / "//sub"), což se v mzdos shellu projevovalo jako "//dir"
 * ve výpisu PWD. Bug se týkal obou FW (sdílený normalizér), oba real FW
 * (přes FatFS f_getcwd) přitom vrací single slash. */
void test_unicard_cmdGETCWD_subdir_single_slash(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* Vytvořit /sub na SD (per-test tmpdir, tj. neexistuje). */
    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdMKDIR);
    uc_send_str("/sub");
    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "cmdMKDIR /sub nesmí ERROR");

    /* uc3 (default, všechny mzarch): "0:/sub" + CR. chdir AŽ po set_fw
     * (reconnect při přepnutí FW může resetovat CWD na "/"). */
    unicard_set_fw(UNICARD_FW_UC3);
    chdir_to_sub_ok();
    recv_cmdGETCWD_expect("0:/sub\x0d");

    /* uc1 (jen MZ-800): "/sub" + CR bez drive prefixu. */
    if (g_mzarch_platform_numeric == 800) {
        unicard_set_fw(UNICARD_FW_UC1);
        chdir_to_sub_ok();
        recv_cmdGETCWD_expect("/sub\x0d");
        /* Vrátit default pro další testy. */
        unicard_set_fw(UNICARD_FW_UC3);
    }
}


/* ================================================================
 * UNIT TESTY - parser equivalence
 * ================================================================ */

/* Parser test: cmdOPEN ("BS") na neexistující soubor. Ověřuje že
 * parser:
 *   1. načte 1 byte parametr (mode)
 *   2. načte string parametr (path) ukončený 0x0d
 *   3. dispatch do do_cmdOPEN volá unicard_file_open
 *   4. fopen selže (FR_NO_FILE) a sts_err=ERROR
 *
 * Tím nepřímo ověřujeme, že parser zvládne libovolnou kombinaci
 * B a S znaků - cmdOPEN je jediný existující dispatched příkaz s
 * mixem byte+string. */
void test_unicard_parser_BS_via_cmdOPEN(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);

    /* OPEN s mode=FA_READ na neexistující path. */
    uc_send_cmd(cmdOPEN);

    /* Po cmdOPEN musí být BUSY (čeká parametry). */
    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_BUSY, sts & UNICARD_STS_BUSY,
                                    "Po cmdOPEN bez parametrů musí být BUSY");

    /* Pošleme byte parametr (mode = FA_READ = 0x01). */
    uc_send_data(0x01);

    /* Po jednom byte je parser dál v BUSY (čeká string). */
    sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_BUSY, sts & UNICARD_STS_BUSY,
                                    "Po prvním byte cmdOPEN musí být dál BUSY (čeká S)");

    /* Pošleme path - neexistující, terminátor 0x0d. */
    uc_send_str("/nonexistent_file.txt");

    /* Po dokončení parametrů: BUSY=0, ERROR=1 (fopen selhal). */
    sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_BUSY,
                                    "Po posledním parametru cmdOPEN nesmí být BUSY");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_ERROR, sts & UNICARD_STS_ERROR,
                                    "OPEN neexistujícího souboru musí dát ERROR");
}


/* Parser test: cmdOPEN ("BS") na existující soubor manageru
 * (vytvořen unicard_init_sd v setUp, per-arch jméno). Otevření musí uspět. */
void test_unicard_parser_BS_open_existing(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x01); /* FA_READ */
    uc_send_str(TEST_UNICARD_MGR_MZF_PATH);

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "OPEN existujícího souboru nesmí dát ERROR");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_READ, sts & UNICARD_STS_READ,
                                    "Po OPEN ve FA_READ módu musí být READ_FILE bit");
}


/* ================================================================
 * UNIT TESTY - Fáze 1: Read-only metadata
 * ================================================================ */

/* Helper: vyčte 4B DWORD LE z DATA portu. */
static uint32_t uc_recv_dword_le(void)
{
    uint32_t v = 0;
    v |= (uint32_t)uc_recv_data();
    v |= (uint32_t)uc_recv_data() << 8;
    v |= (uint32_t)uc_recv_data() << 16;
    v |= (uint32_t)uc_recv_data() << 24;
    return v;
}

/* Helper: vyčte 2B WORD LE. */
static uint16_t uc_recv_word_le(void)
{
    uint16_t v = 0;
    v |= (uint16_t)uc_recv_data();
    v |= (uint16_t)uc_recv_data() << 8;
    return v;
}

/* Helper: vytvoří test soubor v SD root o dané velikosti, naplněný
 * sekvencí 0..255 cyklicky. Cesta v SD root, ne emulátorová. */
static void create_test_file(const char *rel_path, size_t size)
{
    char *full = g_build_filename(s_test_sd_root, rel_path, NULL);
    FILE *f = g_fopen(full, "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "Nepodařilo se vytvořit testovací soubor");
    for (size_t i = 0; i < size; i++) {
        uint8_t b = (uint8_t)(i & 0xff);
        fwrite(&b, 1, 1, f);
    }
    fclose(f);
    g_free(full);
}


/* cmdSIZE - po OPEN existujícího souboru vrátí jeho přesnou velikost. */
void test_unicard_cmdSIZE(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("test_size.bin", 1234);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x01); /* FA_READ */
    uc_send_str("/test_size.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "OPEN test_size.bin nesmí dát ERROR");

    uc_send_cmd(cmdSIZE);
    sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_DOUTRQ, sts & UNICARD_STS_DOUTRQ,
                                    "Po cmdSIZE musí být DOUTRQ=1");

    uint32_t size = uc_recv_dword_le();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1234, size, "cmdSIZE musí vrátit 1234");
}


/* cmdTELL - po OPEN je TELL=0; po několika čteních se posune. */
void test_unicard_cmdTELL(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("test_tell.bin", 100);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x01);
    uc_send_str("/test_tell.bin");

    /* Po OPEN: TELL = 0. */
    uc_send_cmd(cmdTELL);
    uint32_t pos = uc_recv_dword_le();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pos, "TELL po OPEN musí být 0");

    /* Přečíst 7 bytů přes data port (cmdINTGETC implicit), pak TELL=7. */
    for (int i = 0; i < 7; i++) (void)uc_recv_data();

    uc_send_cmd(cmdTELL);
    pos = uc_recv_dword_le();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(7, pos, "TELL po 7 čteních musí být 7");
}


/* cmdSTAT - na existující soubor vrací FILINFO s odpovídající fsize a
 * lfname rovným basename. */
void test_unicard_cmdSTAT_file(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("stat_target.bin", 4242);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdSTAT);

    /* cmdSTAT čeká na string - po cmd musí být BUSY. */
    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_BUSY, sts & UNICARD_STS_BUSY,
                                    "cmdSTAT musí čekat na string parametr");

    uc_send_str("/stat_target.bin");

    sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "STAT existujícího souboru nesmí ERROR");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_DOUTRQ, sts & UNICARD_STS_DOUTRQ,
                                    "STAT musí dát DOUTRQ");

    /* Rozparsovat FILINFO výstup. */
    uint32_t fsize = uc_recv_dword_le();
    uint16_t fdate = uc_recv_word_le();
    uint16_t ftime = uc_recv_word_le();
    uint8_t fattrib = uc_recv_data();

    char fname[13];
    for (int i = 0; i < 13; i++) fname[i] = (char)uc_recv_data();

    uint8_t lfn_strlen = uc_recv_data();

    char lfname[33]; /* _MAX_LFN = 32 */
    for (int i = 0; i < 32; i++) lfname[i] = (char)uc_recv_data();
    lfname[32] = '\0';

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4242, fsize, "fsize musí být 4242");
    TEST_ASSERT_TRUE_MESSAGE(fdate != 0, "fdate musí být nenulové (mtime)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, fattrib & UNICARD_AM_DIR,
                                    "fattrib pro soubor nesmí mít AM_DIR");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("stat_target.bin", lfname,
                                     "lfname musí být basename");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(strlen("stat_target.bin"), lfn_strlen,
                                     "lfn_strlen musí odpovídat délce basename");

    /* Suppress unused warning. */
    (void)ftime;
    (void)fname;
}


/* cmdSTAT - na adresář /unicard musí mít AM_DIR atribut. */
void test_unicard_cmdSTAT_dir(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdSTAT);
    uc_send_str("/unicard");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "STAT /unicard nesmí ERROR");

    /* Skip prvních 8 bytů (fsize + fdate + ftime), čtu fattrib. */
    for (int i = 0; i < 8; i++) (void)uc_recv_data();
    uint8_t fattrib = uc_recv_data();

    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_AM_DIR, fattrib & UNICARD_AM_DIR,
                                    "STAT /unicard musí mít AM_DIR");

    /* Zbytek (zatím nedůležitý) jen vyčíst aby uvolnil DOUTRQ. */
    int remaining = (23 + _MAX_LFN) - 8 - 1;
    for (int i = 0; i < remaining; i++) (void)uc_recv_data();
}


/* cmdSTAT na neexistující path - ERROR + ff_res = FR_NO_FILE. */
void test_unicard_cmdSTAT_missing(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdSTAT);
    uc_send_str("/does_not_exist.xyz");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_ERROR, sts & UNICARD_STS_ERROR,
                                    "STAT na neexistující path musí ERROR");

    /* 4. status byte = ff_res. */
    uint8_t ff_res = uc_read_status_byte(3);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(FR_NO_FILE, ff_res,
                                    "ff_res po STAT missing musí být FR_NO_FILE");
}


/* cmdGETFREE - vrátí 8 bytů (total + free), oba > 0, free <= total. */
void test_unicard_cmdGETFREE(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdGETFREE);

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "cmdGETFREE nesmí ERROR");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_DOUTRQ, sts & UNICARD_STS_DOUTRQ,
                                    "cmdGETFREE musí dát DOUTRQ");

    uint32_t total = uc_recv_dword_le();
    uint32_t freev = uc_recv_dword_le();

    TEST_ASSERT_TRUE_MESSAGE(total > 0, "total_sect musí být > 0");
    TEST_ASSERT_TRUE_MESSAGE(freev > 0, "free_sect musí být > 0");
    TEST_ASSERT_TRUE_MESSAGE(freev <= total, "free_sect musí být <= total_sect");
}


/* ================================================================
 * UNIT TESTY - Fáze 2: Seek
 * ================================================================ */

/* Helper: pošle cmdSEEK s daným módem a offsetem (BBBBB). */
static void uc_seek(uint8_t mode, uint32_t offset)
{
    uc_send_cmd(cmdSEEK);
    uc_send_data(mode);
    uc_send_data((uint8_t)(offset & 0xff));
    uc_send_data((uint8_t)((offset >> 8) & 0xff));
    uc_send_data((uint8_t)((offset >> 16) & 0xff));
    uc_send_data((uint8_t)((offset >> 24) & 0xff));
}

/* Helper: vrátí TELL pozici (předpokládá otevřený soubor). */
static uint32_t uc_tell(void)
{
    uc_send_cmd(cmdTELL);
    return uc_recv_dword_le();
}


/* cmdSEEK mode 0 (SET) - absolutní pozice. */
void test_unicard_cmdSEEK_set(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("seek_set.bin", 1000);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x01);
    uc_send_str("/seek_set.bin");

    uc_seek(0, 100);
    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "SEEK SET 100 nesmí ERROR");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(100, uc_tell(),
                                     "Po SEEK SET 100 musí být TELL=100");
}


/* cmdSEEK mode 1 (END) - vzdálenost OD KONCE. */
void test_unicard_cmdSEEK_end(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("seek_end.bin", 500);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x01);
    uc_send_str("/seek_end.bin");

    /* mode 1, offset 0 → konec souboru (= 500). */
    uc_seek(1, 0);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(500, uc_tell(),
                                     "SEEK END 0 musí dát TELL=size");

    /* mode 1, offset 50 → 50 bytů PŘED koncem (= 450). */
    uc_seek(1, 50);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(450, uc_tell(),
                                     "SEEK END 50 musí dát TELL=450");
}


/* cmdSEEK mode 2 (REL_UP) a mode 3 (REL_DOWN) - relativní posun. */
void test_unicard_cmdSEEK_rel(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("seek_rel.bin", 1000);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x01);
    uc_send_str("/seek_rel.bin");

    uc_seek(0, 200);                     /* TELL=200 */
    TEST_ASSERT_EQUAL_UINT32(200, uc_tell());

    uc_seek(2, 50);                      /* +50 → TELL=250 */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(250, uc_tell(),
                                     "REL_UP +50 musí dát TELL=250");

    uc_seek(3, 30);                      /* -30 → TELL=220 */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(220, uc_tell(),
                                     "REL_DOWN -30 musí dát TELL=220");
}


/* cmdSEEK na zavřený soubor → ERROR. */
void test_unicard_cmdSEEK_no_file(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_seek(0, 100);

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_ERROR, sts & UNICARD_STS_ERROR,
                                    "SEEK bez otevřeného souboru musí ERROR");
}


/* ================================================================
 * UNIT TESTY - Fáze 3: File WRITE
 * ================================================================ */

/* Pomocný helper: zavře otevřený soubor (cmdCLOSE). */
static void uc_close(void)
{
    uc_send_cmd(cmdCLOSE);
}


/* Read full file content from host FS pro verifikaci. */
static size_t read_file_content(const char *rel_path, uint8_t *buf, size_t cap)
{
    char *full = g_build_filename(s_test_sd_root, rel_path, NULL);
    FILE *f = g_fopen(full, "rb");
    g_free(full);
    if (!f) return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}


/* Write workflow: vytvořit soubor přes FA_CREATE_NEW|FA_WRITE, zapsat
 * 100 bytů přes cmdINTPUTC, zavřít, znovu otevřít READ a ověřit. */
void test_unicard_write_create_new(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);

    /* OPEN s FA_CREATE_NEW (0x04) | FA_WRITE (0x02) = 0x06 */
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x06);
    uc_send_str("/write_test.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "OPEN CREATE_NEW|WRITE nesmí ERROR");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_WRITE, sts & UNICARD_STS_WRITE,
                                    "Po OPEN ve WRITE módu musí být WRITE_FILE bit");

    /* Zapsat 100 bytů přes cmdINTPUTC (zápis na DATA portu). */
    for (int i = 0; i < 100; i++) {
        uc_send_data((uint8_t)(i & 0xff));
    }

    uc_close();

    /* Verifikace - přečíst host FS. */
    uint8_t buf[256];
    size_t n = read_file_content("write_test.bin", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(100, n,
                                     "Soubor musí mít 100 bytů");
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_EQUAL_UINT8(i & 0xff, buf[i]);
    }
}


/* FA_WRITE samotné → open existing for write (mapping na "r+b").
 * Po fix v unicard_file_open() už nesmí vracet FR_INVALID_PARAMETER. */
void test_unicard_write_existing_FA_WRITE(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("rewrite.bin", 50); /* obsah 0..49 */

    uc_send_cmd(cmdRESET);

    /* OPEN s pouze FA_WRITE (0x02). Po fix musí uspět. */
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x02);
    uc_send_str("/rewrite.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "OPEN FA_WRITE existujícího nesmí ERROR");

    /* Přepsat prvních 10 bytů hodnotou 0xFF. */
    for (int i = 0; i < 10; i++) {
        uc_send_data(0xFF);
    }

    uc_close();

    /* Verifikace - prvních 10 = 0xFF, zbytek beze změny (10..49). */
    uint8_t buf[100];
    size_t n = read_file_content("rewrite.bin", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(50, n, "Soubor musí mít původní 50 bytů");
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, buf[i]);
    }
    for (int i = 10; i < 50; i++) {
        TEST_ASSERT_EQUAL_HEX8(i & 0xff, buf[i]);
    }
}


/* FA_CREATE_ALWAYS na neexistující soubor → vznikne nový s daty.
 * Regresní test pro bug: chyběl FA_CREATE_ALWAYS branch v
 * unicard_file_open(), mode 0x0A padalo do else větve "r+b" a fopen
 * selhal s "Cant open ... in mode 'r+b'" (file not exists). */
void test_unicard_write_FA_CREATE_ALWAYS_new(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);

    /* OPEN s FA_CREATE_ALWAYS (0x08) | FA_WRITE (0x02) = 0x0A. */
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x0A);
    uc_send_str("/create_always_new.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "OPEN FA_CREATE_ALWAYS na neexistující nesmí ERROR");

    for (int i = 0; i < 30; i++) {
        uc_send_data((uint8_t)(i & 0xff));
    }
    uc_close();

    uint8_t buf[100];
    size_t n = read_file_content("create_always_new.bin", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(30, n, "Soubor musí mít 30 bytů");
    for (int i = 0; i < 30; i++) {
        TEST_ASSERT_EQUAL_UINT8(i & 0xff, buf[i]);
    }
}


/* FA_CREATE_ALWAYS na existující soubor → truncate na 0 a přepsat.
 * Sémantika dle FatFS: vždy začít s prázdným obsahem. */
void test_unicard_write_FA_CREATE_ALWAYS_truncate(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("create_always_trunc.bin", 100); /* obsah 0..99 */

    uc_send_cmd(cmdRESET);

    uc_send_cmd(cmdOPEN);
    uc_send_data(0x0A); /* FA_CREATE_ALWAYS | FA_WRITE */
    uc_send_str("/create_always_trunc.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "OPEN FA_CREATE_ALWAYS na existující nesmí ERROR");

    /* Zapsat jen 5 bytů. */
    for (int i = 0; i < 5; i++) {
        uc_send_data(0xAB);
    }
    uc_close();

    /* Soubor musí mít přesně 5 bytů (= truncated, ne 100). */
    uint8_t buf[200];
    size_t n = read_file_content("create_always_trunc.bin", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(5, n,
                                     "FA_CREATE_ALWAYS musí soubor zkrátit na 5 bytů");
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAB, buf[i]);
    }
}


/* FA_OPEN_ALWAYS na existující soubor → zachovat obsah, dovolit přepis
 * od začátku (= ne truncate). Regresní test pro bug: před fix mapoval
 * FA_OPEN_ALWAYS na "wb" / "w+b" (truncate), což porušovalo FatFS
 * sémantiku (open existing or create new bez truncate). */
void test_unicard_write_FA_OPEN_ALWAYS_existing_preserves(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("open_always_exist.bin", 100); /* obsah 0..99 */

    uc_send_cmd(cmdRESET);

    /* OPEN s FA_OPEN_ALWAYS (0x10) | FA_WRITE (0x02) = 0x12. */
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x12);
    uc_send_str("/open_always_exist.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "OPEN FA_OPEN_ALWAYS na existující nesmí ERROR");

    /* Přepsat prvních 10 bytů hodnotou 0xCC. */
    for (int i = 0; i < 10; i++) {
        uc_send_data(0xCC);
    }
    uc_close();

    /* Soubor musí mít stále 100 bytů (= ne truncate); prvních 10 = 0xCC,
     * zbytek (10..99) musí být beze změny. */
    uint8_t buf[200];
    size_t n = read_file_content("open_always_exist.bin", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(100, n,
                                     "FA_OPEN_ALWAYS NESMÍ zkrátit existující soubor");
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xCC, buf[i]);
    }
    for (int i = 10; i < 100; i++) {
        TEST_ASSERT_EQUAL_HEX8(i & 0xff, buf[i]);
    }
}


/* FA_OPEN_ALWAYS na neexistující soubor → vytvořit nový. */
void test_unicard_write_FA_OPEN_ALWAYS_creates_new(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);

    uc_send_cmd(cmdOPEN);
    uc_send_data(0x12); /* FA_OPEN_ALWAYS | FA_WRITE */
    uc_send_str("/open_always_new.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "OPEN FA_OPEN_ALWAYS na neexistující nesmí ERROR");

    for (int i = 0; i < 7; i++) {
        uc_send_data(0x55);
    }
    uc_close();

    uint8_t buf[50];
    size_t n = read_file_content("open_always_new.bin", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(7, n, "Soubor musí mít 7 bytů");
    for (int i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x55, buf[i]);
    }
}


/* cmdTRUNC - zkrátit soubor na aktuální pozici. */
void test_unicard_cmdTRUNC(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("trunc_test.bin", 100);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdOPEN);
    uc_send_data(0x02); /* FA_WRITE */
    uc_send_str("/trunc_test.bin");

    /* SEEK na 50, TRUNC. */
    uc_seek(0, 50);
    uc_send_cmd(cmdTRUNC);

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "cmdTRUNC nesmí ERROR");

    uc_close();

    /* Soubor musí mít 50 bytů. */
    uint8_t buf[200];
    size_t n = read_file_content("trunc_test.bin", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(50, n,
                                     "Po TRUNC na pozici 50 musí mít soubor 50 B");
}


/* cmdSYNC - smoke test, nesmí padnout. */
void test_unicard_cmdSYNC(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);

    uc_send_cmd(cmdOPEN);
    uc_send_data(0x06); /* FA_CREATE_NEW | FA_WRITE */
    uc_send_str("/sync_test.bin");

    /* Zapsat 5 bytů. */
    for (int i = 0; i < 5; i++) uc_send_data((uint8_t)i);

    uc_send_cmd(cmdSYNC);
    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "cmdSYNC po zápisu nesmí ERROR");

    uc_close();
}


/* ================================================================
 * UNIT TESTY - Fáze 4: cmdMKDIR / cmdUNLINK / cmdRENAME
 * ================================================================ */

/* cmdMKDIR - vytvoření adresáře. */
void test_unicard_cmdMKDIR(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdMKDIR);
    uc_send_str("/newdir");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "cmdMKDIR /newdir nesmí ERROR");

    /* Verifikace na host FS. */
    char *full = g_build_filename(s_test_sd_root, "newdir", NULL);
    TEST_ASSERT_TRUE_MESSAGE(g_file_test(full, G_FILE_TEST_IS_DIR),
                             "Adresář /newdir musí existovat");
    g_free(full);
}


/* cmdMKDIR na existující path → ERROR (FR_EXIST). */
void test_unicard_cmdMKDIR_exists(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdMKDIR);
    uc_send_str("/unicard"); /* už existuje z setUp */

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_ERROR, sts & UNICARD_STS_ERROR,
                                    "MKDIR existujícího musí ERROR");

    uint8_t ff_res = uc_read_status_byte(3);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(FR_EXIST, ff_res,
                                    "ff_res musí být FR_EXIST");
}


/* cmdUNLINK - smazání souboru. */
void test_unicard_cmdUNLINK_file(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("to_delete.bin", 50);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdUNLINK);
    uc_send_str("/to_delete.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "UNLINK existujícího nesmí ERROR");

    char *full = g_build_filename(s_test_sd_root, "to_delete.bin", NULL);
    TEST_ASSERT_FALSE_MESSAGE(g_file_test(full, G_FILE_TEST_EXISTS),
                              "Soubor po UNLINK nesmí existovat");
    g_free(full);
}


/* cmdUNLINK na neexistující → ERROR (FR_NO_FILE). */
void test_unicard_cmdUNLINK_missing(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdUNLINK);
    uc_send_str("/does_not_exist.txt");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_ERROR, sts & UNICARD_STS_ERROR,
                                    "UNLINK na missing musí ERROR");

    uint8_t ff_res = uc_read_status_byte(3);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(FR_NO_FILE, ff_res,
                                    "ff_res musí být FR_NO_FILE");
}


/* cmdRENAME - parser SS, přejmenování souboru. */
void test_unicard_cmdRENAME(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("old_name.bin", 30);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdRENAME);

    /* První S: old_name. */
    uc_send_str("/old_name.bin");
    /* Po prvním stringu musí být dál BUSY (čeká druhý S). */
    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_BUSY, sts & UNICARD_STS_BUSY,
                                    "Mezi S a S musí být dál BUSY");

    /* Druhý S: new_name. */
    uc_send_str("/new_name.bin");

    sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "RENAME nesmí ERROR");

    char *old_full = g_build_filename(s_test_sd_root, "old_name.bin", NULL);
    char *new_full = g_build_filename(s_test_sd_root, "new_name.bin", NULL);
    TEST_ASSERT_FALSE_MESSAGE(g_file_test(old_full, G_FILE_TEST_EXISTS),
                              "Starý název nesmí existovat");
    TEST_ASSERT_TRUE_MESSAGE(g_file_test(new_full, G_FILE_TEST_EXISTS),
                             "Nový název musí existovat");
    g_free(old_full);
    g_free(new_full);
}


/* ================================================================
 * UNIT TESTY - Fáze 5: cmdCHMOD / cmdUTIME
 * ================================================================ */

/* cmdCHMOD - no-op, ale musí prošlo OK na existující path. */
void test_unicard_cmdCHMOD_noop(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("chmod_target.bin", 10);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdCHMOD);
    uc_send_data(0x01);  /* attr = AM_RDO */
    uc_send_data(0x21);  /* mask = AM_RDO | AM_ARC */
    uc_send_str("/chmod_target.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "cmdCHMOD na existující path nesmí ERROR");
}


/* cmdCHMOD na neexistující → ERROR FR_NO_FILE. */
void test_unicard_cmdCHMOD_missing(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdCHMOD);
    uc_send_data(0x00);
    uc_send_data(0x00);
    uc_send_str("/missing.txt");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_ERROR, sts & UNICARD_STS_ERROR,
                                    "CHMOD na missing musí ERROR");
}


/* cmdUTIME - nastavení známé časové značky a verifikace přes cmdSTAT. */
void test_unicard_cmdUTIME(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("utime_target.bin", 5);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdUTIME);
    uc_send_data(15);   /* D */
    uc_send_data(7);    /* M */
    uc_send_data(40);   /* Y-1980 = 2020 */
    uc_send_data(12);   /* H */
    uc_send_data(30);   /* M */
    uc_send_data(20);   /* S */
    uc_send_str("/utime_target.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "cmdUTIME nesmí ERROR");

    /* Ověření přes cmdSTAT - fdate musí odpovídat 2020-07-15. */
    uc_send_cmd(cmdSTAT);
    uc_send_str("/utime_target.bin");

    /* Skip fsize. */
    for (int i = 0; i < 4; i++) (void)uc_recv_data();
    uint16_t fdate = uc_recv_word_le();
    uint16_t ftime = uc_recv_word_le();

    /* FAT date: (year-1980)<<9 | month<<5 | day = (40<<9)|(7<<5)|15 = 0x50EF */
    uint16_t expected_date = (40 << 9) | (7 << 5) | 15;
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(expected_date, fdate,
                                     "fdate musí odpovídat 2020-07-15");

    /* FAT time: hour<<11 | min<<5 | sec/2 = (12<<11)|(30<<5)|10 = 0x63CA */
    uint16_t expected_time = (12 << 11) | (30 << 5) | (20 / 2);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(expected_time, ftime,
                                     "ftime musí odpovídat 12:30:20");

    /* Vyčíst zbytek FILINFO aby se uvolnil DOUTRQ. */
    int remaining = (23 + _MAX_LFN) - 8;
    for (int i = 0; i < remaining; i++) (void)uc_recv_data();
}


/* cmdUTIME na neexistující → ERROR. */
void test_unicard_cmdUTIME_missing(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdUTIME);
    uc_send_data(1);
    uc_send_data(1);
    uc_send_data(40);
    uc_send_data(0);
    uc_send_data(0);
    uc_send_data(0);
    uc_send_str("/missing.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_ERROR, sts & UNICARD_STS_ERROR,
                                    "UTIME na missing musí ERROR");
}


/* ================================================================
 * UNIT TESTY - Fáze 6: cmdREADDIR (binární FILINFO output)
 * ================================================================ */

/* Helper: vyčte kompletní FILINFO (23+_MAX_LFN bytů) z DATA portu
 * a uloží lfname do out_name. Vrátí 1 pokud načteno, 0 pokud konec. */
static int uc_read_dir_filinfo(uint32_t *fsize, uint8_t *fattrib, char *out_name, size_t out_cap)
{
    /* Pokud máme READDIR_DATA bit, máme co číst. */
    uint8_t sts = uc_master_status();
    if (!(sts & UNICARD_STS_READDIR)) {
        return 0;
    }

    *fsize = uc_recv_dword_le();
    (void)uc_recv_word_le(); /* fdate */
    (void)uc_recv_word_le(); /* ftime */
    *fattrib = uc_recv_data();

    /* fname[13] = S (8.3 short), offset 9 */
    char sbuf[13];
    for (int i = 0; i < 13; i++) sbuf[i] = (char)uc_recv_data();
    sbuf[12] = '\0';

    uint8_t lfn_strlen = uc_recv_data();

    /* lfname[_MAX_LFN] = L (LFN), offset 23 */
    char buf[_MAX_LFN];
    for (int i = 0; i < _MAX_LFN; i++) buf[i] = (char)uc_recv_data();

    /* Stejné pravidlo jako mzdos DIR: L když LEN>0, jinak fallback na S
     * (8.3 short). Reálné HW i emu plní L jen pro jména s LFN entry. */
    if (out_name && out_cap > 0) {
        const char *src = (lfn_strlen > 0) ? buf : sbuf;
        size_t copy = strlen(src);
        if (copy > out_cap - 1) copy = out_cap - 1;
        memcpy(out_name, src, copy);
        out_name[copy] = '\0';
    }
    return 1;
}


/* cmdREADDIR - vyčte všechny položky /unicard/, ověří přítomnost
 * známých souborů (mzfloader.mzq, manager MZF, mzfloader.cfg). */
void test_unicard_cmdREADDIR(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdREADDIR);
    uc_send_str("/" UNICARD_UNIMGR_DIR);

    int found_mgr = 0, found_mzq = 0, found_cfg = 0;

    /* Vyčíst všechny položky, dokud READDIR_DATA bit zhasne. */
    for (int iter = 0; iter < 100; iter++) {
        uint32_t fsize;
        uint8_t fattrib;
        char name[40];
        if (!uc_read_dir_filinfo(&fsize, &fattrib, name, sizeof(name))) break;

        /* Case-insensitive: u jmen bez LFN je fallback na S (8.3), které
         * je v uc3 uppercase (altname). */
        if (g_ascii_strcasecmp(name, TEST_UNICARD_MGR_MZF) == 0) found_mgr = 1;
        else if (g_ascii_strcasecmp(name, UNICARD_UNIMGR_MZFLOADER_MZQ) == 0) found_mzq = 1;
        else if (g_ascii_strcasecmp(name, UNICARD_UNIMGR_MZFLOADER_CFG) == 0) found_cfg = 1;
    }

    TEST_ASSERT_TRUE_MESSAGE(found_mgr, "READDIR musí najít " TEST_UNICARD_MGR_MZF);
    TEST_ASSERT_TRUE_MESSAGE(found_mzq, "READDIR musí najít " UNICARD_UNIMGR_MZFLOADER_MZQ);
    TEST_ASSERT_TRUE_MESSAGE(found_cfg, "READDIR musí najít " UNICARD_UNIMGR_MZFLOADER_CFG);
}


/* cmdREADDIR rozliší soubor od adresáře přes AM_DIR atribut. */
void test_unicard_cmdREADDIR_dir_attribute(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* Vytvořit subdir + soubor v root. */
    char *subdir_full = g_build_filename(s_test_sd_root, "subdir1", NULL);
    g_mkdir(subdir_full, 0755);
    g_free(subdir_full);
    create_test_file("file1.bin", 7);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdREADDIR);
    uc_send_str("/");

    int subdir_is_dir = -1, file_is_dir = -1;

    for (int iter = 0; iter < 50; iter++) {
        uint32_t fsize;
        uint8_t fattrib;
        char name[40];
        if (!uc_read_dir_filinfo(&fsize, &fattrib, name, sizeof(name))) break;

        /* Case-insensitive: bez LFN fallback na S (8.3), v uc3 uppercase. */
        if (g_ascii_strcasecmp(name, "subdir1") == 0) subdir_is_dir = (fattrib & UNICARD_AM_DIR) ? 1 : 0;
        else if (g_ascii_strcasecmp(name, "file1.bin") == 0) file_is_dir = (fattrib & UNICARD_AM_DIR) ? 1 : 0;
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, subdir_is_dir,
                                  "subdir1 musí mít AM_DIR=1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, file_is_dir,
                                  "file1.bin musí mít AM_DIR=0");
}


/* ================================================================
 * UNIT TESTY - SD jail (directory traversal protection)
 *
 * Pokrývá nový helper unicard_jail_resolve() a unicard_jail_resolve_with_naked().
 * Cílem je ověřit, že emu-supplied cesty nemůžou opustit SD root - ".." segmenty,
 * absolutní cesty mimo SD root a Windows-style "..\\" sekvence se musí
 * detekovat a operace dostane NULL/FR_NO_PATH.
 *
 * Pre-podmínka: work_dir je "/" (= po cmdRESET). Naked path == relativní
 * k SD root, plný path se sestaví join se s_test_sd_root.
 * ================================================================ */

/* Helper: ověří že 'full' je under SD_root (= začíná SD_rootem) a
 * obsahuje očekávaný basename (post-normalizace).
 *
 * Pozn.: separátor v plné cestě je platform-dependent (na Windows mix
 * "/" a "\\" podle glib build), proto kontrolujeme jen že basename
 * komponenta se v cestě objevuje. */
static void assert_full_under_sd_root(const char *full, const char *expected_basename,
                                      const char *msg)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(full, msg);
    /* Plný path musí začínat SD_root prefixem - to je security invariant. */
    TEST_ASSERT_TRUE_MESSAGE(g_str_has_prefix(full, s_test_sd_root),
                             "Plný path musí být uvnitř SD_root prefix");
    /* A obsahovat očekávaný basename (po normalizaci). */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(full, expected_basename), msg);
}


/* Bezpečné cesty uvnitř SD root - jail musí vrátit non-NULL plný path
 * začínající SD_root prefixem.
 *
 * Naked path má vždy single-slash tvar ("/foo", "/subdir/foo.mzf"),
 * shodný s tím, co vrací reálná Unikarta (uc1 i uc3 přes FatFS f_getcwd).
 * Dřívější quirk s vedoucím dvojitým lomítkem ("/foo" -> "//foo") byl
 * příčinou bugu v cmdGETCWD (mzdos PWD zobrazoval "//dir") a je opraven
 * v unicard_normalize_emu_path. */
void test_unicard_jail_basic_paths(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* Po cmdRESET je work_dir = "/", tj. všechny relativní cesty
     * se rootují do SD root. */
    uc_send_cmd(cmdRESET);

    /* Case A: prázdná cesta - resolve na SD root. */
    char *naked = NULL;
    char *full = unicard_jail_resolve_with_naked("", &naked);
    TEST_ASSERT_NOT_NULL_MESSAGE(full, "Prázdná cesta musí být platná (SD root)");
    TEST_ASSERT_NOT_NULL(naked);
    /* Root naked musí být přesně single-slash "/". */
    TEST_ASSERT_EQUAL_STRING_MESSAGE("/", naked,
                                     "Naked pro \"\" (SD root) musí být \"/\"");
    /* Plný path = SD_root (suffix s_test_sd_root sám sebe = true). */
    TEST_ASSERT_TRUE_MESSAGE(g_str_has_prefix(full, s_test_sd_root),
                             "Plný path \"\" musí být SD_root");
    baseui_tools_mem_free(full);
    baseui_tools_mem_free(naked);

    /* Case B: leading slash - SD-rooted path. Naked musí být single-slash. */
    naked = NULL;
    full = unicard_jail_resolve_with_naked("/foo", &naked);
    TEST_ASSERT_NOT_NULL_MESSAGE(naked, "Naked pro /foo nesmí být NULL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("/foo", naked,
                                     "Naked pro /foo musí být single-slash \"/foo\"");
    assert_full_under_sd_root(full, "foo", "Plný path /foo musí být SD_root/.../foo");
    baseui_tools_mem_free(full);
    baseui_tools_mem_free(naked);

    /* Case C: relativní subdir s souborem. Naked musí být single-slash. */
    naked = NULL;
    full = unicard_jail_resolve_with_naked("subdir/foo.mzf", &naked);
    TEST_ASSERT_NOT_NULL_MESSAGE(naked, "Naked pro subdir/foo.mzf nesmí být NULL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("/subdir/foo.mzf", naked,
                                     "Naked pro subdir/foo.mzf musí být \"/subdir/foo.mzf\"");
    assert_full_under_sd_root(full, "foo.mzf",
                              "Plný path musí obsahovat foo.mzf");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(full, "subdir"),
                                 "Plný path musí obsahovat subdir komponentu");
    baseui_tools_mem_free(full);
    baseui_tools_mem_free(naked);

    /* Case D: tečka jako CWD. */
    naked = NULL;
    full = unicard_jail_resolve_with_naked(".", &naked);
    TEST_ASSERT_NOT_NULL_MESSAGE(full, "Cesta \".\" musí být platná (SD root)");
    TEST_ASSERT_TRUE_MESSAGE(g_str_has_prefix(full, s_test_sd_root),
                             "Plný path \".\" musí být SD_root");
    baseui_tools_mem_free(full);
    baseui_tools_mem_free(naked);
}


/* Pokusy o escape ze SD root - jail MUSÍ vrátit NULL. */
void test_unicard_jail_escape_blocked(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET); /* work_dir = "/" */

    /* Pokud work_dir == "/" (= žádné absorpční úrovně), JEDEN ".."
     * segment už je escape. */

    const char *escape_paths[] = {
        "../etc/passwd",
        "../../foo",
        "subdir/../../bar",
        "subdir/../../../baz",
        "/..",
        "/../foo",
        NULL
    };

    for (int i = 0; escape_paths[i]; i++) {
        char *naked = NULL;
        char *full = unicard_jail_resolve_with_naked(escape_paths[i], &naked);
        char msg[128];
        g_snprintf(msg, sizeof(msg),
                   "Escape path '%s' musí vrátit NULL (= jail blokuje)",
                   escape_paths[i]);
        TEST_ASSERT_NULL_MESSAGE(full, msg);
        TEST_ASSERT_NULL_MESSAGE(naked,
                                 "Naked out param musí být NULL při escape");
    }
}


/* Bezpečné použití "." a ".." které NEvedou ven ze SD root. */
void test_unicard_jail_dot_segments(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);

    /* "subdir/./foo" -> SD_root/subdir/foo (".\" je no-op) */
    char *naked = NULL;
    char *full = unicard_jail_resolve_with_naked("subdir/./foo", &naked);
    assert_full_under_sd_root(full, "subdir",
                              "subdir/./foo musí projít (subdir komponenta)");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(full, "foo"),
                                 "subdir/./foo: foo komponenta musí být přítomna");
    baseui_tools_mem_free(full);
    baseui_tools_mem_free(naked);

    /* "subdir/sub2/../foo" -> SD_root/subdir/foo (".." popne sub2) */
    naked = NULL;
    full = unicard_jail_resolve_with_naked("subdir/sub2/../foo", &naked);
    assert_full_under_sd_root(full, "subdir",
                              "subdir/sub2/../foo: subdir komponenta musí zůstat");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(full, "foo"),
                                 "subdir/sub2/../foo: foo komponenta musí být finální");
    /* Pozitivní: výsledek NESMÍ obsahovat "sub2" (= správně popnuto). */
    TEST_ASSERT_NULL_MESSAGE(strstr(full, "sub2"),
                             "Po normalizaci nesmí v plné cestě zůstat sub2");
    baseui_tools_mem_free(full);
    baseui_tools_mem_free(naked);

    /* "./foo" -> SD_root/foo */
    naked = NULL;
    full = unicard_jail_resolve_with_naked("./foo", &naked);
    assert_full_under_sd_root(full, "foo", "./foo musí projít");
    baseui_tools_mem_free(full);
    baseui_tools_mem_free(naked);
}


/* Windows backslash traversal: "..\\foo" se musí brát jako "../foo"
 * (= escape) - jinak by Windows guest mohl obejít jail. */
void test_unicard_jail_backslash_traversal(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    uc_send_cmd(cmdRESET);

    /* Single "..\\foo" - escape ze SD root. */
    char *full = unicard_jail_resolve("..\\foo");
    TEST_ASSERT_NULL_MESSAGE(full,
        "Backslash \"..\\\\foo\" musí být detekován jako escape");

    /* Compound backslash traversal. */
    full = unicard_jail_resolve("subdir\\..\\..\\bar");
    TEST_ASSERT_NULL_MESSAGE(full,
        "subdir\\\\..\\\\..\\\\bar musí být detekován jako escape");

    /* Mixed slash + backslash escape. */
    full = unicard_jail_resolve("subdir/..\\..\\bar");
    TEST_ASSERT_NULL_MESSAGE(full,
        "Mixed slash/backslash traversal musí být escape");

    /* Pozitivní: jeden backslash subdir bez "..\\" musí projít.
     * Implementace přepíše backslash -> forward slash ještě před
     * normalizací, takže naked výsledek je /subdir/foo. */
    char *naked = NULL;
    full = unicard_jail_resolve_with_naked("subdir\\foo", &naked);
    assert_full_under_sd_root(full, "subdir",
        "subdir\\\\foo (bez escape) musí projít po normalizaci backslash");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(full, "foo"),
        "Backslash-separated cesta musí dát plný path s 'foo' suffixem");
    baseui_tools_mem_free(full);
    baseui_tools_mem_free(naked);
}


/* SD jail integration test: cmdSTAT s escape path nesmí dosáhnout
 * souboru, který fyzicky existuje VEN ze SD root.
 *
 * Postup:
 *   1. Vytvořit reálný soubor v parent dir SD root (= simulace "host"
 *      souboru, který by emu mohl chtít vidět)
 *   2. Poslat cmdSTAT s relativním "../escaped.txt"
 *   3. Status musí mít ERROR=1, ff_res = FR_NO_FILE (jail escape)
 *   4. Soubor po testu uklidit
 */
void test_unicard_jail_via_cmdSTAT(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* Vytvořit soubor "host_secret.txt" v parent dir SD root. */
    char *parent_dir = g_path_get_dirname(s_test_sd_root);
    char *secret_path = g_build_filename(parent_dir, "host_secret.txt", NULL);
    FILE *f = g_fopen(secret_path, "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "Test setup: musí jít vytvořit parent file");
    fputs("HOSTONLY", f);
    fclose(f);

    /* Verify že soubor opravdu existuje (= sanity check setupu). */
    TEST_ASSERT_TRUE_MESSAGE(g_file_test(secret_path, G_FILE_TEST_IS_REGULAR),
                             "Sanity: secret soubor musí existovat na hostu");

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdSTAT);
    uc_send_str("/../host_secret.txt"); /* escape attempt */

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_ERROR, sts & UNICARD_STS_ERROR,
        "cmdSTAT s escape path MUSÍ vrátit ERROR (jail blokuje)");

    /* Žádné DOUTRQ - klient nesmí dostat žádná data o tom souboru. */
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_DOUTRQ,
        "Při jail escape NESMÍ být DOUTRQ (=žádný FILINFO output)");

    /* ff_res = FR_NO_FILE (unicard_stat vrací FR_NO_FILE při jail escape). */
    uint8_t ff_res = uc_read_status_byte(3);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(FR_NO_FILE, ff_res,
        "ff_res po jail escape musí být FR_NO_FILE (ne OK + info)");

    /* Cleanup: smazat secret soubor. */
    g_remove(secret_path);
    g_free(secret_path);
    g_free(parent_dir);
}


/* ================================================================
 * UNIT TESTY - R/O režim emulované SD karty
 *
 * unicard_set_readonly(1) blokuje modifikační operace přes gate
 * v unicard_file_open (write/create flagy), unicard_file_write,
 * unicard_file_truncate, unicard_mkdir, unicard_unlink,
 * unicard_rename, unicard_utime. Read-only operace (FA_READ,
 * dir_open, stat) projdou.
 *
 * Helper teardown_readonly() obnoví R/O na 0 po každém testu, aby
 * nezasahoval do následujících tests (= žije v g_unicard.readonly
 * mezi setUp/tearDown).
 * ================================================================ */

/* Pomocný helper: vrátí R/O režim do default (off) po testu, aby
 * nezasahoval do dalších tests (g_unicard.readonly je persistentní
 * přes connect/disconnect). */
static void teardown_readonly(void)
{
    unicard_set_readonly(0);
}


/* R/O default po unicard_init() je 0 (= R/W). */
void test_unicard_readonly_default_off(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* setUp volá unicard_set_connected(CONNECTED) což interně volá
     * unicard_init - po něm musí být readonly=0. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, unicard_test_readonly(),
        "Default readonly po unicard_init musí být 0 (R/W)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_unicard.readonly,
        "g_unicard.readonly musí default 0");
}


/* R/O blokuje unicard_file_open s create/write flagy.
 * FR_WRITE_PROTECTED + soubor nesmí vzniknout na hostu. */
void test_unicard_readonly_blocks_write(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    unicard_set_readonly(1);

    /* Try file_open s FA_CREATE_NEW | FA_WRITE - musí selhat. */
    st_UNICARD_FILE file;
    unicard_file_init(&file);
    FRESULT r = unicard_file_open(&file, "/ro_blocked.bin",
                                  FA_CREATE_NEW | FA_WRITE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED, r,
        "file_open s CREATE_NEW|WRITE v R/O musí dát FR_WRITE_PROTECTED");

    /* Soubor NESMÍ vzniknout na host FS. */
    char *full = g_build_filename(s_test_sd_root, "ro_blocked.bin", NULL);
    TEST_ASSERT_FALSE_MESSAGE(g_file_test(full, G_FILE_TEST_EXISTS),
        "V R/O režimu nesmí vzniknout nový soubor");
    g_free(full);

    /* I FA_OPEN_ALWAYS (může vytvořit) musí být blokované. */
    r = unicard_file_open(&file, "/ro_blocked2.bin",
                          FA_OPEN_ALWAYS | FA_WRITE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED, r,
        "FA_OPEN_ALWAYS|FA_WRITE v R/O musí dát FR_WRITE_PROTECTED");

    /* FA_CREATE_ALWAYS taky. */
    r = unicard_file_open(&file, "/ro_blocked3.bin",
                          FA_CREATE_ALWAYS | FA_WRITE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED, r,
        "FA_CREATE_ALWAYS v R/O musí dát FR_WRITE_PROTECTED");

    teardown_readonly();
}


/* R/O blokuje unicard_mkdir. */
void test_unicard_readonly_blocks_mkdir(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    unicard_set_readonly(1);

    FRESULT r = unicard_mkdir("/ro_newdir");
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED, r,
        "mkdir v R/O režimu musí dát FR_WRITE_PROTECTED");

    /* Adresář nevznikl. */
    char *full = g_build_filename(s_test_sd_root, "ro_newdir", NULL);
    TEST_ASSERT_FALSE_MESSAGE(g_file_test(full, G_FILE_TEST_IS_DIR),
        "V R/O režimu nesmí vzniknout adresář");
    g_free(full);

    teardown_readonly();
}


/* R/O blokuje unicard_unlink. */
void test_unicard_readonly_blocks_unlink(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* Vytvořit soubor v R/W režimu. */
    create_test_file("ro_protected.bin", 25);

    unicard_set_readonly(1);

    FRESULT r = unicard_unlink("/ro_protected.bin");
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED, r,
        "unlink v R/O režimu musí dát FR_WRITE_PROTECTED");

    /* Soubor stále existuje. */
    char *full = g_build_filename(s_test_sd_root, "ro_protected.bin", NULL);
    TEST_ASSERT_TRUE_MESSAGE(g_file_test(full, G_FILE_TEST_IS_REGULAR),
        "Soubor po blokovaném unlinku musí stále existovat");
    g_free(full);

    teardown_readonly();
}


/* R/O blokuje unicard_rename. */
void test_unicard_readonly_blocks_rename(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("ro_rename_src.bin", 10);

    unicard_set_readonly(1);

    FRESULT r = unicard_rename("/ro_rename_src.bin", "/ro_rename_dst.bin");
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED, r,
        "rename v R/O režimu musí dát FR_WRITE_PROTECTED");

    /* Zdroj musí existovat, cíl ne. */
    char *src = g_build_filename(s_test_sd_root, "ro_rename_src.bin", NULL);
    char *dst = g_build_filename(s_test_sd_root, "ro_rename_dst.bin", NULL);
    TEST_ASSERT_TRUE_MESSAGE(g_file_test(src, G_FILE_TEST_EXISTS),
        "Zdroj rename musí stále existovat");
    TEST_ASSERT_FALSE_MESSAGE(g_file_test(dst, G_FILE_TEST_EXISTS),
        "Cíl rename nesmí existovat");
    g_free(src);
    g_free(dst);

    teardown_readonly();
}


/* R/O blokuje unicard_file_truncate.
 *
 * Postup: nejdřív v R/W otevřít soubor s FA_WRITE, pak zapnout R/O,
 * pokus truncate musí selhat s FR_WRITE_PROTECTED. (Stejně tak by
 * měl selhat samotný file_write na již otevřeném souboru.) */
void test_unicard_readonly_blocks_truncate(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("ro_trunc_target.bin", 100);

    st_UNICARD_FILE file;
    unicard_file_init(&file);
    FRESULT r = unicard_file_open(&file, "/ro_trunc_target.bin", FA_WRITE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_OK, r, "Open v R/W režimu musí projít");

    /* Zapnout R/O AFTER open. */
    unicard_set_readonly(1);

    r = unicard_file_truncate(&file);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED, r,
        "truncate v R/O režimu musí dát FR_WRITE_PROTECTED");

    /* Pojistka: write na existing otevřený soubor je taky blokovaný. */
    uint8_t data[5] = {1, 2, 3, 4, 5};
    uint32_t written = 0;
    r = unicard_file_write(&file, data, sizeof(data), &written);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED, r,
        "file_write v R/O režimu musí dát FR_WRITE_PROTECTED");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, written,
        "Žádné byty nesmí být zapsané");

    /* Cleanup: zavřít file, vypnout R/O, ověřit že soubor má původní velikost. */
    unicard_file_close(&file);

    char *full = g_build_filename(s_test_sd_root, "ro_trunc_target.bin", NULL);
    GStatBuf st;
    g_stat(full, &st);
    TEST_ASSERT_EQUAL_INT_MESSAGE(100, st.st_size,
        "Velikost souboru po blokovaném truncate musí zůstat 100 B");
    g_free(full);

    teardown_readonly();
}


/* R/O persistuje napříč session - opakované toggling se chová
 * konzistentně (= flag se mění, gate stále drží).
 *
 * Pozn.: g_warning vystavený přes unicard_warn_readonly_blocked se
 * z testu deterministicky nezachycuje (nemáme log capture v Unity),
 * proto kontrolujeme jen state machine: gate musí blokovat při R/O=1
 * a propouštět při R/O=0, opakovaně.
 */
void test_unicard_readonly_persists_session(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* Sekvence: R/W -> blokující operace projde, R/O -> blokuje,
     * R/W -> propouští, R/O -> blokuje znovu. */

    /* Krok 1: R/W, mkdir musí projít. */
    unicard_set_readonly(0);
    TEST_ASSERT_EQUAL_INT(FR_OK, unicard_mkdir("/persist_dir_a"));

    /* Krok 2: R/O on, mkdir blokovaný. */
    unicard_set_readonly(1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED,
        unicard_mkdir("/persist_dir_b"),
        "Po zapnutí R/O musí být mkdir blokovaný");

    /* Druhý write attempt - stále blokovaný (counter potlačí warning,
     * ale gate samotný drží). */
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED,
        unicard_mkdir("/persist_dir_c"),
        "Druhý write v R/O sessions stále blokovaný (gate trvá)");

    /* Soubor /persist_dir_b ani /persist_dir_c NESMÍ existovat. */
    char *b = g_build_filename(s_test_sd_root, "persist_dir_b", NULL);
    char *c = g_build_filename(s_test_sd_root, "persist_dir_c", NULL);
    TEST_ASSERT_FALSE(g_file_test(b, G_FILE_TEST_EXISTS));
    TEST_ASSERT_FALSE(g_file_test(c, G_FILE_TEST_EXISTS));
    g_free(b);
    g_free(c);

    /* Krok 3: R/O off, mkdir znovu projde. */
    unicard_set_readonly(0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_OK,
        unicard_mkdir("/persist_dir_d"),
        "Po vypnutí R/O musí mkdir znovu projít");

    /* Krok 4: R/O znovu on - znovu blokuje. */
    unicard_set_readonly(1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_WRITE_PROTECTED,
        unicard_mkdir("/persist_dir_e"),
        "Opakované R/O on musí znovu blokovat");

    teardown_readonly();
}


/* R/O režim NESMÍ blokovat read operace. Ověřuje že file_open
 * FA_READ, file_read, dir_open, dir_read_filelist a stat projdou
 * i při readonly=1. */
void test_unicard_readonly_allows_read(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* V R/W režimu vytvořit test soubor s daty 0..49. */
    create_test_file("ro_readable.bin", 50);

    /* Zapnout R/O - od teď žádné writes. */
    unicard_set_readonly(1);

    /* 1) file_open s FA_READ na existing musí projít. */
    st_UNICARD_FILE file;
    unicard_file_init(&file);
    FRESULT r = unicard_file_open(&file, "/ro_readable.bin", FA_READ);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_OK, r,
        "FA_READ open existing v R/O režimu MUSÍ projít");

    /* 2) file_read musí přečíst data. */
    uint8_t buf[64];
    uint32_t read_len = 0;
    r = unicard_file_read(&file, buf, 50, &read_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_OK, r,
        "file_read v R/O režimu musí projít");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(50, read_len,
        "Musí přečíst všech 50 bajtů");
    for (int i = 0; i < 50; i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)i, buf[i],
            "Obsah souboru musí být beze změny");
    }
    unicard_file_close(&file);

    /* 3) dir_open + dir_read_filelist musí projít. */
    st_UNICARD_DIR dir;
    unicard_dir_init(&dir);
    r = unicard_dir_open(&dir, "/");
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_OK, r,
        "dir_open v R/O režimu musí projít");

    char list_buf[256];
    r = unicard_dir_read_filelist(&dir, list_buf, sizeof(list_buf));
    TEST_ASSERT_EQUAL_INT_MESSAGE(FR_OK, r,
        "dir_read_filelist v R/O režimu musí projít");
    unicard_dir_close(&dir);

    /* 4) cmdSTAT na existing musí dát OK + DOUTRQ. */
    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdSTAT);
    uc_send_str("/ro_readable.bin");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
        "cmdSTAT existing v R/O režimu NESMÍ ERROR");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNICARD_STS_DOUTRQ, sts & UNICARD_STS_DOUTRQ,
        "cmdSTAT v R/O režimu musí dát DOUTRQ (info available)");

    /* Vyčíst FILINFO output aby se uvolnil DOUTRQ pro další testy. */
    int total_bytes = 23 + _MAX_LFN;
    for (int i = 0; i < total_bytes; i++) (void)uc_recv_data();

    teardown_readonly();
}


/* ================================================================
 * FatFS 8.3 SFN alias generátor (unicard_sfn) - čisté unit testy
 *
 * Očekávané hodnoty odvozeny ze zdroje FW Unicard (FatFS R0.13a,
 * create_name + gen_numname + get_fileinfo). Pole offset 9 = FatFS
 * FILINFO.altname.
 * ================================================================ */

/* Pomocník: vygeneruj alias pro jediné jméno (fresh kontext, daný styl). */
static void sfn_make_s(const char *name, en_UNICARD_SFN_STYLE style, char out[13])
{
    st_UNICARD_SFN_CTX ctx;
    unicard_sfn_ctx_init(&ctx);
    unicard_sfn_make(name, style, &ctx, out, NULL);
    unicard_sfn_ctx_clear(&ctx);
}

/* Pomocník: vrať has_lfn (jméno potřebuje LFN entry). Nezávisí na stylu. */
static gboolean sfn_has_lfn(const char *name)
{
    char out[13];
    gboolean hl = FALSE;
    st_UNICARD_SFN_CTX ctx;
    unicard_sfn_ctx_init(&ctx);
    unicard_sfn_make(name, UNICARD_SFN_STYLE_UC1, &ctx, out, &hl);
    unicard_sfn_ctx_clear(&ctx);
    return hl;
}

/* UC3 styl (Unicard3, FatFS R0.13a). */
static void sfn_single(const char *name, char out[13])
{
    sfn_make_s(name, UNICARD_SFN_STYLE_UC3, out);
}

void test_unicard_sfn_lossy_long_name(void)
{
    char out[13];
    sfn_single("Batman Demo.mzf", out);
    TEST_ASSERT_EQUAL_STRING("BATMAN~1.MZF", out);
}

void test_unicard_sfn_uc3_pure_83_uppercase_filled(void)
{
    /* "Opravený" uc3 plní pole S vždy 8.3 aliasem (velkými) - empty rule
     * (reálný FatFS by dal prázdné) záměrně NEpoužíváme. */
    char out[13];
    sfn_single("Y2K.MZF", out);
    TEST_ASSERT_EQUAL_STRING("Y2K.MZF", out);
    sfn_single("SAPO-P.MZF", out);
    TEST_ASSERT_EQUAL_STRING("SAPO-P.MZF", out);
}

void test_unicard_sfn_83_lowercase_uppercased(void)
{
    /* 8.3 jméno s malými písmeny -> má LFN entry, altname = velká 8.3. */
    char out[13];
    sfn_single("Gp-Simul.mzf", out);
    TEST_ASSERT_EQUAL_STRING("GP-SIMUL.MZF", out);
}

void test_unicard_sfn_space_in_name(void)
{
    /* Mezera v jméně -> lossy -> ~N alias. */
    char out[13];
    sfn_single("Picture Show.MZF", out);
    TEST_ASSERT_EQUAL_STRING("PICTUR~1.MZF", out);
}

void test_unicard_sfn_long_base(void)
{
    /* Základ delší než 8 znaků -> lossy -> ~N alias. */
    char out[13];
    sfn_single("Incopy102.mzf", out);
    TEST_ASSERT_EQUAL_STRING("INCOPY~1.MZF", out);
    sfn_single("Pool_3_V16k.mzf", out);
    TEST_ASSERT_EQUAL_STRING("POOL_3~1.MZF", out);
    sfn_single("Turbo_Copy_V1.21.mzf", out);
    TEST_ASSERT_EQUAL_STRING("TURBO_~1.MZF", out);
}

void test_unicard_sfn_collision_numbering(void)
{
    /* Dvě lossy jména se stejným 6-znak prefixem + ext dostanou ~1 a ~2. */
    char out[13];
    st_UNICARD_SFN_CTX ctx;
    unicard_sfn_ctx_init(&ctx);

    unicard_sfn_make("LongNameOne.txt", UNICARD_SFN_STYLE_UC3, &ctx, out, NULL);
    TEST_ASSERT_EQUAL_STRING("LONGNA~1.TXT", out);

    unicard_sfn_make("LongNameTwo.txt", UNICARD_SFN_STYLE_UC3, &ctx, out, NULL);
    TEST_ASSERT_EQUAL_STRING("LONGNA~2.TXT", out);

    unicard_sfn_ctx_clear(&ctx);
}

/* has_lfn: jméno potřebuje LFN entry jen když není čisté 8.3 (lossy /
 * smíšená velikost / non-ASCII). Reálné HW podle toho plní pole LFN. */
void test_unicard_sfn_has_lfn_flag(void)
{
    /* Mají LFN: smíšená velikost a lossy. */
    TEST_ASSERT_TRUE(sfn_has_lfn("Bomber.mzf"));        /* mixed case */
    TEST_ASSERT_TRUE(sfn_has_lfn("Batman Demo.mzf"));   /* lossy */
    TEST_ASSERT_TRUE(sfn_has_lfn("Gp-Simul.mzf"));      /* mixed case */
    /* Nemají LFN: čistě 8.3 uniform case (velká i malá). */
    TEST_ASSERT_FALSE(sfn_has_lfn("SAPO-P.MZF"));       /* upper 8.3 */
    TEST_ASSERT_FALSE(sfn_has_lfn("Y2K.MZF"));          /* upper 8.3 */
    TEST_ASSERT_FALSE(sfn_has_lfn("seesharp.mzf"));     /* lower 8.3 */
}

/* UC1 styl (MZ800UKP1, FatFS R0.09): offset 9 vždy vyplněné, 8.3 s case.
 * Očekávané hodnoty odvozeny z reálné karty (fotka FW1) + R0.09 zdroje. */
void test_unicard_sfn_uc1_pure_83_filled(void)
{
    /* Reálná uc1 karta plní offset 9 i pro čisté 8.3 (na rozdíl od uc3). */
    char out[13];
    sfn_make_s("Y2K.MZF", UNICARD_SFN_STYLE_UC1, out);
    TEST_ASSERT_EQUAL_STRING("Y2K.MZF", out);
    sfn_make_s("SAPO-P.MZF", UNICARD_SFN_STYLE_UC1, out);
    TEST_ASSERT_EQUAL_STRING("SAPO-P.MZF", out);
}

void test_unicard_sfn_uc1_lowercase_preserved(void)
{
    /* uc1 zachová malá písmena (NT case flag): seesharp.mzf zůstane malými. */
    char out[13];
    sfn_make_s("seesharp.mzf", UNICARD_SFN_STYLE_UC1, out);
    TEST_ASSERT_EQUAL_STRING("seesharp.mzf", out);
}

void test_unicard_sfn_uc1_mixed_uppercased(void)
{
    /* Smíšená velikost (mixed) nemá uniform NT flag -> velká písmena. */
    char out[13];
    sfn_make_s("Gp-Simul.mzf", UNICARD_SFN_STYLE_UC1, out);
    TEST_ASSERT_EQUAL_STRING("GP-SIMUL.MZF", out);
}

void test_unicard_sfn_uc1_lossy(void)
{
    /* Lossy jména mají ~N alias stejně jako uc3 (velkými). */
    char out[13];
    sfn_make_s("Batman Demo.mzf", UNICARD_SFN_STYLE_UC1, out);
    TEST_ASSERT_EQUAL_STRING("BATMAN~1.MZF", out);
    sfn_make_s("Incopy102.mzf", UNICARD_SFN_STYLE_UC1, out);
    TEST_ASSERT_EQUAL_STRING("INCOPY~1.MZF", out);
}

/* Integrace: cmdSTAT na lossy jméno musí v poli offset 9 (8.3 short)
 * vrátit FatFS alias, ne uťatý LFN. */
void test_unicard_cmdSTAT_sfn_alias(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    create_test_file("Batman Demo.mzf", 100);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdSTAT);
    uc_send_str("/Batman Demo.mzf");

    uint8_t sts = uc_master_status();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, sts & UNICARD_STS_ERROR,
                                    "STAT existujícího souboru nesmí ERROR");

    /* skip fsize(4)+fdate(2)+ftime(2)+fattrib(1) = 9 bytů */
    for (int i = 0; i < 9; i++) (void)uc_recv_data();
    char fname[13];
    for (int i = 0; i < 13; i++) fname[i] = (char)uc_recv_data();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("BATMAN~1.MZF", fname,
        "offset 9 musí být FatFS 8.3 alias, ne uťatý LFN");

    /* drain zbytek (lfn_strlen + lfname) aby se uvolnil DOUTRQ */
    (void)uc_recv_data();
    for (int i = 0; i < _MAX_LFN; i++) (void)uc_recv_data();
}

/* Integrace: cmdREADDIR musí v poli offset 9 vrátit FatFS alias. */
void test_unicard_cmdREADDIR_sfn_alias(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *sub = g_build_filename(s_test_sd_root, "sfndir", NULL);
    g_mkdir(sub, 0777);
    g_free(sub);
    create_test_file("sfndir/Batman Demo.mzf", 100);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdREADDIR);
    uc_send_str("/sfndir");

    int found = 0;
    for (int iter = 0; iter < 100; iter++) {
        uint8_t sts = uc_master_status();
        if (!(sts & UNICARD_STS_READDIR)) break;

        (void)uc_recv_dword_le(); /* fsize */
        (void)uc_recv_word_le();  /* fdate */
        (void)uc_recv_word_le();  /* ftime */
        (void)uc_recv_data();     /* fattrib */
        char fname[13];
        for (int i = 0; i < 13; i++) fname[i] = (char)uc_recv_data();
        (void)uc_recv_data();     /* lfn_strlen */
        for (int i = 0; i < _MAX_LFN; i++) (void)uc_recv_data(); /* lfname */

        if (strcmp(fname, "BATMAN~1.MZF") == 0) found = 1;
    }

    TEST_ASSERT_TRUE_MESSAGE(found,
        "READDIR offset 9 musí obsahovat FatFS alias BATMAN~1.MZF");
}

/* Integrace uc3: off 23 = primární jméno (fname) - VŽDY vyplněné, i pro
 * čisté 8.3 (SAPO-P). off 9 (altname) je naopak u čistě-VELKÝCH 8.3
 * prázdné -> jméno nese pole L. (uc3 default; položky podle fsize.) */
void test_unicard_cmdREADDIR_uc3_lfn_always(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *sub = g_build_filename(s_test_sd_root, "lfndir", NULL);
    g_mkdir(sub, 0777);
    g_free(sub);
    create_test_file("lfndir/SAPO-P.MZF", 111);    /* čisté 8.3 UPPER */
    create_test_file("lfndir/Bomber.mzf", 222);    /* mixed case */

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdREADDIR);
    uc_send_str("/lfndir");

    int seen_sapo = 0, seen_bomber = 0;
    for (int iter = 0; iter < 100; iter++) {
        uint8_t sts = uc_master_status();
        if (!(sts & UNICARD_STS_READDIR)) break;

        uint32_t fsize = uc_recv_dword_le();
        (void)uc_recv_word_le();  /* fdate */
        (void)uc_recv_word_le();  /* ftime */
        (void)uc_recv_data();     /* fattrib */
        char sbuf[13];
        for (int i = 0; i < 13; i++) sbuf[i] = (char)uc_recv_data();  /* S (off 9) */
        sbuf[12] = '\0';
        uint8_t len = uc_recv_data();   /* LEN (off 22) */
        char lfname[33];
        for (int i = 0; i < 32; i++) lfname[i] = (char)uc_recv_data();
        lfname[32] = '\0';

        if (fsize == 111) {
            seen_sapo = 1;
            /* uc3 (opravený): off 23 = primární jméno, off 9 = 8.3 alias
             * (vždy vyplněné). */
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(strlen("SAPO-P.MZF"), len, "SAPO-P uc3: LEN=délka jména");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("SAPO-P.MZF", lfname, "SAPO-P uc3: L=primární jméno");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("SAPO-P.MZF", sbuf, "SAPO-P uc3: S=8.3 alias (vždy)");
        } else if (fsize == 222) {
            seen_bomber = 1;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(strlen("Bomber.mzf"), len, "Bomber uc3: LEN");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("Bomber.mzf", lfname, "Bomber uc3: L=primární jméno");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(seen_sapo, "READDIR musí vidět SAPO-P.MZF");
    TEST_ASSERT_TRUE_MESSAGE(seen_bomber, "READDIR musí vidět Bomber.mzf");
}

/* Integrace uc1: off 23 = LFN - plněno JEN pro jména s LFN entry. Čisté
 * 8.3 (SAPO-P) má L prázdné + LEN=0 (jméno nese pole S); mixed (Bomber)
 * má L=LFN. Přepíná FW na uc1 (save/restore). */
void test_unicard_cmdREADDIR_uc1_lfn_conditional(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    if (g_mzarch_platform_numeric != 800) {
        TEST_IGNORE_MESSAGE("uc1 firmware jen na MZ-800");
        return;
    }

    en_UNICARD_FW saved_fw = unicard_get_fw();
    unicard_set_fw(UNICARD_FW_UC1);

    char *sub = g_build_filename(s_test_sd_root, "uc1lfn", NULL);
    g_mkdir(sub, 0777);
    g_free(sub);
    create_test_file("uc1lfn/SAPO-P.MZF", 111);
    create_test_file("uc1lfn/Bomber.mzf", 222);

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdREADDIR);
    uc_send_str("/uc1lfn");

    int seen_sapo = 0, seen_bomber = 0;
    for (int iter = 0; iter < 100; iter++) {
        uint8_t sts = uc_master_status();
        if (!(sts & UNICARD_STS_READDIR)) break;

        uint32_t fsize = uc_recv_dword_le();
        (void)uc_recv_word_le();
        (void)uc_recv_word_le();
        (void)uc_recv_data();
        char sbuf[13];
        for (int i = 0; i < 13; i++) sbuf[i] = (char)uc_recv_data();  /* S (off 9) */
        sbuf[12] = '\0';
        uint8_t len = uc_recv_data();
        char lfname[33];
        for (int i = 0; i < 32; i++) lfname[i] = (char)uc_recv_data();
        lfname[32] = '\0';

        if (fsize == 111) {
            seen_sapo = 1;
            /* uc1: off 23 prázdné (bez LFN), jméno v S (off 9). */
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, len, "SAPO-P uc1: LEN=0");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", lfname, "SAPO-P uc1: L prázdné");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("SAPO-P.MZF", sbuf, "SAPO-P uc1: S=8.3 jméno");
        } else if (fsize == 222) {
            seen_bomber = 1;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(strlen("Bomber.mzf"), len, "Bomber uc1: LEN");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("Bomber.mzf", lfname, "Bomber uc1: L=LFN");
        }
    }

    unicard_set_fw(saved_fw);

    TEST_ASSERT_TRUE_MESSAGE(seen_sapo, "READDIR musí vidět SAPO-P.MZF");
    TEST_ASSERT_TRUE_MESSAGE(seen_bomber, "READDIR musí vidět Bomber.mzf");
}

/* Integrace uc1: v podadresáři posílá ".." jako první položku; pole S má
 * 8.3 s case, L jen pro jména s LFN. Přepíná FW na uc1 (save/restore). */
void test_unicard_cmdREADDIR_uc1_dotdot(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);
    if (g_mzarch_platform_numeric != 800) {
        TEST_IGNORE_MESSAGE("uc1 firmware jen na MZ-800");
        return;
    }

    en_UNICARD_FW saved_fw = unicard_get_fw();
    unicard_set_fw(UNICARD_FW_UC1);

    char *sub = g_build_filename(s_test_sd_root, "uc1dir", NULL);
    g_mkdir(sub, 0777);
    g_free(sub);
    create_test_file("uc1dir/seesharp.mzf", 333);  /* čistě malá 8.3 */

    uc_send_cmd(cmdRESET);
    uc_send_cmd(cmdREADDIR);
    uc_send_str("/uc1dir");

    int dotdot_first = -1, idx = 0;
    int seen_lower = 0;
    for (int iter = 0; iter < 100; iter++) {
        uint8_t sts = uc_master_status();
        if (!(sts & UNICARD_STS_READDIR)) break;

        (void)uc_recv_dword_le(); /* fsize */
        (void)uc_recv_word_le();  /* fdate */
        (void)uc_recv_word_le();  /* ftime */
        (void)uc_recv_data();     /* fattrib */
        char fname[13];
        for (int i = 0; i < 13; i++) fname[i] = (char)uc_recv_data();
        (void)uc_recv_data();     /* LEN */
        for (int i = 0; i < 32; i++) (void)uc_recv_data();  /* L */

        if (strcmp(fname, "..") == 0 && dotdot_first < 0) dotdot_first = idx;
        if (strcmp(fname, "seesharp.mzf") == 0) seen_lower = 1;  /* uc1 S = malými */
        idx++;
    }

    unicard_set_fw(saved_fw);  /* restore */

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, dotdot_first,
        "uc1 musí poslat '..' jako PRVNÍ položku v podadresáři");
    TEST_ASSERT_TRUE_MESSAGE(seen_lower,
        "uc1 S pole musí mít malá písmena (seesharp.mzf)");
}


/* ================================================================
 * MAIN
 * ================================================================ */

int main(int argc, char *argv[])
{
    mztest_parse_args(argc, argv);
    mztest_init();

    UNITY_BEGIN();
    RUN_TEST(test_unicard_setup_creates_sd_structure);
    RUN_TEST(test_unicard_smoke_cmdRESET);
    RUN_TEST(test_unicard_smoke_cmdREV);
    RUN_TEST(test_unicard_smoke_cmdGETCWD);
    RUN_TEST(test_unicard_cmdGETCWD_subdir_single_slash);
    RUN_TEST(test_unicard_parser_BS_via_cmdOPEN);
    RUN_TEST(test_unicard_parser_BS_open_existing);
    RUN_TEST(test_unicard_cmdSIZE);
    RUN_TEST(test_unicard_cmdTELL);
    RUN_TEST(test_unicard_cmdSTAT_file);
    RUN_TEST(test_unicard_cmdSTAT_dir);
    RUN_TEST(test_unicard_cmdSTAT_missing);
    RUN_TEST(test_unicard_cmdGETFREE);
    RUN_TEST(test_unicard_cmdSEEK_set);
    RUN_TEST(test_unicard_cmdSEEK_end);
    RUN_TEST(test_unicard_cmdSEEK_rel);
    RUN_TEST(test_unicard_cmdSEEK_no_file);
    RUN_TEST(test_unicard_write_create_new);
    RUN_TEST(test_unicard_write_existing_FA_WRITE);
    RUN_TEST(test_unicard_write_FA_CREATE_ALWAYS_new);
    RUN_TEST(test_unicard_write_FA_CREATE_ALWAYS_truncate);
    RUN_TEST(test_unicard_write_FA_OPEN_ALWAYS_existing_preserves);
    RUN_TEST(test_unicard_write_FA_OPEN_ALWAYS_creates_new);
    RUN_TEST(test_unicard_cmdTRUNC);
    RUN_TEST(test_unicard_cmdSYNC);
    RUN_TEST(test_unicard_cmdMKDIR);
    RUN_TEST(test_unicard_cmdMKDIR_exists);
    RUN_TEST(test_unicard_cmdUNLINK_file);
    RUN_TEST(test_unicard_cmdUNLINK_missing);
    RUN_TEST(test_unicard_cmdRENAME);
    RUN_TEST(test_unicard_cmdCHMOD_noop);
    RUN_TEST(test_unicard_cmdCHMOD_missing);
    RUN_TEST(test_unicard_cmdUTIME);
    RUN_TEST(test_unicard_cmdUTIME_missing);
    RUN_TEST(test_unicard_cmdREADDIR);
    RUN_TEST(test_unicard_cmdREADDIR_dir_attribute);

    /* FatFS 8.3 SFN alias generátor (unicard_sfn). */
    RUN_TEST(test_unicard_sfn_lossy_long_name);
    RUN_TEST(test_unicard_sfn_uc3_pure_83_uppercase_filled);
    RUN_TEST(test_unicard_sfn_83_lowercase_uppercased);
    RUN_TEST(test_unicard_sfn_space_in_name);
    RUN_TEST(test_unicard_sfn_long_base);
    RUN_TEST(test_unicard_sfn_collision_numbering);
    RUN_TEST(test_unicard_sfn_uc1_pure_83_filled);
    RUN_TEST(test_unicard_sfn_uc1_lowercase_preserved);
    RUN_TEST(test_unicard_sfn_uc1_mixed_uppercased);
    RUN_TEST(test_unicard_sfn_uc1_lossy);
    RUN_TEST(test_unicard_sfn_has_lfn_flag);
    RUN_TEST(test_unicard_cmdSTAT_sfn_alias);
    RUN_TEST(test_unicard_cmdREADDIR_sfn_alias);
    RUN_TEST(test_unicard_cmdREADDIR_uc3_lfn_always);
    RUN_TEST(test_unicard_cmdREADDIR_uc1_lfn_conditional);
    RUN_TEST(test_unicard_cmdREADDIR_uc1_dotdot);

    /* SD jail (directory traversal protection). */
    RUN_TEST(test_unicard_jail_basic_paths);
    RUN_TEST(test_unicard_jail_escape_blocked);
    RUN_TEST(test_unicard_jail_dot_segments);
    RUN_TEST(test_unicard_jail_backslash_traversal);
    RUN_TEST(test_unicard_jail_via_cmdSTAT);

    /* R/O režim emulované SD karty. */
    RUN_TEST(test_unicard_readonly_default_off);
    RUN_TEST(test_unicard_readonly_blocks_write);
    RUN_TEST(test_unicard_readonly_blocks_mkdir);
    RUN_TEST(test_unicard_readonly_blocks_unlink);
    RUN_TEST(test_unicard_readonly_blocks_rename);
    RUN_TEST(test_unicard_readonly_blocks_truncate);
    RUN_TEST(test_unicard_readonly_persists_session);
    RUN_TEST(test_unicard_readonly_allows_read);

    int result = UNITY_END();

    mztest_teardown();
    return result;
}
