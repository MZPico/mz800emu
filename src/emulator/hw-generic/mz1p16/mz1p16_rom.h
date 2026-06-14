/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
/**
 * @file mz1p16_rom.h
 * @brief Embeddovany firmware 8050 plotteru MZ-1P16 - verejne API.
 */
#ifndef MZ1P16_ROM_H
#define MZ1P16_ROM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Velikost firmwaru plotteru v bajtech (4 KB). */
#define MZ1P16_ROM_SIZE 4096

/** Embeddovane pole firmwaru (vlastnik = tento modul). */
extern const uint8_t g_mz1p16_8050_rom[MZ1P16_ROM_SIZE];

/**
 * @brief Vrati ukazatel na embeddovany firmware plotteru.
 * @return Ukazatel na MZ1P16_ROM_SIZE bajtu ROM (platny po celou dobu behu).
 */
const uint8_t *mz1p16_rom_data(void);

#ifdef __cplusplus
}
#endif

#endif /* MZ1P16_ROM_H */
