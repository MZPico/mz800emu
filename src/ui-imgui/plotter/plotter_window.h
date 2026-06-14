/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file plotter_window.h
 * @brief GUI okno plotteru Sharp MZ-1P16 - deklarace.
 *
 * ImGui okno se živým náhledem kresby plotteru (stroke buffer @ref g_mz1p16,
 * vykreslený přes ImGui DrawList), toolbarem (Save PNG / Reset / Paper feed /
 * tlačítka self-testu) a statusbarem (X, Y, barva, pero, BUSY).
 *
 * Okno je gateováno na architektury se Z80 PIO (MZ-800, MZ-1500), stejně jako
 * Centronics tiskárna; na ostatních je render no-op.
 *
 * Vztah okno <-> plotter: plotter je AKTIVNÍ jen když je okno otevřené. Při
 * prvním renderu (open) se zavolá mz1p16_emu_set_active(true), při zavření [X]
 * mz1p16_emu_set_active(false). Plotter se tak při zavřeném okně tváří
 * odpojený (MCU se nekrokuje).
 */

#ifndef PLOTTER_WINDOW_H_INCLUDED
#define PLOTTER_WINDOW_H_INCLUDED

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Render okna plotteru MZ-1P16.
     *
     * Vykreslí okno se živým náhledem kresby + ovládáním. Při otevření okna
     * (přechod zavřeno->otevřeno) aktivuje plotter, při zavření ho deaktivuje
     * (drží konzistenci active flagu se stavem okna).
     *
     * @param p_open Pointer na bool s viditelností okna (ImGui idiom). Při
     *               zavření [X] se nastaví na false.
     *
     * Preconditions: ImGui kontext aktivní (volání mezi NewFrame a Render).
     * Side effects: může volat mz1p16_emu_set_active / reset / self-test /
     *               PNG export (file dialog). Na archu bez Z80 PIO no-op.
     */
    void imgui_plotter_window(bool *p_open);

#ifdef __cplusplus
}
#endif

#endif /* PLOTTER_WINDOW_H_INCLUDED */
