#!/usr/bin/env bash

set -e

# Function to print help
print_help() {
    echo "Usage: $0 <path-to-exe> <target-dir>"
    echo
    echo "This script collects all MSYS2 DLL dependencies of a given .exe file"
    echo "and copies them into a specified directory. It resolves dependencies recursively."
    echo
    echo "Arguments:"
    echo "  <path-to-exe>   Path to the executable file"
    echo "  <target-dir>    Directory where DLLs will be copied"
    echo
    echo "Only DLLs located inside MSYS2 paths will be copied (e.g. /mingw64/bin/...)."
    echo
    echo "Example:"
    echo "  $0 ./myapp.exe ./distrib"
}

# Check for help
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    print_help
    exit 0
fi

# Check argument count
if [[ $# -ne 2 ]]; then
    echo "Error: Invalid number of arguments."
    print_help
    exit 1
fi

EXE="$1"
DEST_DIR="$2"

# Validate input file
if [[ ! -f "$EXE" ]]; then
    echo "Error: Executable '$EXE' does not exist."
    exit 2
fi

# Validate target directory
if [[ ! -d "$DEST_DIR" ]]; then
    echo "Creating target directory '$DEST_DIR'..."
    mkdir -p "$DEST_DIR" || {
        echo "Error: Failed to create directory '$DEST_DIR'"
        exit 3
    }
fi

# Define valid MSYS2 base paths
MSYS_PATHS=("/mingw64" "/mingw32" "/ucrt64" "/clang64" "/clang32")

# Function to check if a DLL path is from MSYS2
is_msys2_dll() {
    local path="$1"
    for prefix in "${MSYS_PATHS[@]}"; do
        if [[ "$path" == "$prefix"* ]]; then
            return 0
        fi
    done
    return 1
}

# Set to track already processed DLLs
declare -A PROCESSED

# Function to recursively collect DLLs
collect_dlls() {
    local binary="$1"

    ldd "$binary" | grep -oE '/[^ ]+\.dll' | while read -r dll; do
        if ! is_msys2_dll "$dll"; then
            echo "Skipping system DLL: $dll"
            continue
        fi

        base_dll="$(basename "$dll")"

        if [[ ${PROCESSED["$base_dll"]} ]]; then
            continue
        fi

        PROCESSED["$base_dll"]=1

        if [[ -f "$DEST_DIR/$base_dll" ]]; then
            echo "Skipping already existing $base_dll"
            continue
        fi

        echo "Copying $base_dll"
        cp "$dll" "$DEST_DIR/" || {
            echo "Warning: Failed to copy $dll"
            continue
        }

        collect_dlls "$dll"
    done
}

# Start with the main executable
collect_dlls "$EXE"

echo "Done."
