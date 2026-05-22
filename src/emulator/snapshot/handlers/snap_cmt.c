/**
 * @file snap_cmt.c
 * @brief Snapshot handler: CMT kazeta — uložení a načtení stavu kazetového magnetofonu
 */

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "snapshot/snapshot_mgr.h"
#include "snapshot/snapshot_xml.h"
#include "hw-generic/cmt/cmt.h"


static en_SNAPSHOT_RESULT snap_cmt_save(st_SNAPSHOT_CONTEXT *ctx)
{
    snapshot_xml_writer_t *w = snapshot_xml_writer_new();
    snapshot_xml_write_header(w);

    snapshot_xml_open_element(w, "cmt_state");

    /* Základní stavové proměnné */
    snapshot_xml_write_int(w, "state", (int)g_cmt.state);
    snapshot_xml_write_int(w, "paused", g_cmt.paused);
    snapshot_xml_write_int(w, "polarity", (int)g_cmt.polarity);
    snapshot_xml_write_int(w, "mz_cmtspeed", (int)g_cmt.mz_cmtspeed);
    snapshot_xml_write_int(w, "output", g_cmt.output);

    /* Časové údaje */
    snapshot_xml_write_uint64(w, "start_time", g_cmt.start_time);
    snapshot_xml_write_uint64(w, "paused_time", g_cmt.paused_time);
    snapshot_xml_write_uint64(w, "recording_last_event", g_cmt.recording_last_event);

    /* Nastavení */
    snapshot_xml_write_int(w, "cpu_boost", (int)g_cmt.cpu_boost);
    snapshot_xml_write_int(w, "mzfsize_check", (int)g_cmt.mzfsize_check);
    snapshot_xml_write_int(w, "recording_to_stream", g_cmt.recording_to_stream);

    /* Název posledního souboru (může být NULL) */
    snapshot_xml_write_string(w, "last_filename",
                              g_cmt.last_filename ? g_cmt.last_filename : "");

    snapshot_xml_close_element(w); /* cmt_state */

    char *xml = snapshot_xml_writer_finish(w);
    en_SNAPSHOT_RESULT res = snapshot_io_write_xml(ctx->io, "devices/cmt.xml", xml);
    g_free(xml);

    return res;
}


static en_SNAPSHOT_RESULT snap_cmt_load(st_SNAPSHOT_CONTEXT *ctx)
{
    /* CMT entry je volitelný — pokud neexistuje, přeskočíme */
    if (!snapshot_io_entry_exists(ctx->io, "devices/cmt.xml")) {
        return SNAPSHOT_OK;
    }

    char *xml = NULL;
    en_SNAPSHOT_RESULT res = snapshot_io_read_xml(ctx->io, "devices/cmt.xml", &xml);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("cmt", "Cannot load devices/cmt.xml");
        return res;
    }

    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    g_free(xml);

    if (!r) {
        SNAP_ERR("cmt", "Parse error in devices/cmt.xml");
        return SNAPSHOT_ERR_XML_PARSE;
    }

    if (!snapshot_xml_enter_element(r, "cmt_state")) {
        SNAP_ERR("cmt", "Missing element cmt_state");
        snapshot_xml_reader_free(r);
        return SNAPSHOT_ERR_XML_PARSE;
    }

    int ival;

    /* Základní stavové proměnné */
    if (snapshot_xml_read_int(r, "state", &ival)) g_cmt.state = (en_CMT_STATE)ival;
    snapshot_xml_read_int(r, "paused", &g_cmt.paused);
    if (snapshot_xml_read_int(r, "polarity", &ival)) g_cmt.polarity = (en_CMT_STREAM_POLARITY)ival;
    if (snapshot_xml_read_int(r, "mz_cmtspeed", &ival)) g_cmt.mz_cmtspeed = (en_CMTSPEED)ival;
    snapshot_xml_read_int(r, "output", &g_cmt.output);

    /* Časové údaje */
    snapshot_xml_read_uint64(r, "start_time", &g_cmt.start_time);
    snapshot_xml_read_uint64(r, "paused_time", &g_cmt.paused_time);
    snapshot_xml_read_uint64(r, "recording_last_event", &g_cmt.recording_last_event);

    /* Nastavení */
    if (snapshot_xml_read_int(r, "cpu_boost", &ival)) g_cmt.cpu_boost = (en_CMT_CPU_BOOST)ival;
    if (snapshot_xml_read_int(r, "mzfsize_check", &ival)) g_cmt.mzfsize_check = (en_CMT_MZFSIZE_CHECK)ival;
    snapshot_xml_read_int(r, "recording_to_stream", &g_cmt.recording_to_stream);

    /* Název posledního souboru */
    char *fname = NULL;
    if (snapshot_xml_read_string(r, "last_filename", &fname)) {
        if (fname && fname[0] != '\0') {
            SNAP_WARN("cmt", "Cassette reference file '%s' recorded, but will not be auto-opened", fname);
        }
        g_free(fname);
    }

    snapshot_xml_leave_element(r); /* cmt_state */
    snapshot_xml_reader_free(r);

    return SNAPSHOT_OK;
}


void snap_cmt_register(void)
{
    snapshot_register_component("cmt",
                                SNAPSHOT_PRIORITY_DEVICE,
                                snap_cmt_save,
                                snap_cmt_load,
                                true);
}
