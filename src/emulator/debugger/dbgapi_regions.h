/*
 * dbgapi_regions.h - inventář paměťových regionů pro Memory Browser
 *
 * Poskytuje banking-aware enumeraci všech paměťových regionů aktuální
 * architektury (MZ-800/MZ-700/MZ-1500) plus no-side-effect read a safe
 * write per region.
 *
 * Architektonický kontext:
 *   - Per-arch implementace v src/emulator/mzarch/<arch>/debugger/<arch>_regions.c
 *   - Společné jádro v src/emulator/debugger/dbgapi_regions.c dispatchuje
 *     přes compile-time MZARCH a sdílí kód pro read/write (Memext, Ramdisk,
 *     VRAM plane, PROHIBITED shadow).
 *   - API je určeno pro Memory Browser, Watch okno a Cheat Search; volá se
 *     z debug vlákna (UI side) - není v emulátor hot path.
 *
 * ID stabilita:
 *   region.id je stabilní v rámci jedné session (pořadí zápisu při enumerate).
 *   Klient nesmí persistovat raw `id` mezi spuštěními. Pro persistenci je
 *   doporučeno persistovat dvojici (kind, sub_id) a při restartu znovu
 *   resolvovat na aktuální id přes enumerate.
 *
 * No-side-effect contract:
 *   - read: nikdy nesmí měnit chip state (auto-inc latch, GDG RF dispatch,
 *     IRQ trigger, atd.). Přímý array access nebo speciální *_no_se varianty.
 *   - enumerate: čte g_memory.map, HW connection flagy a vrací snapshot;
 *     bez side-effectu na běžící HW.
 *
 * Ownership:
 *   - st_REGION_DESC pole alokuje caller a předá pres out parametr; modul
 *     pouze zapisuje a nealokuje.
 *   - Vrácená data jsou snapshot - po návratu z funkce může klient držet
 *     bez další synchronizace.
 *
 * Licence: GPLv3
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

#ifndef DBGAPI_REGIONS_H
#define DBGAPI_REGIONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Druh paměťového regionu.
 *
 * Per arch je viditelná pouze podmnožina (např. PCG_1500 jen na MZ-1500,
 * VRAM_PHYS_PLANE jen na MZ-800). Caller musí umět zpracovat jakoukoliv
 * hodnotu, kterou enumerate vrátí.
 *
 * sub_id v st_REGION_DESC disambiguuje regiony stejného druhu (plane index,
 * bank index, PEZIK instance).
 */
typedef enum en_REGION_KIND
{
    REGION_KIND_LOGICAL = 0,            /**< Současný Z80 view (banking-aware). */
    REGION_KIND_RAM,                    /**< Plná 64 KB user DRAM jak ji vidí CPU pro RAM read/write (čte/zapisuje přes memram_read/write[] indirection, ne přímo do g_memory.RAM[] pole). S připojeným Memextem se data čtou/zapisují do aktivní memext bank; bez Memextu identické s g_memory.RAM[]. */
    REGION_KIND_ROM_LOWER,              /**< Monitor ROM 0x0000-0x0FFF (4 KB). */
    REGION_KIND_ROM_UPPER,              /**< Monitor ROM 0xE000-0xFFFF (8 KB). */
    REGION_KIND_CGROM,                  /**< CG-ROM (4 KB). MZ-800 native v 0x1000-0x1FFF, MZ-1500 přes SPEC=1. */
    REGION_KIND_CGRAM_700,              /**< CG-RAM MZ-700 (4 KB). MZ-800 v 700 compat módu. */
    REGION_KIND_VRAM_700_CHAR,          /**< Znaková VRAM MZ-700 (1 KB v 0xD000-0xD3FF). */
    REGION_KIND_VRAM_700_ATTR,          /**< Atributová VRAM MZ-700 (1 KB v 0xD800-0xDBFF). */
    REGION_KIND_VRAM_PHYS_PLANE,        /**< MZ-800 fyzická plane I-IV (8 KB raw), sub_id = 0..3. */
    REGION_KIND_PCG_1500,               /**< MZ-1500 PCG bank 1..3 (8 KB), sub_id = 0..2. */
    REGION_KIND_MEMEXT_RAM,             /**< Memext RAM bank (4 KB), sub_id = raw bank index 0..0x7F (Luftner) nebo 0..0x3F (PEHU). */
    REGION_KIND_MEMEXT_FLASH,           /**< Memext Luftner FLASH bank (4 KB), sub_id = raw bank index 0x80..0xFF. */
    REGION_KIND_RAMDISK_STD,            /**< Ramdisk STD bank (64 KB), sub_id = bank index 0..255. */
    REGION_KIND_RAMDISK_PEZIK,          /**< Ramdisk PEZIK bank (64 KB), sub_id = pezik_instance * 8 + bank_idx (0..7 = port 0x68 banks 0..7, 8..15 = port 0xE8 banks 0..7). */
    REGION_KIND_PROHIBITED_SHADOW,      /**< Virtuální shadow region (vrací 0x1A) když PROHIBITED mode aktivní. */
    REGION_KIND_COUNT                   /**< Sentinel - počet kindů. */
} en_REGION_KIND;

/**
 * @brief Popis jednoho paměťového regionu.
 *
 * Vrací se z dbgapi_regions_enumerate() jako pole snapshotů. ID se používá
 * pro následující read/write volání ve stejné session.
 *
 * Invariant:
 *   - id je unikátní 0..N-1 v jedné enumerate iteraci
 *   - name je '\0'-terminated ASCII (anglicky, pro i18n přes _() v UI vrstvě)
 *   - size > 0 vždy
 *   - logical_base = 0xFFFFFFFF znamená "region není mapovaný do Z80 prostoru
 *     ve výchozím stavu" (např. fyzická plane, off-bank, PROHIBITED shadow)
 */
typedef struct st_REGION_DESC
{
    int             id;             /**< Stable ID 0..N-1 v rámci session. */
    en_REGION_KIND  kind;           /**< Druh regionu. */
    int             sub_id;         /**< Disambiguator (plane/bank index, PEZIK instance). */
    char            name[64];       /**< ASCII jméno (anglicky, klíč pro _() v UI). */
    uint32_t        logical_base;   /**< Z80 adresa kde region "obvykle" sedí, nebo 0xFFFFFFFF. */
    uint32_t        size;           /**< Velikost regionu v bajtech. */
    int             writable;       /**< 1 = lze do něj psát přes dbgapi_regions_write, 0 = R-only. */
    int             connected;      /**< 1 = HW připojen / data validní, 0 = volitelný HW odpojen. */
    int             mapped_now;     /**< 1 = aktuálně viditelný v Z80 prostoru (banking match), 0 = off-bank. */
} st_REGION_DESC;

/**
 * @brief Enumeruje všechny paměťové regiony aktuální architektury.
 *
 * Vrací snapshot dle aktuálního banking stavu (`g_memory.map`, GDG DMD) a
 * connection flagů volitelného HW (Memext, Ramdisk STD, PEZIK 68/E8).
 *
 * Per arch je seznam regionů ohraničený:
 *   - MZ-800: LOGICAL, RAM, ROM_LOWER, ROM_UPPER, CGROM, CGRAM_700,
 *             VRAM_700_CHAR/ATTR (jen v 700 compat módu), 4× VRAM_PHYS_PLANE
 *             (plane III/IV vždy enumerované - EXVRAM pole je vždy alokované
 *             v st_MEMORY), MEMEXT_RAM/FLASH banks (pokud connected),
 *             RAMDISK_STD banks (pokud connected), RAMDISK_PEZIK 0/1 (pokud
 *             příslušná instance connected), PROHIBITED_SHADOW (pokud aktivní).
 *   - MZ-700: LOGICAL, RAM, ROM_LOWER, ROM_UPPER, CGRAM_700, VRAM_700_CHAR/ATTR,
 *             PROHIBITED_SHADOW (pokud aktivní). Bez Memext/Ramdisk/VRAM plane.
 *   - MZ-1500: LOGICAL, RAM, ROM_LOWER, ROM_UPPER, CGROM, VRAM_700_CHAR/ATTR,
 *              3× PCG_1500, MEMEXT_RAM/FLASH banks (pokud connected; MZ-1500
 *              má port 0xE7 jako MZ-800). PROHIBITED není MZ-1500 koncept;
 *              nepřidává se.
 *
 * No-side-effect: čte pouze stav HW (banking, connection), nezasahuje do
 * chip state. Vhodné pro UI thread polling.
 *
 * @param[out] out         Pole st_REGION_DESC alokované callerem.
 * @param[in]  max_count   Maximální počet zapsaných položek (= velikost pole).
 * @return                 Skutečný počet zapsaných položek (0..max_count).
 *                         Při out == NULL nebo max_count <= 0 vrací -1.
 *
 * @note ID v out[i].id = i. Caller může spoléhat na 0-based pořadí v rámci
 *       jednoho volání. Mezi voláními (nebo po HW reconfigure - např. po
 *       Memext connect/disconnect) se pořadí může změnit; klient si nesmí
 *       cache-ovat ID mezi enumerate calls.
 */
int dbgapi_regions_enumerate(st_REGION_DESC *out, int max_count);

/**
 * @brief No-side-effect read z regionu.
 *
 * Čte len bajtů z (region_id, offset). Žádný auto-inc latch, žádný GDG RF
 * dispatch, žádný IRQ trigger. Pro REGION_KIND_LOGICAL volá per-arch
 * memory_read_byte_no_se(), pro fyzické regiony přímý array access.
 *
 * Pokud `offset + len` přesáhne velikost regionu, čte se jen do konce
 * regionu (clamp) a vrací se zkrácená délka.
 *
 * @param[in]  region_id   ID získané z dbgapi_regions_enumerate(). Musí
 *                         odpovídat poslední enumerate volání (stabilita
 *                         per session).
 * @param[in]  offset      Offset v rámci regionu (0..size-1).
 * @param[out] out         Buffer pro výsledek; min `len` bajtů.
 * @param[in]  len         Počet bajtů k přečtení.
 * @return                 Počet skutečně přečtených bajtů (0..len), -1 při
 *                         chybě: out=NULL, len=0, region_id mimo rozsah,
 *                         offset >= size, region disconnected.
 *
 * @pre Caller musel zavolat dbgapi_regions_enumerate() a získat platné ID.
 *      Mezi enumerate a read se nesmí HW rekonfigurovat (jinak ID neplatí).
 * @post Žádný chip state se nezměnil.
 */
int dbgapi_regions_read(int region_id, uint32_t offset, uint8_t *out, uint32_t len);

/**
 * @brief Zápis do regionu (respekt writable flagů).
 *
 * Pro REGION_KIND_MEMEXT_FLASH vrací -1 (FLASH je R-only z pohledu Memory
 * Browseru - CPU-level WOM redirect je interní HW emulace a do debug API
 * se nepromítá).
 * Pro REGION_KIND_PROHIBITED_SHADOW vrací -1 (virtual region).
 * Pro VRAM regiony nedělá automatický screen refresh - to je UI zodpovědnost
 * při edit v pause modu.
 *
 * @param[in] region_id   ID získané z dbgapi_regions_enumerate().
 * @param[in] offset      Offset v rámci regionu.
 * @param[in] data        Buffer s daty k zapsání.
 * @param[in] len         Počet bajtů k zapsání.
 * @return                Počet skutečně zapsaných bajtů (0..len), -1 při
 *                        chybě: data=NULL, len=0, region_id mimo rozsah,
 *                        offset >= size, region disconnected, region R-only.
 *
 * @pre Region musí být writable a connected.
 * @post Při úspěchu jsou data zapsaná do paměti; ostatní state nedotčen
 *       (banking, latch, atd.).
 */
int dbgapi_regions_write(int region_id, uint32_t offset, const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* DBGAPI_REGIONS_H */
