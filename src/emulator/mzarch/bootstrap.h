#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include <stdint.h>

extern void mzarch_bootstrap_run_mzf(const char *filename);

/**
 * @brief Platform-specific bootstrap inicializace - default banking.
 *
 * Volá se po inicializaci PIO/CTC, ale před nahráním MZF headeru/těla
 * do paměti. Nastaví výchozí memory map (ROM 0000 + ROM E000 + případné
 * platform-specific kroky jako kopírování CG-ROM do CG-RAM na MZ-800).
 */
extern void mzarch_platform_bootstrap_init(void);

/**
 * @brief Nastaví kanonickou load-time memory map (= RAM na header bufferu
 *        0x10F0 i v dolní RAM), bez resetu PIO/CTC a bez CGROM kopie.
 *
 * Vyčleněno z `mzarch_platform_bootstrap_init()` (= jen řádek s
 * `g_memory.map = ...`). Slouží pro mid-session load (media_load_mzf),
 * kde nechceme destruktivní machine reset, ale potřebujeme map ve které
 * `cmthack_load_mzf_filename()` korektně zapíše hlavičku do RAM na 0x10F0
 * (= na MZ-800 je po resetu na 0x1000-0x1FFF mapovaná CG-ROM, viz
 * `MEMORY_MZ800_MAP_FLAG_ROM_1000`, takže MAPED zápis hlavičky by se
 * ztratil).
 *
 * Volající si typicky uloží `g_memory.map` před voláním a po dokončení
 * loadu ho obnoví, aby load neměl trvalý side effect na banking.
 *
 * Side effecty: pouze `g_memory.map`. Žádný PIO/CTC/GDG/CGROM zásah.
 */
extern void mzarch_platform_bootstrap_apply_load_map(void);

/**
 * @brief Odmapuje dolní ROM (0x0000-0x0FFF) pokud `fstrt < 0x1000`.
 *
 * Identické s "Bodem 1" v `mzarch_platform_bootstrap_post_header()`, ale
 * BEZ platform-specific GDG/video zásahů (= mz800 Bod 2 přepnutí DMD).
 * Určeno pro mid-session load (media_load_mzf), kde nechceme měnit video
 * mód běžícího programu, ale tělo MZF s `fstrt < 0x1000` se musí zapsat
 * do RAM místo pod ROM.
 *
 * Volá se PŘED `cmthack_read_mzf_body()`, na již nastavené load-time mapě
 * (`mzarch_platform_bootstrap_apply_load_map()`).
 *
 * @param fstrt cílová adresa programu (mzf_header.fstrt)
 * Side effecty: pouze `g_memory.map`.
 */
extern void mzarch_platform_load_prepare_body_map(uint16_t fstrt);

/**
 * @brief Platform-specific bootstrap úpravy podle načteného MZF headeru.
 *
 * Volá se po načtení MZF headeru do `0x10F0` a po jeho přečtení do
 * `mzf_header` struktury, ale PŘED nahráním těla MZF přes
 * `cmthack_read_mzf_body()`. Každá platforma sama rozhodne co upravit
 * v memory map / GDG state na základě cílové adresy programu.
 *
 * Společné chování napříč platformami:
 *   - Pokud `fstrt < 0x1000`, odmapovat dolní ROM (`MEMORY_*_MAP_FLAG_ROM_0000`),
 *     jinak by `cmthack_read_mzf_body()` zapisoval pod ROM. Pozn.: reálná
 *     ROM toto dělá jen když `fstrt == 0x0000` - naše bootstrap je víc
 *     vanilla (= mapping musí odpovídat skutečné cílové RAM).
 *
 * Platform-specific (MZ-800):
 *   - Pokud je nastavený MZ-800 mode (= backside switch S1=OFF), přepnout
 *     GDG DMD na 320x200@4A (= clear MZ700 bit + ostatní = 0) a odmapovat
 *     CG-RAM/VRAM v rozsahu 0x1000-0x1FFF + 0xC000-0xCFFF (= reset CGRAM_VRAM
 *     flag). Bez toho zůstává po `mzarch_platform_bootstrap_init()` default
 *     MZ-700 mode = nahraný MZ-800 program by neviděl správně mapovanou paměť.
 *
 * @param fstrt   cílová adresa programu (mzf_header.fstrt)
 */
extern void mzarch_platform_bootstrap_post_header(uint16_t fstrt);

#endif // BOOTSTRAP_H