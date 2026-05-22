#!/bin/bash
# Najde zdrojové soubory pro xgettext (per-architektura)
# Použití: i18n_find_sources.sh <MZARCH>
# Výstup: seznam zdrojových souborů na stdout

MZARCH="${1:-800}"
COMMON_DIRS="src/ui-imgui src/ui src/baseui src/emulator"
ARCH_DIR="src/emulator/mzarch/mz${MZARCH}"

# Společné zdrojové soubory (bez arch-specifických adresářů)
for dir in $COMMON_DIRS; do
    find "$dir" -name "*.cpp" -o -name "*.hpp" -o -name "*.c" -o -name "*.h"
done | grep -v "src/emulator/mzarch/mz700/" \
     | grep -v "src/emulator/mzarch/mz800/" \
     | grep -v "src/emulator/mzarch/mz1500/"

# Arch-specifické soubory (pokud adresář existuje)
if [ -d "$ARCH_DIR" ]; then
    find "$ARCH_DIR" -name "*.cpp" -o -name "*.hpp" -o -name "*.c" -o -name "*.h"
fi
