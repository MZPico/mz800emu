/*
 * json_test_helpers.h - sdílené helpery pro JSON asserty v testech
 *
 * json-glib pretty print formátuje "klíč" : hodnota (= mezera před i po
 * dvojtečce). Předchozí "manual" JSON používal "klíč": hodnota (= mezera
 * jen po dvojtečce). Tyto helpery tolerují obě varianty pro testy které
 * historicky kontrolovaly přesné textové formáty.
 *
 * Header-only. Stačí #include "json_test_helpers.h" v test souboru.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 */
#ifndef JSON_TEST_HELPERS_H
#define JSON_TEST_HELPERS_H

#include <string.h>
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tolerantní JSON klíč:hodnota substring kontrola.
 *
 * Akceptuje obě varianty oddělovače:
 *   - "klíč": hodnota   (= python json.dumps(indent=N) styl, manual emit)
 *   - "klíč" : hodnota  (= json-glib pretty print styl)
 *
 * Caller předá needle bez dvojtečky, např. (haystack, "subsys", "\"cputrack\"")
 * a interně se postaví obě formy a hledá strstr().
 *
 * @param haystack  Text k prohledání (= načtený JSON soubor).
 * @param key       Klíč bez uvozovek (= "subsys", ne "\"subsys\"").
 * @param value     Hodnota včetně případných uvozovek/závorek
 *                  (= "\"cputrack\"" pro string, "5" pro int, "[" pro
 *                  array začátek, "false" pro bool).
 *
 * @return 1 pokud nalezeno v některé formě, 0 jinak.
 */
static inline int json_kv_present(const char *haystack,
                                    const char *key,
                                    const char *value)
{
    if (!haystack || !key || !value) return 0;
    char buf1[256];
    char buf2[256];
    g_snprintf(buf1, sizeof(buf1), "\"%s\": %s",  key, value);
    g_snprintf(buf2, sizeof(buf2), "\"%s\" : %s", key, value);
    return (strstr(haystack, buf1) || strstr(haystack, buf2)) ? 1 : 0;
}

/**
 * @brief Tolerantní substring kontrola jen na přítomnost klíče.
 *
 * Pro testy které nezajímá hodnota, jen zda klíč existuje. Akceptuje:
 *   - "klíč":   (= manual emit styl)
 *   - "klíč" :  (= json-glib pretty styl)
 *
 * @param haystack  Text k prohledání.
 * @param key       Klíč bez uvozovek.
 *
 * @return 1 pokud klíč nalezen, 0 jinak.
 */
static inline int json_key_present(const char *haystack, const char *key)
{
    if (!haystack || !key) return 0;
    char buf1[256];
    char buf2[256];
    g_snprintf(buf1, sizeof(buf1), "\"%s\":",  key);
    g_snprintf(buf2, sizeof(buf2), "\"%s\" :", key);
    return (strstr(haystack, buf1) || strstr(haystack, buf2)) ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* JSON_TEST_HELPERS_H */
