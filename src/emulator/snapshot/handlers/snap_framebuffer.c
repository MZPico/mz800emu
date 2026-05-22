/**
 * @file snap_framebuffer.c
 * @brief Snapshot handler: framebuffer — pixel buffery pro zobrazení
 */

#include <stdio.h>
#include <glib.h>

#include "snapshot/snapshot_mgr.h"
#include "snapshot/snapshot_xml.h"
#include "hw-generic/gdg/framebuffer.h"

static en_SNAPSHOT_RESULT snap_framebuffer_save(st_SNAPSHOT_CONTEXT *ctx)
{
    en_SNAPSHOT_RESULT res;

    /* Uložení pixel bufferů jako binární data */
    res = snapshot_io_write_bin(ctx->io, "hw/framebuffer.bin",
                                (const uint8_t *)g_framebuffer.pixbuff,
                                sizeof(g_framebuffer.pixbuff));
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("framebuffer", "Cannot write hw/framebuffer.bin");
        return res;
    }

    /* Uložení stavu (pixels_id) jako XML */
    snapshot_xml_writer_t *w = snapshot_xml_writer_new();
    snapshot_xml_write_header(w);

    snapshot_xml_open_element(w, "framebuffer_state");
    snapshot_xml_write_int(w, "pixels_id", g_framebuffer.pixels_id);
    snapshot_xml_close_element(w);

    char *xml = snapshot_xml_writer_finish(w);
    res = snapshot_io_write_xml(ctx->io, "hw/framebuffer_state.xml", xml);
    g_free(xml);

    return res;
}

static en_SNAPSHOT_RESULT snap_framebuffer_load(st_SNAPSHOT_CONTEXT *ctx)
{
    en_SNAPSHOT_RESULT res;

    /* Načtení pixel bufferů */
    res = snapshot_io_read_bin_into(ctx->io, "hw/framebuffer.bin",
                                    (uint8_t *)g_framebuffer.pixbuff,
                                    sizeof(g_framebuffer.pixbuff));
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("framebuffer", "Cannot load hw/framebuffer.bin");
        return res;
    }

    /* Načtení stavu */
    char *xml = NULL;
    res = snapshot_io_read_xml(ctx->io, "hw/framebuffer_state.xml", &xml);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("framebuffer", "Cannot load hw/framebuffer_state.xml");
        return res;
    }

    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    g_free(xml);

    if (!r) {
        SNAP_ERR("framebuffer", "Parse error in hw/framebuffer_state.xml");
        return SNAPSHOT_ERR_XML_PARSE;
    }

    if (snapshot_xml_enter_element(r, "framebuffer_state")) {
        snapshot_xml_read_int(r, "pixels_id", &g_framebuffer.pixels_id);
        snapshot_xml_leave_element(r);
    }

    snapshot_xml_reader_free(r);

    /* Nastavení ukazatele pixels na správný buffer dle pixels_id */
    if (g_framebuffer.pixels_id < 0 || g_framebuffer.pixels_id >= GDG_FRAMEBUFFER_PIXBUF_COUNT) {
        SNAP_WARN("framebuffer", "Invalid pixels_id %d, resetting to 0",
                  g_framebuffer.pixels_id);
        g_framebuffer.pixels_id = 0;
    }
    g_framebuffer.pixels = g_framebuffer.pixbuff[g_framebuffer.pixels_id];

    return SNAPSHOT_OK;
}

void snap_framebuffer_register(void)
{
    snapshot_register_component("framebuffer",
                                SNAPSHOT_PRIORITY_FRAMEBUFFER,
                                snap_framebuffer_save,
                                snap_framebuffer_load,
                                false);
}
