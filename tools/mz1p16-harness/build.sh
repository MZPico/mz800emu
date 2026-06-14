#!/usr/bin/env bash
# Copyright (c) 2026 Michal Hucik
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build standalone harnessu kreslicího self-testu plotteru MZ-1P16.
# Linkuje embeddovaný firmware (mz1p16_rom) + jádro 8050 (mcs48) + model
# mechaniky (mz1p16). Bez CMake, bez SDL/ImGui - čistě C.
#
# Pouziti (z korene repa mz800new-mz1p16):
#   tools/mz1p16-harness/build.sh
# Pak:
#   tools/mz1p16-harness/mz1p16_drawtest.exe [sekundy] [vystup.png]
set -euo pipefail

# Koren repa = o dve urovne vyse nez tento skript.
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC="$ROOT/src/emulator/hw-generic/mz1p16"

# -pipe: UCRT64 gcc v tomto prostredi nedokaze zapsat docasne soubory
# (TEMP smeruje do neprzapisovatelne C:\WINDOWS); -pipe je obejde.
CORE=("$SRC/mcs48.c" "$SRC/mz1p16.c" "$SRC/mz1p16_rom.c")

gcc -pipe -O2 -Wall -Wextra -I "$ROOT/src" \
    "$HERE/mz1p16_drawtest.c" "${CORE[@]}" \
    -o "$HERE/mz1p16_drawtest.exe"
echo "built: $HERE/mz1p16_drawtest.exe"

# Offline tisk zachycenych dat (printer-*.bin) realnym firmwarem plotteru.
gcc -pipe -O2 -Wall -Wextra -I "$ROOT/src" \
    "$HERE/mz1p16_print.c" "${CORE[@]}" \
    -o "$HERE/mz1p16_print.exe"
echo "built: $HERE/mz1p16_print.exe"

# Diagnostické nástroje (probe = měření pen/stepper/PC; disasm = MCS-48
# disassembler firmwaru). Slouží k analýze chování, ne k produkci výstupu.
gcc -pipe -O2 -Wall -Wextra -I "$ROOT/src" \
    "$HERE/mz1p16_probe.c" "${CORE[@]}" \
    -o "$HERE/mz1p16_probe.exe"
echo "built: $HERE/mz1p16_probe.exe"

gcc -pipe -O2 -Wall -Wextra -I "$ROOT/src" \
    "$HERE/disasm.c" "$SRC/mz1p16_rom.c" \
    -o "$HERE/disasm.exe"
echo "built: $HERE/disasm.exe"
