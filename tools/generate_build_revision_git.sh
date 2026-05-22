#!/bin/bash

# Print help function
print_help() {
    echo "Usage: $0 <output_file>"
    echo ""
    echo "This script creates a C source file containing the current build"
    echo "revision number derived from git."
    echo ""
    echo "The revision is only generated for the official upstream repository"
    echo "github.com/michalhucik/mz800emu (HTTPS, SSH or gh CLI form). For any"
    echo "other clone (fork, mirror, local-only repo) the revision is set to -1."
    echo ""
    echo "Calculation:"
    echo "  - upstream:   REV_INT = 250 + total commit count on HEAD"
    echo "                (offset 250 preserves revision-number continuity"
    echo "                after the project's migration from SourceForge"
    echo "                (SVN) to GitHub (git); historical SVN revisions"
    echo "                ended below 250, so the new git numbering starts"
    echo "                strictly above the last released SF revision)"
    echo "  - otherwise:  REV_INT = -1, REV_TEXT = \"???\""
    echo ""
    echo "Intended for use during the build process (Makefile or CMake)."
    echo ""
    echo "Arguments:"
    echo "  <output_file>   The path to the output C source file."
}

# Check number of arguments
if [ "$#" -ne 1 ]; then
    echo "Error: Missing output file argument."
    print_help
    exit 1
fi

OUTPUT_FILE="$1"
SCRIPT_PATH="$(realpath "$0")"
DATETIME="$(date +"%Y-%m-%d %H:%M:%S")"

# Offset zachovává kontinuitu číslování revizí po přechodu projektu
# ze SourceForge (SVN) na GitHub (git). Historické SVN revize na
# SourceForge skončily pod hodnotou 250; první git revize tedy
# pokračuje od 250 + 1 a žádné dříve vydané číslo revize se nikdy
# znovu nepoužije. Díky tomu zůstanou všechna stará čísla revizí
# (uvedená v binárkách distribuovaných ze SourceForge, ve zprávách
# o chybách, screenshotech atd.) jednoznačně identifikovatelná.
REV_OFFSET=250

# Regex, který identifikuje oficiální upstream repozitář. Pokrývá všechny
# obvyklé varianty URL, které git/gh produkují:
#   https://github.com/michalhucik/mz800emu(.git)?
#   git@github.com:michalhucik/mz800emu(.git)?
#   ssh://git@github.com/michalhucik/mz800emu(.git)?
# Porovnání je case-insensitive (GitHub URL jsou v praxi case-insensitive
# v host části i v path části jména repozitáře).
UPSTREAM_REGEX='github\.com[:/]michalhucik/mz800emu(\.git)?/?$'

echo "Generating build revision file: $OUTPUT_FILE"

# Default revision values (= "neznámý / nepodporovaný repozitář")
REV_TEXT="???"
REV_INT="-1"

# Detekce git a ověření, že běžíme uvnitř git pracovního stromu.
if ! command -v git >/dev/null 2>&1; then
    echo "Warning: git command not found. Using default values."
elif ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "Warning: not inside a git working tree. Using default values."
else
    # Načti všechny remote URL (každý remote může mít fetch i push URL).
    REMOTE_URLS=""
    while IFS= read -r remote; do
        [ -z "$remote" ] && continue
        for kind in fetch push; do
            url=$(git remote get-url --"$kind" "$remote" 2>/dev/null || true)
            [ -n "$url" ] && REMOTE_URLS+="$url"$'\n'
        done
    done < <(git remote 2>/dev/null)

    # Zkontroluj, jestli alespoň jeden remote ukazuje na oficiální upstream.
    IS_UPSTREAM=0
    if [ -n "$REMOTE_URLS" ]; then
        # shopt nocasematch zajistí case-insensitive porovnání bez závislosti
        # na grep flagách.
        shopt -s nocasematch
        while IFS= read -r url; do
            [ -z "$url" ] && continue
            if [[ "$url" =~ $UPSTREAM_REGEX ]]; then
                IS_UPSTREAM=1
                MATCHED_URL="$url"
                break
            fi
        done <<< "$REMOTE_URLS"
        shopt -u nocasematch
    fi

    if [ "$IS_UPSTREAM" -eq 1 ]; then
        if REV_COUNT=$(git rev-list --count HEAD 2>/dev/null); then
            REV_INT=$(( REV_OFFSET + REV_COUNT ))
            REV_TEXT="$REV_INT"
            echo "Upstream remote matched: $MATCHED_URL"
            echo "Git commits on HEAD: $REV_COUNT, offset: $REV_OFFSET, revision: $REV_INT"
        else
            echo "Warning: upstream remote matched but git rev-list failed. Using default values."
        fi
    else
        echo "Warning: no remote points to github.com/michalhucik/mz800emu."
        echo "         Build revision set to -1 (this is not the official upstream clone)."
    fi
fi

# Write the output file
echo "Writing output file..."
cat <<EOF > "$OUTPUT_FILE"
/* This file is automatically created by $SCRIPT_PATH */
/* Do not edit! */
const char *build_revision_get_const_char(void) { return "Revision: $REV_TEXT"; };
int build_revision_get_int(void) { return $REV_INT; };
EOF

echo "Done."
