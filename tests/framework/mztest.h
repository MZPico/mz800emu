/*
 * mztest.h — testovací wrapper pro emulátor MZ-800/700/1500
 *
 * Poskytuje inicializaci/teardown globálního stavu emulátoru
 * pro headless (bez SDL/ImGui) testování s frameworkem Unity.
 *
 * Licence: GPLv3
 */

#ifndef MZTEST_H
#define MZTEST_H

#include "unity.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Testovací úrovně
 */
#define MZTEST_LEVEL_SMOKE  1   /* základní smoke testy (~sekundy) */
#define MZTEST_LEVEL_UNIT   2   /* unit testy (~desítky sekund) */
#define MZTEST_LEVEL_FULL   3   /* kompletní sada (~minuty) */

/* aktuální testovací úroveň (nastavuje se přes --level) */
extern int mztest_current_level;

/* verbose výstup */
extern int mztest_verbose;

/*
 * Makro pro přeskočení testu pokud je úroveň nižší než požadovaná
 */
#define MZTEST_REQUIRE_LEVEL(required_level) \
    do { \
        if (mztest_current_level < (required_level)) { \
            TEST_IGNORE_MESSAGE("Přeskočeno (vyžaduje vyšší test level)"); \
            return; \
        } \
    } while (0)

/*
 * Inicializace a teardown
 */

/* Inicializace globálního stavu emulátoru v headless režimu.
 * Volá cfgmain_init, memory_init, mzarch_platform_fn_init atd.
 * Nespouští SDL okna, ImGui ani emulátorové vlákno.
 */
void mztest_init(void);

/* Úklid — uvolnění alokované paměti, zavření souborů */
void mztest_teardown(void);

/* Zpracování CLI argumentů (--level, --verbose) */
void mztest_parse_args(int argc, char *argv[]);

/*
 * Logging
 */
#define MZTEST_LOG(fmt, ...) \
    do { if (mztest_verbose) fprintf(stderr, "[mztest] " fmt "\n", ##__VA_ARGS__); } while (0)

#ifdef __cplusplus
}
#endif

#endif /* MZTEST_H */
