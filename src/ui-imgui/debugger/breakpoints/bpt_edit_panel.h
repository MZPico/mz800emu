/*
 * bpt_edit_panel.h - Nemodální dockable panel pro editaci BP/Group
 *
 * Single-instance model: panel zobrazuje JEDEN BP/Group najednou. Další
 * klik "Edit" na jiném BP přepne obsah panelu (s confirm při dirty
 * stavu). Layout je 2-sloupcový pro Event (Properties + Code Preview),
 * jednodušší pro Group.
 *
 * ----------------------------- License -------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef BPT_EDIT_PANEL_H
#define BPT_EDIT_PANEL_H

#include <stdint.h>
#include <stdbool.h>
#include "bpt_state.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Vykreslí edit panel pokud je viditelný (g_bpt_ui.edit_panel.visible).
 * Volá se každý frame z bpt_window.cpp.
 */
void bpt_edit_panel_render(void);


/**
 * Otevře (nebo přepne obsah) edit panel pro daný typ + id.
 * id == -1 = "new" (= prázdné defaulty pro nový BP/Group).
 *
 * Pokud je panel již zobrazen pro jiný edit_id a dirty == true,
 * zobrazí confirm dialog "Discard changes?". Pokud dirty == false,
 * přepne obsah okamžitě.
 */
void bpt_edit_panel_open(BptItemType type, int id);


/**
 * Skryje panel. Pokud je dirty, zobrazí confirm. Po confirm volá
 * bpt_edit_panel_close_confirmed.
 */
void bpt_edit_panel_close(void);


/**
 * Otevre Edit panel pro NOVY BP s pre-fill IORQ R/W type a portem.
 *
 * Pouziva I/O Ports viewer (D.7) pri "Add IORQ R/W BP" z context menu.
 * Po zavolani panel zobrazi formular pro novy BP s wc_type uz nastavenym
 * na IORQ_R nebo IORQ_W a wc_port pre-fillnutym hex stringem portu.
 *
 * Pri dirty existujicim panelu se chova stejne jako bpt_edit_panel_open()
 * (= confirm dialog "Discard changes?"). V tom pripade pre-fill probehne
 * az po potvrzeni discard.
 *
 * @param port_addr 16-bit I/O adresa portu (typicky 8bit, ale 16bit IORQ
 *                  existuje pro GDG scroll registry).
 * @param is_write  true = IORQ_W, false = IORQ_R.
 *
 * Pre-condition: Edit panel module je inicializovan (= bpt_state_init bezel).
 * Side-effect: g_bpt_ui.edit_panel pole se prepise.
 */
void bpt_edit_panel_open_new_iorq(uint16_t port_addr, bool is_write);


/**
 * Helper - otevre Edit BP panel s pre-fillem pro novy MEM_R / MEM_W
 * breakpoint.
 *
 * Pouziva I/O Ports viewer (V1.5.F) pri "Add MEM R/W BP" z context menu
 * nad memory-mapped IO entries (0xE000-0xE008). Po zavolani panel zobrazi
 * formular pro novy BP s wc_type uz nastavenym na MEM_R/MEM_W, wc_addr
 * pre-fillnutym hex stringem adresy a wc_addr_end nastavenym na stejnou
 * hodnotu (= SINGLE match mode default; user muze rozsirit na RANGE
 * v Edit panelu).
 *
 * Pri dirty existujicim panelu se chova stejne jako bpt_edit_panel_open()
 * (= confirm dialog "Discard changes?"). V tom pripade pre-fill probehne
 * az po potvrzeni discard.
 *
 * @param mem_addr 16-bit pametovy adresa (typicky 0xE000-0xE008 pro MMIO).
 * @param is_write true = MEM_W, false = MEM_R.
 *
 * Pre-condition: Edit panel module je inicializovan (= bpt_state_init bezel).
 * Side-effect: g_bpt_ui.edit_panel pole se prepise.
 */
void bpt_edit_panel_open_new_mem(uint16_t mem_addr, bool is_write);


#ifdef __cplusplus
}
#endif

#endif /* BPT_EDIT_PANEL_H */
