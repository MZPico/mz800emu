#include "main.h"

#include <string.h>

#include "mzarch/mzarch_config.h"
#include "mzarch/mzarch.h"
#include "mzarch/bootstrap.h"
#include "hw-generic/memory/memory.h"
#include "memory/mz800_memory.h"
#include "gdg/mz800_gdg.h"

void mzarch_platform_bootstrap_init(void)
{
    g_memory.map = MEMORY_MZ800_MAP_FLAG_ROM_0000 | MEMORY_MZ800_MAP_FLAG_ROM_E000;

    /* Při bootu MZ-800 monitor ROM rutina kopíruje obsah CG-ROM (4 KB) do
     * CG-RAM (= prvních 4 KB VRAM Plane I, mapovaných v MZ-700 modu na
     * 0xC000-0xCFFF). Bootstrap obchází ROM start, takže to musíme udělat
     * sami, jinak je CG-RAM po bootu prázdná a programy v MZ-700 modu
     * vidí prázdné znaky.
     *
     * CG-ROM v paměti: g_memory.ROM[0x1000-0x1FFF] (= addr & 0x3fff pro
     * bus 0x1000-0x1FFF, viz MEMORY_ROM_READ_BYTE makro). */
    memcpy ( g_memoryVRAM_I, &g_memory.ROM[ 0x1000 ], 0x1000 );
}

void mzarch_platform_bootstrap_post_header(uint16_t fstrt)
{
    /* Bod 1 - společné pro všechny platformy:
     * Pokud cílová adresa programu je v rozsahu prvních 4 KB, odmapujeme
     * ROM 0x0000-0x0FFF aby se tělo MZF korektně uložilo do RAM. Pozn.:
     * reálná ROM toto chování dělá jen když fstrt == 0x0000, ale naše
     * bootstrap je víc vanilkové (= mapping musí odpovídat skutečné
     * cílové RAM). */
    if (fstrt < 0x1000)
    {
        g_memory.map &= ~MEMORY_MZ800_MAP_FLAG_ROM_0000;
    };

    /* Bod 2 - MZ-800 specific: nastavit startovní mode podle zadního
     * switche (en_SWITCH700) v g_mzarch_main.switch700.
     *
     * Společné napříč oběma stavy switche:
     *   - PROHIBITED = 0 (= banking mode není aktivní; pro jistotu clear)
     *   - ROM E000 mapped (= horní ROM dostupná; default už je v _init)
     */
    g_memory.map &= ~MEMORY_MZ800_MAP_FLAG_PROHIBITED;
    g_memory.map |= MEMORY_MZ800_MAP_FLAG_ROM_E000;

    //if (g_mzarch_main.switch700 == SWITCH700_OFF)
    if (!g_mzarch_main.switch700)
    {
        printf ( "Bootstrap: SWITCH700_ON = MZ-700 mode\n" );
        /* SWITCH700_ON = MZ-700 mode */
        g_gdg.regDMD = 0x08;
    }
    else
    {
        printf ( "Bootstrap: SWITCH700_OFF = MZ-800 mode\n" );
        /* SWITCH700_OFF = MZ-800 mode:
         *   - GDG DMD = 0x00 (= 320x200@4A, bit 3 MZ700 vypnutý)
         *   - VRAM/CG-RAM odpojené (clear CGRAM_VRAM + ROM_1000 flagy)
         *
         * Default po gdg_init je DMD = REGISTER_DMD_FLAG_MZ700 (=
         * MZ-700 mode), takže přepneme přes IORQ simulaci. gdg_write_byte
         * je standardní cesta - vyvolá framebuffer_MZ800_screen_changed
         * a CTC8253 event jako reálný OUT. */
        g_gdg.regDMD = 0x00;
        g_memory.map &= ~( MEMORY_MZ800_MAP_FLAG_CGRAM_VRAM |
                           MEMORY_MZ800_MAP_FLAG_ROM_1000 );
    }
    /* SWITCH700_ON = MZ-700 mode: ponechat default z _init.
     *   - DMD zůstává v MZ-700 mode (= bit 3 = 1, default po gdg_init).
     *   - VRAM se v MZ-700 mode připojuje automaticky s horní ROM
     *     (= ROM E000 implikuje VRAM D000-DFFF mapping, viz
     *     MEMORY_MZ700_MAP_TEST_VRAM_D000 makro). CGRAM_VRAM flag tu
     *     není relevantní - v 700 modu se 0xC000-0xCFFF mapuje
     *     automaticky s ROM E000 přes memory_internal_read_c000_cfff. */
}
