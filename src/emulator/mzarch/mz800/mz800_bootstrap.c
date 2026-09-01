#include "main.h"

#include <string.h>

#include "mzarch/mzarch_config.h"
#include "mzarch/mzarch.h"
#include "mzarch/bootstrap.h"
#include "hw-generic/memory/memory.h"
#include "memory/mz800_memory.h"
#include "gdg/mz800_gdg.h"

void mzarch_platform_bootstrap_apply_load_map(void)
{
    /* Load-time map: ROM na 0x0000-0x0FFF a 0xE000+, ale RAM na
     * 0x1000-0x1FFF (= ROM_1000/CG-ROM odmapováno), aby header buffer
     * 0x10F0 byl RAM a hlavička MZF se korektně zapsala. */
    g_memory.map = MEMORY_MZ800_MAP_FLAG_ROM_0000 | MEMORY_MZ800_MAP_FLAG_ROM_E000;
#ifdef MZ800EMU_CFG_RAM_FASTPATH
    mz800_ram_fastpath_rebuild ();
#endif
}

void mzarch_platform_bootstrap_init(void)
{
    mzarch_platform_bootstrap_apply_load_map ();

    /* Při bootu MZ-800 monitor ROM rutina kopíruje obsah CG-ROM (4 KB) do
     * CG-RAM (= prvních 4 KB VRAM Plane I, mapovaných v MZ-700 modu na
     * 0xC000-0xCFFF). Bootstrap obchází ROM start, takže to musíme udělat
     * sami, jinak je CG-RAM po bootu prázdná a programy v MZ-700 modu
     * vidí prázdné znaky.
     *
     * CG-ROM v paměti: g_memory.ROM[0x1000-0x1FFF] (= addr & 0x3fff pro
     * bus 0x1000-0x1FFF, viz MEMORY_ROM_READ_BYTE makro). */
    memcpy ( g_memoryVRAM_I, &g_memory.ROM[ 0x1000 ], 0x1000 );

    /* The monitor clears the MZ-700 mode text screen at boot: character VRAM
     * (D000-D7FF = plane I + 0x1000) to 0x00 and attribute VRAM (D800-DFFF =
     * plane I + 0x1800) to 0x71 (white on blue). Programs such as S-BASIC
     * 1Z-013B never touch the attributes themselves and rely on this; without
     * it the power-on VRAM pattern shows through as stripes. */
    memset ( g_memoryVRAM_I + 0x1000, 0x00, 0x0800 );
    memset ( g_memoryVRAM_I + 0x1800, 0x71, 0x0800 );
}

void mzarch_platform_load_prepare_body_map(uint16_t fstrt)
{
    /* Pokud cílová adresa programu je v rozsahu prvních 4 KB, odmapujeme
     * ROM 0x0000-0x0FFF aby se tělo MZF korektně uložilo do RAM. Pozn.:
     * reálná ROM toto chování dělá jen když fstrt == 0x0000, ale naše
     * bootstrap je víc vanilkové (= mapping musí odpovídat skutečné
     * cílové RAM). */
    if (fstrt < 0x1000)
    {
        g_memory.map &= ~MEMORY_MZ800_MAP_FLAG_ROM_0000;
#ifdef MZ800EMU_CFG_RAM_FASTPATH
        mz800_ram_fastpath_rebuild ();
#endif
    };
}

void mzarch_platform_bootstrap_post_header(uint16_t fstrt)
{
    /* Bod 1 - společné pro všechny platformy: odmapování dolní ROM. */
    mzarch_platform_load_prepare_body_map ( fstrt );

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
#ifdef MZ800EMU_CFG_RAM_FASTPATH
    /* Bootstrap menil g_memory.map a regDMD primo -> prepocti fast-path. */
    mz800_ram_fastpath_rebuild ();
#endif
    /* SWITCH700_ON = MZ-700 mode: ponechat default z _init.
     *   - DMD zůstává v MZ-700 mode (= bit 3 = 1, default po gdg_init).
     *   - VRAM se v MZ-700 mode připojuje automaticky s horní ROM
     *     (= ROM E000 implikuje VRAM D000-DFFF mapping, viz
     *     MEMORY_MZ700_MAP_TEST_VRAM_D000 makro). CGRAM_VRAM flag tu
     *     není relevantní - v 700 modu se 0xC000-0xCFFF mapuje
     *     automaticky s ROM E000 přes memory_internal_read_c000_cfff. */
}
