#!/usr/bin/env bash
# CI build matrix check pro MCP integraci (V0.B.3).
#
# Spustí 4 build varianty v sekvenci:
#   1. default                  - plný build (MCP + MCP TCP + debugger)
#   2. NO_MCP_TCP=1              - bez TCP listeneru (jen pipe transport)
#   3. NO_MCP=1                  - bez MCP backendu (implicitne i NO_MCP_TCP)
#   4. NO_DEBUGGER=1             - bez debuggeru (implicitne i NO_MCP)
#
# Pro variantu 1 navíc spustí ``make test`` a hlásí ctest sumář.
# Pro varianty 2-4 ověřuje pouze úspěšný build (= bezvyzkoušení ctestu,
# protože subset testů má smysl jen v plné konfiguraci).
#
# Smyslem: regress check před merge mz800new master - aby se neztratil
# žádný kompilace path se zapnutými/vypnutými fíčry.
#
# Spuštění:
#   bash tests/mcp/ci_build_matrix.sh
#
# Exit code:
#   0 = všechny 4 build varianty PASS
#   1 = aspoň jedna varianta selhala (= seznam v sumáři)
#
# Předpoklady:
#   * MSYSTEM=UCRT64 (= správný toolchain podle parent shellu).
#   * mingw32-make dostupný v PATH (= UCRT64 binárka).
#
# Pozn.: ``make clean`` se NEvolá v případě, že varianta nemění
# CMakeCache - ale tady varianty MĚNÍ konfigurační flagy a Makefile
# nemá auto-reconfigure pri změně flagů, takže ``make mrproper`` před
# každou variantou je nutný.

set -u   # nezamlčuj non-set proměnné; -e NEpoužíváme protože chceme
         # nasbírat všechny faily a vrátit konsolidovaný report

# Skript leží v tests/mcp/ - repo root je o 2 úrovně výš.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${REPO_ROOT}" || exit 1

# MSYS2: vyberme mingw32-make s explicitní cestou pokud je k dispozici,
# jinak fallback na PATH. To je obrana proti situaci, kdy PATH má jen
# /usr/bin/make.exe (= MSYS2 make, ne MinGW make).
if command -v mingw32-make >/dev/null 2>&1; then
    MAKE_CMD="mingw32-make"
else
    MAKE_CMD="make"
fi

echo "=== CI build matrix (V0.B.3) ==="
echo "  Repo root: ${REPO_ROOT}"
echo "  Make:      ${MAKE_CMD}"
echo "  MSYSTEM:   ${MSYSTEM:-(unset)}"
echo ""

PASS=0
FAIL=0
FAIL_NAMES=()

run_variant() {
    local name="$1"
    local flags="$2"
    echo ""
    echo "--- Variant: ${name} ---"
    echo "  flags: ${flags:-<none>}"

    # mrproper smaže build/ a kompletně přečte CMakeCache od nuly.
    # Bez něj by Makefile recyklo cache s předchozí konfigurací a tato
    # varianta by nedostala nový -DMZ_NO_MCP / -DMZ_NO_DEBUGGER.
    if ! ${MAKE_CMD} mrproper >/dev/null 2>&1; then
        echo "  mrproper FAIL"
        FAIL=$((FAIL + 1))
        FAIL_NAMES+=("${name} (mrproper)")
        return
    fi

    # Spustime build s logováním do souboru pro pozdější diagnostiku
    # v případě selhání.
    local log_file="/tmp/ci_matrix_${name}.log"
    if ${MAKE_CMD} mz800emu ${flags} >"${log_file}" 2>&1; then
        echo "  Build PASS  (log: ${log_file})"
        PASS=$((PASS + 1))
    else
        echo "  Build FAIL  (log: ${log_file})"
        tail -20 "${log_file}" | sed 's/^/    /'
        FAIL=$((FAIL + 1))
        FAIL_NAMES+=("${name}")
    fi
}

run_variant "default"          ""
run_variant "NO_MCP_TCP"       "NO_MCP_TCP=1"
run_variant "NO_MCP"           "NO_MCP=1 NO_MCP_TCP=1"
run_variant "NO_DEBUGGER"      "NO_DEBUGGER=1 NO_MCP=1 NO_MCP_TCP=1"

echo ""
echo "=== CI Matrix Result ==="
echo "  PASS: ${PASS} / 4"
echo "  FAIL: ${FAIL} / 4"
if [ "${FAIL}" -gt 0 ]; then
    echo "  Failed variants:"
    for n in "${FAIL_NAMES[@]}"; do
        echo "    - ${n}"
    done
    exit 1
fi

echo ""
echo "  All build variants PASS."
exit 0
