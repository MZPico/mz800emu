#!/usr/bin/env python3
# Copyright (c) 2026 Michal Hucik
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generátor C pole z binárního obrazu ROM plotteru MZ-1P16.

Z 4 KB binárky firmwaru vytvoří dvojici mz1p16_rom.{c,h}: zkompilované
pole bajtů embeddované do binárky emulátoru (stejně jako ostatní ROMy
mz800new). Spouští se ručně/Makefile krokem, ne při běžném buildu - ROM se
nemění.

Použití:
    python gen_rom.py <vstup.rom> <vystupni_adresar>

Vygeneruje <vystupni_adresar>/mz1p16_rom.c a mz1p16_rom.h.
"""
import sys
import os

ROM_SIZE = 4096
SYMBOL = "g_mz1p16_8050_rom"


def emit_c(data: bytes, out_dir: str) -> None:
    """Zapíše mz1p16_rom.c s polem bajtů.

    @param data    Obsah ROM (přesně ROM_SIZE bajtů).
    @param out_dir Cílový adresář.
    """
    path = os.path.join(out_dir, "mz1p16_rom.c")
    lines = []
    lines.append("/*")
    lines.append(" * Copyright (c) 2026 Michal Hucik")
    lines.append(" * SPDX-License-Identifier: GPL-3.0-or-later")
    lines.append(" *")
    lines.append(" * AUTOMATICKY GENEROVANO skriptem gen_rom.py - NEUPRAVOVAT RUCNE.")
    lines.append(" */")
    lines.append("/**")
    lines.append(" * @file mz1p16_rom.c")
    lines.append(" * @brief Embeddovany 4 KB firmware mikrokontroleru 8050 plotteru MZ-1P16.")
    lines.append(" *")
    lines.append(" * Pole je generovano z binarniho obrazu ROM (gen_rom.py). Vlastnikem dat")
    lines.append(" * je tento modul; jadro mcs48 dostava ukazatel pres mz1p16_rom_data().")
    lines.append(" */")
    lines.append("")
    lines.append('#include "mz1p16_rom.h"')
    lines.append("")
    lines.append("const uint8_t %s[MZ1P16_ROM_SIZE] = {" % SYMBOL)
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hexs = ", ".join("0x%02x" % b for b in chunk)
        lines.append("    %s," % hexs)
    lines.append("};")
    lines.append("")
    lines.append("const uint8_t *mz1p16_rom_data(void)")
    lines.append("{")
    lines.append("    return %s;" % SYMBOL)
    lines.append("}")
    lines.append("")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines))


def emit_h(out_dir: str) -> None:
    """Zapíše mz1p16_rom.h s deklaracemi.

    @param out_dir Cílový adresář.
    """
    path = os.path.join(out_dir, "mz1p16_rom.h")
    txt = '''/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AUTOMATICKY GENEROVANO skriptem gen_rom.py - NEUPRAVOVAT RUCNE.
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
'''
    with open(path, "w", newline="\n") as f:
        f.write(txt)


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("usage: gen_rom.py <input.rom> <output_dir>\n")
        return 2
    src, out_dir = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        data = f.read()
    if len(data) != ROM_SIZE:
        sys.stderr.write("error: ROM size %d != %d\n" % (len(data), ROM_SIZE))
        return 1
    os.makedirs(out_dir, exist_ok=True)
    emit_c(data, out_dir)
    emit_h(out_dir)
    sys.stderr.write("generated mz1p16_rom.{c,h} (%d bytes)\n" % len(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
