#include "main.h"

#include "mzarch/mzarch_config.h"
#include "mzarch/bootstrap.h"
#include "hw-generic/memory/memory.h"
#include "memory/mz1500_memory.h"

void mzarch_platform_bootstrap_init(void)
{
    g_memory.map = MEMORY_MZ1500_MAP_FLAG_ROM_0000 | MEMORY_MZ1500_MAP_FLAG_ROM_UPPER;
}

void mzarch_platform_bootstrap_post_header(uint16_t fstrt)
{
    /* Pokud cilova adresa programu je v rozsahu prvnich 4 KB, odmapujeme
     * ROM 0x0000-0x0FFF aby se telo MZF korektne ulozilo do RAM. Pozn.:
     * realna ROM toto chovani dela jen kdyz fstrt == 0x0000, ale nase
     * bootstrap je vic vanilkove (= mapping musi odpovidat skutecne
     * cilove RAM). */
    if (fstrt < 0x1000)
    {
        g_memory.map &= ~MEMORY_MZ1500_MAP_FLAG_ROM_0000;
    };
}
