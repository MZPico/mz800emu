/**
 * @file snap_memory.c
 * @brief Snapshot handler: RAM, VRAM, EXVRAM (MZ-800) / PCG (MZ-1500)
 */

#include <stdio.h>
#include <glib.h>

#include "snapshot/snapshot_mgr.h"
#include "snapshot/snapshot_xml.h"
#include "memory/memory.h"

static en_SNAPSHOT_RESULT snap_memory_save(st_SNAPSHOT_CONTEXT *ctx)
{
    en_SNAPSHOT_RESULT res;

    /* Uložení hlavní RAM (64 KB) */
    res = snapshot_io_write_bin(ctx->io, "memory/ram.bin",
                                g_memory.RAM, MEMORY_SIZE_RAM);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("memory", "Cannot write memory/ram.bin");
        return res;
    }

    /* Uložení VRAM */
    res = snapshot_io_write_bin(ctx->io, "memory/vram.bin",
                                g_memory.VRAM, MEMORY_SIZE_VRAM);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("memory", "Cannot write memory/vram.bin");
        return res;
    }

#if MZARCH == 800
    /* MZ-800: rozšířená VRAM (banky III, IV) */
    res = snapshot_io_write_bin(ctx->io, "memory/exvram.bin",
                                g_memory.EXVRAM, MEMORY_SIZE_VRAM);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("memory", "Cannot write memory/exvram.bin");
        return res;
    }
#endif

#if MZARCH == 1500
    /* MZ-1500: PCG banky */
    res = snapshot_io_write_bin(ctx->io, "memory/pcg.bin",
                                g_memory.PCG, MEMORY_SIZE_PCG);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("memory", "Cannot write memory/pcg.bin");
        return res;
    }
#endif

    /* Uložení stavu paměťové mapy */
    snapshot_xml_writer_t *w = snapshot_xml_writer_new();
    snapshot_xml_write_header(w);

    snapshot_xml_open_element(w, "memory_state");
#if MZARCH == 700
    /* Snapshot kompatibilita: bit 2 byl puvodne ROM_E800_mapped (1=mapped),
     * po prejmenovani na PROHIBITED ma invertovany vyznam (1=Prohibited
     * active). Snapshot format zachovan v puvodni semantice ROM_E800 -
     * pri save invertujeme bit 2. */
    snapshot_xml_write_hex8(w, "map", g_memory.map ^ MEMORY_MZ700_MAP_FLAG_PROHIBITED);
#else
    snapshot_xml_write_hex8(w, "map", g_memory.map);
#endif
    snapshot_xml_close_element(w);

    char *xml = snapshot_xml_writer_finish(w);
    res = snapshot_io_write_xml(ctx->io, "memory/memory_state.xml", xml);
    g_free(xml);

    return res;
}

static en_SNAPSHOT_RESULT snap_memory_load(st_SNAPSHOT_CONTEXT *ctx)
{
    en_SNAPSHOT_RESULT res;

    /* Načtení hlavní RAM */
    res = snapshot_io_read_bin_into(ctx->io, "memory/ram.bin",
                                    g_memory.RAM, MEMORY_SIZE_RAM);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("memory", "Cannot load memory/ram.bin");
        return res;
    }

    /* Načtení VRAM */
    res = snapshot_io_read_bin_into(ctx->io, "memory/vram.bin",
                                    g_memory.VRAM, MEMORY_SIZE_VRAM);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("memory", "Cannot load memory/vram.bin");
        return res;
    }

#if MZARCH == 800
    /* MZ-800: rozšířená VRAM */
    res = snapshot_io_read_bin_into(ctx->io, "memory/exvram.bin",
                                    g_memory.EXVRAM, MEMORY_SIZE_VRAM);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("memory", "Cannot load memory/exvram.bin");
        return res;
    }
#endif

#if MZARCH == 1500
    /* MZ-1500: PCG banky */
    res = snapshot_io_read_bin_into(ctx->io, "memory/pcg.bin",
                                    g_memory.PCG, MEMORY_SIZE_PCG);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("memory", "Cannot load memory/pcg.bin");
        return res;
    }
#endif

    /* Načtení stavu paměťové mapy */
    char *xml = NULL;
    res = snapshot_io_read_xml(ctx->io, "memory/memory_state.xml", &xml);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("memory", "Cannot load memory/memory_state.xml");
        return res;
    }

    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    g_free(xml);

    if (!r) {
        SNAP_ERR("memory", "Parse error in memory/memory_state.xml");
        return SNAPSHOT_ERR_XML_PARSE;
    }

    if (snapshot_xml_enter_element(r, "memory_state")) {
        uint8_t map_val;
        if (snapshot_xml_read_hex8(r, "map", &map_val)) {
#if MZARCH == 700
            /* Snapshot kompatibilita - viz save: invertujeme bit 2 zpet
             * z puvodni semantiky ROM_E800_mapped na novou PROHIBITED. */
            map_val ^= MEMORY_MZ700_MAP_FLAG_PROHIBITED;
#endif
            g_memory.map = map_val;
        }
        snapshot_xml_leave_element(r);
    }

    snapshot_xml_reader_free(r);

    /* Přepojení paměťových bank dle načtené mapy */
    memory_reconnect_ram();

    return SNAPSHOT_OK;
}

void snap_memory_register(void)
{
    snapshot_register_component("memory",
                                SNAPSHOT_PRIORITY_MEMORY,
                                snap_memory_save,
                                snap_memory_load,
                                false);
}
