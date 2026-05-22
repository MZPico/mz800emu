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