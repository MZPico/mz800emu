/**
 * @file snap_z80.c
 * @brief Snapshot handler: Z80 CPU — uložení a načtení všech registrů
 */

#include <stdio.h>
#include <glib.h>

#include "snapshot/snapshot_mgr.h"
#include "snapshot/snapshot_xml.h"
#include "mzarch/mzarch.h"

static en_SNAPSHOT_RESULT snap_z80_save(st_SNAPSHOT_CONTEXT *ctx)
{
    z80_t *cpu = g_mzarch_main.cpu;

    snapshot_xml_writer_t *w = snapshot_xml_writer_new();
    snapshot_xml_write_header(w);

    snapshot_xml_open_element(w, "z80_state");

    /* Hlavní registry */
    snapshot_xml_open_element(w, "registers");
    snapshot_xml_write_hex16(w, "AF", (uint16_t)z80_get_reg(cpu, Z80_REG_AF));
    snapshot_xml_write_hex16(w, "BC", (uint16_t)z80_get_reg(cpu, Z80_REG_BC));
    snapshot_xml_write_hex16(w, "DE", (uint16_t)z80_get_reg(cpu, Z80_REG_DE));
    snapshot_xml_write_hex16(w, "HL", (uint16_t)z80_get_reg(cpu, Z80_REG_HL));
    snapshot_xml_close_element(w);

    /* Alternativní registry */
    snapshot_xml_open_element(w, "alt_registers");
    snapshot_xml_write_hex16(w, "AF_", (uint16_t)z80_get_reg(cpu, Z80_REG_AF2));
    snapshot_xml_write_hex16(w, "BC_", (uint16_t)z80_get_reg(cpu, Z80_REG_BC2));
    snapshot_xml_write_hex16(w, "DE_", (uint16_t)z80_get_reg(cpu, Z80_REG_DE2));
    snapshot_xml_write_hex16(w, "HL_", (uint16_t)z80_get_reg(cpu, Z80_REG_HL2));
    snapshot_xml_close_element(w);

    /* Indexové, PC, SP registry */
    snapshot_xml_write_hex16(w, "IX", (uint16_t)z80_get_reg(cpu, Z80_REG_IX));
    snapshot_xml_write_hex16(w, "IY", (uint16_t)z80_get_reg(cpu, Z80_REG_IY));
    snapshot_xml_write_hex16(w, "PC", (uint16_t)z80_get_reg(cpu, Z80_REG_PC));
    snapshot_xml_write_hex16(w, "SP", (uint16_t)z80_get_reg(cpu, Z80_REG_SP));

    /* I, R, R7 registry */
    snapshot_xml_write_hex8(w, "I", (uint8_t)cpu->i);
    snapshot_xml_write_hex8(w, "R", (uint8_t)(cpu->r & 0x7F));
    snapshot_xml_write_hex8(w, "R7", (uint8_t)(cpu->r & 0x80));

    /* Přerušovací mód a flagy */
    snapshot_xml_write_int(w, "interrupt_mode", (int)cpu->im);
    snapshot_xml_write_bool(w, "IFF1", cpu->iff1 != 0);
    snapshot_xml_write_bool(w, "IFF2", cpu->iff2 != 0);
    snapshot_xml_write_bool(w, "halted", cpu->halted != 0);

    snapshot_xml_close_element(w); /* z80_state */

    char *xml = snapshot_xml_writer_finish(w);
    en_SNAPSHOT_RESULT res = snapshot_io_write_xml(ctx->io, "cpu/z80.xml", xml);
    g_free(xml);

    return res;
}

static en_SNAPSHOT_RESULT snap_z80_load(st_SNAPSHOT_CONTEXT *ctx)
{
    char *xml = NULL;
    en_SNAPSHOT_RESULT res = snapshot_io_read_xml(ctx->io, "cpu/z80.xml", &xml);
    if (res != SNAPSHOT_OK) {
        SNAP_ERR("z80", "Cannot load cpu/z80.xml");
        return res;
    }

    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    g_free(xml);

    if (!r) {
        SNAP_ERR("z80", "Parse error in cpu/z80.xml");
        return SNAPSHOT_ERR_XML_PARSE;
    }

    z80_t *cpu = g_mzarch_main.cpu;

    if (!snapshot_xml_enter_element(r, "z80_state")) {
        SNAP_ERR("z80", "Missing element z80_state");
        snapshot_xml_reader_free(r);
        return SNAPSHOT_ERR_XML_PARSE;
    }

    /* Hlavní registry */
    if (snapshot_xml_enter_element(r, "registers")) {
        uint16_t val;
        if (snapshot_xml_read_hex16(r, "AF", &val)) z80_set_reg(cpu, Z80_REG_AF, val);
        if (snapshot_xml_read_hex16(r, "BC", &val)) z80_set_reg(cpu, Z80_REG_BC, val);
        if (snapshot_xml_read_hex16(r, "DE", &val)) z80_set_reg(cpu, Z80_REG_DE, val);
        if (snapshot_xml_read_hex16(r, "HL", &val)) z80_set_reg(cpu, Z80_REG_HL, val);
        snapshot_xml_leave_element(r);
    }

    /* Alternativní registry */
    if (snapshot_xml_enter_element(r, "alt_registers")) {
        uint16_t val;
        if (snapshot_xml_read_hex16(r, "AF_", &val)) z80_set_reg(cpu, Z80_REG_AF2, val);
        if (snapshot_xml_read_hex16(r, "BC_", &val)) z80_set_reg(cpu, Z80_REG_BC2, val);
        if (snapshot_xml_read_hex16(r, "DE_", &val)) z80_set_reg(cpu, Z80_REG_DE2, val);
        if (snapshot_xml_read_hex16(r, "HL_", &val)) z80_set_reg(cpu, Z80_REG_HL2, val);
        snapshot_xml_leave_element(r);
    }

    /* Indexové, PC, SP registry */
    {
        uint16_t val;
        if (snapshot_xml_read_hex16(r, "IX", &val)) z80_set_reg(cpu, Z80_REG_IX, val);
        if (snapshot_xml_read_hex16(r, "IY", &val)) z80_set_reg(cpu, Z80_REG_IY, val);
        if (snapshot_xml_read_hex16(r, "PC", &val)) z80_set_reg(cpu, Z80_REG_PC, val);
        if (snapshot_xml_read_hex16(r, "SP", &val)) z80_set_reg(cpu, Z80_REG_SP, val);
    }

    /* I, R, R7 registry */
    {
        uint8_t val8;
        if (snapshot_xml_read_hex8(r, "I", &val8)) cpu->i = val8;
        if (snapshot_xml_read_hex8(r, "R", &val8)) cpu->r = (val8 & 0x7F) | (cpu->r & 0x80);
        if (snapshot_xml_read_hex8(r, "R7", &val8)) cpu->r = (cpu->r & 0x7F) | (val8 & 0x80);
    }

    /* Přerušovací mód a flagy */
    {
        int im;
        bool bval;
        if (snapshot_xml_read_int(r, "interrupt_mode", &im)) cpu->im = (uint8_t)im;
        if (snapshot_xml_read_bool(r, "IFF1", &bval)) cpu->iff1 = bval ? 1 : 0;
        if (snapshot_xml_read_bool(r, "IFF2", &bval)) cpu->iff2 = bval ? 1 : 0;
        /* halted — stav HALT se nastaví přímo přes cpu->halted,
         * ale po loadu se CPU rozběhne od adresy PC, což je korektní chování */
    }

    snapshot_xml_leave_element(r); /* z80_state */
    snapshot_xml_reader_free(r);

    return SNAPSHOT_OK;
}

void snap_z80_register(void)
{
    snapshot_register_component("z80",
                                SNAPSHOT_PRIORITY_CPU,
                                snap_z80_save,
                                snap_z80_load,
                                false);
}
