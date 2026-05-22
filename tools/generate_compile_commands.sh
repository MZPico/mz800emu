#!/bin/bash
#
# Generuje compile_commands.json z výstupu make -n -B (dry run).
# Použití: bash tools/generate_compile_commands.sh [target]
#   target - cíl make (výchozí: mz800emu)
#
# Výstup: compile_commands.json v kořeni projektu
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

TARGET="${1:-mz800emu}"
OUTPUT="$PROJECT_DIR/compile_commands.json"

# Windows/MSYS2 vyžaduje explicitní parametry pro make
MAKE_EXTRA_ARGS=""
if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then
    MAKE_EXTRA_ARGS="PLATFORM=Windows TEMP=/tmp TMP=/tmp WINDRES=windres"
fi

echo "Generuji compile_commands.json pro cíl '$TARGET'..."

# Absolutní cesta projektu v nativním formátu (Windows: C:/...)
PROJECT_DIR_NATIVE="$(cygpath -m "$PROJECT_DIR" 2>/dev/null || echo "$PROJECT_DIR")"

# Uložit make dry run do dočasného souboru
TMPFILE=$(mktemp /tmp/make_dryrun.XXXXXX)
trap "rm -f '$TMPFILE'" EXIT

cd "$PROJECT_DIR"
make -n -B "$TARGET" $MAKE_EXTRA_ARGS 2>/dev/null > "$TMPFILE"

# Pomocí Python3 parsujeme výstup a generujeme JSON
python3 - "$TMPFILE" "$PROJECT_DIR_NATIVE" "$OUTPUT" << 'PYTHON_EOF'
import sys
import json
import os
import posixpath
import re

tmpfile = sys.argv[1]
project_dir = sys.argv[2]
output_file = sys.argv[3]

entries = []

with open(tmpfile, 'r') as f:
    for line in f:
        line = line.strip()

        # Hledáme řádky s gcc/g++ obsahující '-c ' (kompilace, ne linkování)
        if not re.match(r'^(gcc|g\+\+)\s', line):
            continue
        if ' -c ' not in line:
            continue

        # Najdeme zdrojový soubor (argument za -c)
        parts = line.split()
        try:
            c_idx = parts.index('-c')
            source_file = parts[c_idx + 1]
        except (ValueError, IndexError):
            continue

        # Absolutní cesta ke zdrojovému souboru
        if not os.path.isabs(source_file) and not re.match(r'^[A-Za-z]:', source_file):
            source_file = project_dir + '/' + source_file
        # Normalizace cesty (odstranění /../ apod.)
        source_file = posixpath.normpath(source_file)

        # Odstranění -MMD -MP (dependency tracking — irelevantní pro Sourcetrail)
        cmd = line.replace(' -MMD', '').replace(' -MP', '')

        # Převod relativních -I cest na absolutní
        def make_abs_path(match):
            path = match.group(2)
            if path.startswith('/') or re.match(r'^[A-Za-z]:', path):
                full = path
            else:
                full = project_dir + '/' + path
            return '-I' + posixpath.normpath(full)

        cmd = re.sub(r'-I(\./)?([^ ]+)', make_abs_path, cmd)

        entries.append({
            'directory': project_dir,
            'command': cmd,
            'file': source_file
        })

with open(output_file, 'w') as f:
    json.dump(entries, f, indent=2, ensure_ascii=False)
    f.write('\n')

print(f"Vygenerovano {len(entries)} zaznamu do {output_file}")
PYTHON_EOF
