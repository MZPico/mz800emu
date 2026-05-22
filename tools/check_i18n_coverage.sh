#!/bin/bash
# check_i18n_coverage.sh — detekce UI stringů bez _() wrapperu
#
# Hledá string literály v ImGui volání (Button, MenuItem, Text, ...),
# které nejsou zabaleny v _() gettext makru.
#
# Použití: ./tools/check_i18n_coverage.sh [adresáře...]
#   Výchozí: src/ui-imgui src/ui
#
# Výstup: soubor:řádek: nalezený string
#
# Licence: GPLv3

SEARCH_DIRS="${@:-src/ui-imgui src/ui}"

# ImGui funkce, které přijímají uživatelsky viditelný text jako první argument
# Pattern: ImGui::Function("text" — bez předchozího _(
IMGUI_FUNCTIONS="Button|MenuItem|Text|TextWrapped|TextColored|TextDisabled|BulletText|TreeNode|TreeNodeEx|Selectable|SetTooltip|BeginTabItem|CollapsingHeader|BeginMenu|BeginCombo|InputText|SliderFloat|SliderInt|DragFloat|DragInt|Checkbox|RadioButton|BeginTable|TableSetupColumn|SetItemTooltip"

# Hledáme: ImGui::Func("string" — kde string NENÍ zabalený v _(
# Ale ignorujeme:
#   - ##id stringy (ImGui interní ID)
#   - prázdné stringy ""
#   - formátovací stringy začínající %
#   - komentáře

echo "=== i18n: pokrytí UI stringů ==="
echo "Prohledávané adresáře: $SEARCH_DIRS"
echo ""

TOTAL=0
MISSING=0

# Najdi všechny řádky s ImGui::Func(" kde text není v _()
while IFS= read -r line; do
    # Přeskočit řádky kde je _( před stringem
    if echo "$line" | grep -qE "($IMGUI_FUNCTIONS)\(\s*_\("; then
        continue
    fi
    # Přeskočit ## ID stringy
    if echo "$line" | grep -qE '($IMGUI_FUNCTIONS)\(\s*"##'; then
        continue
    fi
    # Přeskočit prázdné stringy
    if echo "$line" | grep -qE '($IMGUI_FUNCTIONS)\(\s*""'; then
        continue
    fi
    # Přeskočit komentáře
    if echo "$line" | grep -qE '^\s*//'; then
        continue
    fi

    MISSING=$((MISSING + 1))
    echo "  $line"
done < <(grep -rnE "($IMGUI_FUNCTIONS)\(\s*\"[^\"#]" $SEARCH_DIRS --include="*.cpp" --include="*.hpp" 2>/dev/null \
    | grep -v '_(' \
    | grep -v '^\s*//' \
    | grep -v '"##')

echo ""
if [ "$MISSING" -gt 0 ]; then
    echo "INFO: nalezeno $MISSING potenciálně nezabalených stringů"
    echo "      (některé mohou být falešné poplachy — technické ID, formáty)"
    exit 0  # informativní, ne blokující
else
    echo "PASS: všechny nalezené UI stringy jsou zabaleny v _()"
    exit 0
fi
