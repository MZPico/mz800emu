#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Lokalizace
#include "i18n.h"

#include "hw-generic/memory/rom_config.h"

extern uint8_t c_ROM_MZ1500_0000[];
extern uint8_t c_ROM_MZ1500_CGROM[];
extern uint8_t c_ROM_MZ1500_E000[];

extern uint8_t c_ROM_MZNEWMON_0000[];
extern uint8_t c_ROM_MZNEWMON_CGROM[];

static st_ROM_CONFIG_ROW c_roms[] = {
    {ROM_CONFIG_TYPE_ROM, true, "STANDARD", N_("Standard Sharp MZ-1500 ROM"), ROM_CONFIG_FLAG_NONE, c_ROM_MZ1500_0000, c_ROM_MZ1500_CGROM, c_ROM_MZ1500_E000},
    {ROM_CONFIG_TYPE_ROM, true, "NEWMON", N_("Alternative monitor: MZ-NEWMON"), ROM_CONFIG_FLAG_CMTHACK_INCOMP, c_ROM_MZNEWMON_0000, c_ROM_MZNEWMON_CGROM, c_ROM_MZ1500_E000},
    {ROM_CONFIG_TYPE_SECTION, true, NULL, N_("User"), ROM_CONFIG_FLAG_NONE, NULL, NULL, NULL},
    {ROM_CONFIG_TYPE_ROM, true, "USER_DEFINED", N_("User Defined ROM"), ROM_CONFIG_FLAG_USER_DEFINED, NULL, NULL, NULL},
    {ROM_CONFIG_TYPE_NONE, false, NULL, NULL, ROM_CONFIG_FLAG_NONE, NULL, NULL, NULL}};

static st_ROM_CONFIG c_rom_config = {
    .roms = c_roms,
    .default_rom = &c_roms[0]};

st_ROM_CONFIG *g_rom_config = &c_rom_config;
