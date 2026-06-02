#!/usr/bin/env bash
# mcpinit.sh - inicializace Python venv + generace .mcp.json pro tento
# konkrétní install location.
#
# Idempotentní setup skript:
#   * vytvoří .venv pokud chybí (na MSYS2 mingw Pythonu s
#     --system-site-packages kvuli pydantic-core)
#   * pip install -r requirements.txt (pri selhani da akcni remediaci,
#     negeneruje .mcp.json dokud zavislosti nejsou OK)
#   * sanity check že FastMCP load
#   * vygeneruje .mcp.json s absolutními cestami pro tento install
#     (= staré .mcp.json je zazálohováno jako .mcp.json.bak pokud se
#     liší od nového obsahu)
#   * vypíše souhrn pro uživatele
#
# MSYS2 pozn.: mingw/ucrt Python neumi pip-zbuildit Rust-native deps
# mcp[cli] (pydantic-core, rpds-py, ...). Skript proto na mingw Pythonu
# automaticky sahne po 'py -3' launcheru (python.org, prebuilt wheels).
# Viz docs/{cz,en}/mcp-server/python-wrapper.md sekce MSYS2 caveats.
# Vsechny user-facing echo hlasky jsou anglicky (komentare cesky).
#
# Spouštěj z adresáře mcp-server/ NEBO odkudkoliv s explicit cestou:
#   ./mcpinit.sh
#   bash /path/to/mcp-server/mcpinit.sh
#
# Po dokončení venv NENÍ aktivovaný v rodičovském shellu - to není
# potřeba, Claude Code spustí mcp_server.py přímo přes cestu v .mcp.json.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ ! -f requirements.txt ]]; then
    echo "ERROR: requirements.txt not found in $SCRIPT_DIR" >&2
    echo "       Are you running mcpinit.sh from mcp-server/?" >&2
    exit 1
fi

# Vyber Python interpretu pro vytvoreni venv.
#
# Problem: mcp[cli] ma vic Rust-native zavislosti (pydantic-core, rpds-py,
# ...), ktere MSYS2 mingw/ucrt Python (platform 'mingw_x86_64_*') NEUMI
# pip-zbuildit z Rustu ("Unsupported platform: mingw_*"). Reseni neni
# pacman po kusech (whack-a-mole, nektere deps balicek nemaji), ale pouzit
# CPython s prebuilt wheels (python.org). Ten ma 'py -3' launcher.
#
# Priorita vyberu interpretu:
#   1. $PYTHON override (uzivatel explicitne)
#   2. 'python' z PATH, pokud NENI mingw build
#   3. 'py -3' launcher (Windows python.org), pokud existuje a neni mingw
#   4. 'python3' z PATH (Linux/macOS, kde 'python' casto chybi), pokud NENI mingw
#   5. fallback 'python' (i mingw) -> pip nejspis selze -> remediacni hlaska
py_platform() { "$@" -c 'import sysconfig; print(sysconfig.get_platform())' 2>/dev/null || echo unknown; }

PY_CMD=""
if [[ -n "${PYTHON:-}" ]]; then
    PY_CMD="$PYTHON"
elif command -v python >/dev/null 2>&1 && [[ "$(py_platform python)" != *mingw* ]] \
        && [[ "$(py_platform python)" != unknown ]]; then
    PY_CMD="python"
elif command -v py >/dev/null 2>&1 && [[ "$(py_platform py -3)" != *mingw* ]] \
        && [[ "$(py_platform py -3)" != unknown ]]; then
    PY_CMD="py -3"
    echo "[info] 'python' is an MSYS2 mingw build (unsuitable for mcp[cli] Rust deps);"
    echo "       using 'py -3' (python.org, has prebuilt wheels)."
elif command -v python3 >/dev/null 2>&1 && [[ "$(py_platform python3)" != *mingw* ]] \
        && [[ "$(py_platform python3)" != unknown ]]; then
    # Linux/macOS: 'python' binarka casto neexistuje, jen 'python3'.
    PY_CMD="python3"
else
    PY_CMD="python"
fi

IS_MINGW_PY=0
[[ "$(py_platform $PY_CMD)" == *mingw* ]] && IS_MINGW_PY=1

# pacman prefix podle MSYSTEM toolchainu (pouzito jen v remediacni hlasce
# nize; definovano vzdy kvuli set -u).
case "${MSYSTEM:-}" in
    MINGW64) MSYS2_PKG_PREFIX="mingw-w64-x86_64-"       ;;
    CLANG64) MSYS2_PKG_PREFIX="mingw-w64-clang-x86_64-" ;;
    *)       MSYS2_PKG_PREFIX="mingw-w64-ucrt-x86_64-"  ;;
esac

VENV_OPTS=""
# Jen kdyz jsme nuceni do mingw Pythonu (zadny non-mingw nenalezen) zkusime
# zachranu pres --system-site-packages + pacman (viz remediacni hlaska nize).
[[ "$IS_MINGW_PY" == "1" ]] && VENV_OPTS="--system-site-packages"

# --- [1/4] venv setup ----------------------------------------------------
# Recreate existujici venv pokud jeho python je mingw (= nepouzitelny pro
# mcp[cli]) - typicky po drivejsim neuspesnem behu se starym skriptem.
# Tim se mzdos-style rozbity mingw venv nahradi vybranym (py -3) Pythonem.
NEED_CREATE=0
if [[ ! -d .venv ]]; then
    NEED_CREATE=1
else
    EXIST_PY=".venv/Scripts/python.exe"
    [[ -x "$EXIST_PY" ]] || EXIST_PY=".venv/bin/python.exe"
    [[ -x "$EXIST_PY" ]] || EXIST_PY=".venv/bin/python"
    if [[ -x "$EXIST_PY" ]] && [[ "$(py_platform "$EXIST_PY")" == *mingw* ]] \
            && [[ "$IS_MINGW_PY" == "0" ]]; then
        echo "[1/4] existing .venv is MSYS2 mingw (unusable for mcp[cli]); recreating with the selected Python ..."
        rm -rf .venv
        NEED_CREATE=1
    fi
fi
if [[ "$NEED_CREATE" == "1" ]]; then
    echo "[1/4] Creating .venv $VENV_OPTS (via: $PY_CMD) ..."
    $PY_CMD -m venv $VENV_OPTS .venv
else
    echo "[1/4] .venv exists, skipping venv creation"
fi

# Detekce venv layoutu - podle skutečně vytvořené struktury, ne podle
# uname. Tři běžné varianty:
#   Scripts/python.exe   = Windows-native Python (z python.org)
#   bin/python.exe       = MSYS2 mingw/ucrt Python (= POSIX dir layout
#                          ale Windows binárka)
#   bin/python           = Linux / macOS / čistý POSIX Python
# Předchozí 'uname -s' heuristika selhávala v MSYS2 UCRT64 kde uname
# vrací MINGW64_NT-... ale Python venv má bin/python.exe layout.
if [[ -x ".venv/Scripts/python.exe" ]]; then
    VENV_BIN="Scripts"
    EXE_SUFFIX=".exe"
elif [[ -x ".venv/bin/python.exe" ]]; then
    VENV_BIN="bin"
    EXE_SUFFIX=".exe"
elif [[ -x ".venv/bin/python" ]]; then
    VENV_BIN="bin"
    EXE_SUFFIX=""
else
    echo "ERROR: venv was created but no python binary found in" >&2
    echo "       .venv/Scripts/python.exe or .venv/bin/python[.exe]" >&2
    ls -la .venv/Scripts/ .venv/bin/ 2>&1 || true
    exit 3
fi

# Suffix pro mz800emu binárku (= Windows má .exe, jinde ne). Determinujeme
# stejně jako u Pythonu - podle toho co venv layout říká.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EMU_EXE_SUFFIX=".exe" ;;
    *)                    EMU_EXE_SUFFIX=""     ;;
esac

# --- [2/4] install deps --------------------------------------------------
echo "[2/4] Installing requirements (pip install -r requirements.txt) ..."
# Spustit pip přes venv Pythonu - tím nepotřebujeme zdrojovat activate
# (= bezpečnější pro idempotent / scripted use). pip upgrade je
# best-effort (selhani nesmi shodit cely setup).
"./.venv/$VENV_BIN/python" -m pip install --quiet --upgrade pip || true
if ! "./.venv/$VENV_BIN/python" -m pip install --quiet -r requirements.txt; then
    echo "" >&2
    echo "ERROR: pip install -r requirements.txt failed." >&2
    if [[ "$IS_MINGW_PY" == "1" ]]; then
        echo "" >&2
        echo "Cause: the selected Python is an MSYS2 ${MSYSTEM:-mingw} build and no" >&2
        echo "non-mingw Python was found. mcp[cli] has several Rust-native deps" >&2
        echo "(pydantic-core, rpds-py, ...) that a mingw Python cannot pip-build" >&2
        echo "('Unsupported platform: mingw_*')." >&2
        echo "" >&2
        echo "RECOMMENDED FIX: install a vanilla Windows Python from python.org" >&2
        echo "(it has prebuilt wheels for all of these). The script then finds it" >&2
        echo "via the 'py -3' launcher, or point at it explicitly:" >&2
        echo "" >&2
        echo "    PYTHON='C:/path/to/python.exe' ./mcpinit.sh" >&2
        echo "" >&2
        echo "Fragile alternative: pacman-install EVERY Rust dep, e.g." >&2
        echo "    pacman -S ${MSYS2_PKG_PREFIX}python-pydantic ${MSYS2_PKG_PREFIX}python-rpds-py" >&2
        echo "    ./mcpinit.sh   (venv has --system-site-packages)" >&2
        echo "but some deps may have no pacman package -> python.org is safer." >&2
    else
        echo "Check the pip output above for details." >&2
    fi
    echo "" >&2
    echo ".mcp.json was NOT generated - fix dependencies first, then re-run" >&2
    echo "mcpinit.sh." >&2
    exit 2
fi

# --- [3/4] sanity check --------------------------------------------------
echo "[3/4] Sanity check 'import mcp' ..."
if ! "./.venv/$VENV_BIN/python" -c "import mcp" 2>/dev/null; then
    echo "ERROR: 'import mcp' failed after install" >&2
    echo "       Try: ./.venv/$VENV_BIN/python -m pip install -r requirements.txt" >&2
    exit 2
fi

# --- [4/4] generate .mcp.json --------------------------------------------
echo "[4/4] Generating .mcp.json with current install paths ..."

# Compute absolutní cesty.
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VENV_PYTHON_ABS="$(cd ".venv/$VENV_BIN" && pwd)/python${EXE_SUFFIX}"
MCP_SERVER_PY_ABS="$SCRIPT_DIR/mcp_server.py"
MZ800EMU_EXE_ABS="$REPO_ROOT/mz800emu${EMU_EXE_SUFFIX}"

# Convert na Windows-friendly (= forward-slash) paths pro JSON na MSYS2.
# Claude Code na Windows preferuje 'C:/msys64/...' před '/c/msys64/...'.
if command -v cygpath >/dev/null 2>&1; then
    REPO_ROOT_JSON="$(cygpath -m "$REPO_ROOT")"
    VENV_PYTHON_JSON="$(cygpath -m "$VENV_PYTHON_ABS")"
    MCP_SERVER_PY_JSON="$(cygpath -m "$MCP_SERVER_PY_ABS")"
    MZ800EMU_EXE_JSON="$(cygpath -m "$MZ800EMU_EXE_ABS")"
else
    REPO_ROOT_JSON="$REPO_ROOT"
    VENV_PYTHON_JSON="$VENV_PYTHON_ABS"
    MCP_SERVER_PY_JSON="$MCP_SERVER_PY_ABS"
    MZ800EMU_EXE_JSON="$MZ800EMU_EXE_ABS"
fi

# Varování pokud mz800emu.exe ještě nesestaven.
if [[ ! -f "$MZ800EMU_EXE_ABS" ]]; then
    echo "  WARN: $MZ800EMU_EXE_ABS not found"
    echo "        Build first: cd '$REPO_ROOT' && mingw32-make mz800emu"
    echo "        .mcp.json will contain this path, so it becomes valid"
    echo "        right after the build."
fi

# Generate nový .mcp.json do dočasného souboru.
NEW_JSON="$(mktemp)"
cat > "$NEW_JSON" <<EOF
{
  "mcpServers": {
    "mz800emu": {
      "command": "$VENV_PYTHON_JSON",
      "args": ["$MCP_SERVER_PY_JSON"],
      "cwd": "$REPO_ROOT_JSON",
      "env": {
        "MZ800EMU_EXE": "$MZ800EMU_EXE_JSON",
        "MZ800EMU_TRANSPORT": "pipe",
        "MZ800EMU_USER_KB_DIR": ""
      }
    },
    "mz800emu-tcp": {
      "command": "$VENV_PYTHON_JSON",
      "args": ["$MCP_SERVER_PY_JSON"],
      "cwd": "$REPO_ROOT_JSON",
      "env": {
        "MZ800EMU_TRANSPORT": "tcp",
        "MZ800EMU_TCP_HOST": "127.0.0.1",
        "MZ800EMU_TCP_PORT": "23800",
        "MZ800EMU_USER_KB_DIR": ""
      }
    }
  }
}
EOF

# Zachovat starý .mcp.json jako .bak pokud se liší od nového.
if [[ -f .mcp.json ]] && ! cmp -s .mcp.json "$NEW_JSON"; then
    cp -f .mcp.json .mcp.json.bak
    echo "  Existing .mcp.json backed up as .mcp.json.bak"
fi
mv -f "$NEW_JSON" .mcp.json

# --- Souhrn --------------------------------------------------------------
cat <<EOF

============================================================
OK - venv + .mcp.json ready.

Generated $SCRIPT_DIR/.mcp.json with:

  Python interpreter : $VENV_PYTHON_JSON
  MCP server script  : $MCP_SERVER_PY_JSON
  Working directory  : $REPO_ROOT_JSON
  Emulator binary    : $MZ800EMU_EXE_JSON

Optional - user knowledge base:
  The "env" blocks contain an empty "MZ800EMU_USER_KB_DIR". Set it to
  a directory of your own Markdown notes to expose them to AI clients
  as emulator://kb/<topic> (scanned recursively). Empty = disabled.

Next step: copy .mcp.json to ~/.claude/.mcp.json (user-level)
or <project-root>/.claude/.mcp.json (project-level), edit the env if
needed, then restart Claude Code.
============================================================
EOF
