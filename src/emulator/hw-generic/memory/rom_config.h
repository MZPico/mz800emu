#ifndef ROM_CONFIG_H
#define ROM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum en_ROM_CONFIG_FLAGS
    {
        ROM_CONFIG_FLAG_NONE           = 0,
        ROM_CONFIG_FLAG_CMTHACK_INCOMP = (1 << 0), // Nekompatibilní s CMT hackem (WILLY)
        ROM_CONFIG_FLAG_DEVELOPMENT    = (1 << 1), // Jen pro development mód
        ROM_CONFIG_FLAG_USER_DEFINED   = (1 << 2), // Speciální - data se načítají ze souborů
    } en_ROM_CONFIG_FLAGS;

    typedef enum en_ROM_CONFIG_ROW_TYPE
    {
        ROM_CONFIG_TYPE_NONE,    // Pro ukončení pole
        ROM_CONFIG_TYPE_SECTION, // Pro oddělení sekcí v UI, může mít popisek (description)
        ROM_CONFIG_TYPE_ROM,     // Skutečný ROM, musí mít name, description a data
    } en_ROM_CONFIG_ROW_TYPE;

    typedef struct st_ROM_CONFIG_ROW
    {
        en_ROM_CONFIG_ROW_TYPE type; // Typ řádku (ROM, sekce, konec)
        bool enabled;                // Je tento řádek povolen (pro budoucí možnost deaktivace některých ROM z nabídky)
        char *name;                  // Název ROM pro identifikaci a pro cfgfile (např. "DEFAULT", "JSS105C", "WILLY_EN" apod.)
        char *description;           // Popis ROM, může být zobrazen v UI
        uint32_t flags;              // Bitové příznaky (en_ROM_CONFIG_FLAGS)
        void *rom_0000;              // Ukazatel na data ROM pro adresu 0x0000
        void *rom_cgrom;             // Ukazatel na data ROM pro adresu CGROM
        void *rom_e000;              // Ukazatel na data ROM pro adresu 0xE000
    } st_ROM_CONFIG_ROW;

    typedef struct st_ROM_CONFIG
    {
        st_ROM_CONFIG_ROW *roms;        // Pole s konfigurací ROM, musí být ukončeno řádkem s type == ROM_CONFIG_TYPE_NONE
        st_ROM_CONFIG_ROW *default_rom; // Ukazatel na výchozí ROM, která se použije pokud není známa konfigurace, nebo pokud konfigurace obsahuje neplatný název ROM, nebo nepovolený řádek
    } st_ROM_CONFIG;

    // Makro pro testování flagů na řádku ROM konfigurace
#define ROM_CFG_TEST_FLAG(row, flag) (((row) != NULL) && ((row)->flags & (flag)))

    extern st_ROM_CONFIG *g_rom_config; // Globální ukazatel na konfiguraci ROM, který může být přepsán v konkrétní implementaci (např. mz1500_rom.c, mz800_rom.c) a který bude použit v rom.c pro načítaní ROM podle typu

#ifdef __cplusplus
}
#endif

#endif /* ROM_CONFIG_H */