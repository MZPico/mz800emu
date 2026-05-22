/**
 * @file wd279x_read_track.c
 * @brief WD279x - Type III READ TRACK (raw track stream).
 *
 * Vyextrahováno z wd279x.c v rámci refactoru. Sdílí state s chip
 * core přes st_WD279X struct, helper funkce a konstanty z
 * wd279x_internal.h.
 *
 * Veřejně exportované funkce (volané z wd279x.c):
 *  - do_read_track_setup() - aktivace v dispatch_command pro 0xE0..0xEF
 *  - rt_synth_byte() - on-demand byte z syntetického streamu v DATA read
 *
 * Sharp software READ TRACK nepoužívá; implementace je pro kompletnost.
 *
 * License: GPLv3 - viz wd279x.h.
 */

#include <stdint.h>
#include <string.h>

#include "wd279x.h"
#include "wd279x_internal.h"
#include "fdc.h"
#include "libs/dsk/dsk.h"

/* ====================================================================
 * Type III - READ TRACK (raw track stream)
 * ==================================================================== */

/**
 * @brief Velikost "per-sector" bloku v syntetické READ TRACK streamu.
 *
 * Layout (minimalistický, bez nadbytečných GAP fillerů):
 *  - 1 byte 0xFE (IDAM)
 *  - 4 byty ID (track, side, sector_id, ssize_code)
 *  - 1 byte 0xFB (DAM)
 *  - N bytů sektor data (N = sector size)
 */
#define RT_PER_SECTOR_OVERHEAD 6 /**< Markery + ID = 6 bytes před daty. */

/**
 * @brief Inicializuje READ TRACK streaming state.
 *
 * Volá se v dispatch_command pro opcodes 0xE0..0xEF. Načte tinfo aktuální
 * stopy, naplní rt_* fieldy a vypočítá data_counter (celková velikost
 * syntetického streamu).
 *
 * Pozn.: Sharp ROM (CP/M 1.4, 4.1, BASIC) READ TRACK nepoužívá. Příkaz
 * je implementován pro kompletnost a budoucí použití (např. disk image
 * analyzer, raw track dump). Streaming používá minimalistický formát
 * (1×IAM + N×{IDAM+ID+DAM+data}) - bez přesných MFM GAP fillerů.
 *
 * @param chip ukazatel na chip.
 * @param command true-bus hodnota příkazu (0xE0..0xEF).
 */
void do_read_track_setup(st_WD279X *chip, uint8_t command)
{
    (void)command;

    chip->status_mode = WD279X_STATUS_MODE_TYPE_II_III;
    chip->buffer_pos = 0;
    chip->data_counter = 0;
    chip->multiblock_rw = 0;
    chip->rt_cached_sec_idx = -1;

    if (!drive_is_ready(chip))
    {
        chip->regSTATUS = WDST_NOT_READY;
        chip->intrq_active = 1;
        return;
    }

    struct st_FDDrive *drv = current_drive(chip);
    if (!drv)
    {
        chip->regSTATUS = WDST_NOT_READY;
        chip->intrq_active = 1;
        return;
    }

    uint8_t side = chip->SIDE & 0x01;
    if (!set_track_position(chip, drv))
    {
        chip->regSTATUS = WDST_T2_RNF;
        chip->intrq_active = 1;
        return;
    }

    uint8_t abstrack = compute_abstrack(drv, chip->regTRACK, side);
    if (abstrack == 0xFF)
    {
        chip->regSTATUS = WDST_T2_RNF;
        chip->intrq_active = 1;
        return;
    }

    st_DSK_SHORT_TRACK_INFO tinfo;
    if (EXIT_SUCCESS != dsk_read_short_track_info(&drv->handler, NULL, abstrack, &tinfo))
    {
        chip->regSTATUS = WDST_T2_RNF;
        chip->intrq_active = 1;
        return;
    }
    if (tinfo.sectors == 0)
    {
        chip->regSTATUS = WDST_T2_RNF;
        chip->intrq_active = 1;
        return;
    }

    /* Cache parametry track pro on-demand byte generaci. */
    chip->rt_sectors = tinfo.sectors;
    chip->rt_ssize_code = tinfo.ssize;
    chip->rt_sector_bytes = dsk_decode_sector_size(tinfo.ssize);
    if (chip->rt_sector_bytes == 0 || chip->rt_sector_bytes > WD279X_BUFFER_SIZE)
    {
        chip->regSTATUS = WDST_T2_RNF;
        chip->intrq_active = 1;
        return;
    }

    /* Cache sektor ID list (sinfo[0..sectors-1]). */
    uint8_t copy_n = (tinfo.sectors > 29) ? 29 : tinfo.sectors;
    memcpy(chip->rt_sinfo, tinfo.sinfo, copy_n);

    /* Total bytes = 1 (IAM) + sectors × (6 overhead + N data). */
    chip->data_counter = (uint16_t)(1 + (uint32_t)chip->rt_sectors *
                                            (RT_PER_SECTOR_OVERHEAD + chip->rt_sector_bytes));

    chip->regSTATUS = WDST_BUSY | WDST_T2_DRQ;
}

/**
 * @brief Vrátí další byte syntetického READ TRACK streamu.
 *
 * Volá se z FDCPORT_DATA read handler když je v běhu READ TRACK
 * (= regCOMMAND v rozsahu 0xE0..0xEF) a data_counter > 0.
 *
 * Position-to-byte mapping (chip->buffer_pos = absolutní pozice v streamu):
 *  - pos == 0: 0xFC (IAM)
 *  - pos >= 1: per-sector block
 *      block_index = (pos - 1) / (6 + N)
 *      in_block    = (pos - 1) % (6 + N)
 *      in_block 0: 0xFE (IDAM)
 *      in_block 1: track
 *      in_block 2: side
 *      in_block 3: sector_id = rt_sinfo[block_index]
 *      in_block 4: ssize_code
 *      in_block 5: 0xFB (DAM)
 *      in_block 6..N+5: sector data byte (k = in_block - 6)
 *
 * Sector data se lazy-loaduje do chip->buffer při změně block_index.
 *
 * @param chip ukazatel na chip.
 * @return Byte k vrácení na DATA port.
 */
uint8_t rt_synth_byte(st_WD279X *chip)
{
    uint16_t pos = chip->buffer_pos;
    if (pos == 0)
        return 0xFC; /* IAM */

    uint32_t p = pos - 1;
    uint32_t block_size = (uint32_t)RT_PER_SECTOR_OVERHEAD + chip->rt_sector_bytes;
    uint8_t sec_idx = (uint8_t)(p / block_size);
    uint32_t in_block = p % block_size;

    /* Hranice safety - pokud bychom vypočítali sec_idx mimo rozsah,
     * vrátíme 0x00 (= sync filler) - lépe než garbage. */
    if (sec_idx >= chip->rt_sectors)
        return 0x00;

    if (in_block == 0)
        return 0xFE; /* IDAM */
    if (in_block == 1)
        return chip->regTRACK;
    if (in_block == 2)
        return chip->SIDE & 0x01;
    if (in_block == 3)
        return chip->rt_sinfo[sec_idx];
    if (in_block == 4)
        return chip->rt_ssize_code;
    if (in_block == 5)
        return 0xFB; /* DAM */

    /* Sektor data area - lazy load sektoru do bufferu při změně indexu. */
    if (chip->rt_cached_sec_idx != (int8_t)sec_idx)
    {
        struct st_FDDrive *drv = current_drive(chip);
        if (drv)
        {
            uint8_t side = chip->SIDE & 0x01;
            uint8_t abstrack = compute_abstrack(drv, chip->regTRACK, side);
            if (abstrack != 0xFF)
            {
                /* Načti sektor i (= chip->rt_sinfo[sec_idx] = sector ID) do chip->buffer. */
                if (EXIT_SUCCESS == dsk_rw_sector(&drv->handler, DSK_RWOP_READ, NULL, NULL,
                                                    abstrack, chip->rt_sinfo[sec_idx],
                                                    chip->buffer))
                {
                    chip->rt_cached_sec_idx = (int8_t)sec_idx;
                }
                else
                {
                    /* Read failure - vyplň buffer fillerem (= 0xE5) jako _new_
                     * format default. */
                    memset(chip->buffer, 0xE5, WD279X_BUFFER_SIZE);
                    chip->rt_cached_sec_idx = (int8_t)sec_idx;
                }
            }
        }
    }

    uint32_t data_offset = in_block - RT_PER_SECTOR_OVERHEAD;
    if (data_offset >= chip->rt_sector_bytes)
        return 0x00; /* safety */
    return chip->buffer[data_offset];
}

