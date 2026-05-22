# cmake/AddMzEmu.cmake
#
# Helper funkce mz_add_emulator() která vytvoří per-arch executable (mz800emu,
# mz700emu, mz1500emu).
#
# Sources jsou společné pro všechny architektury (DIRS_MAIN, DIRS_EMULATOR,
# flat src/*.c, ...) - liší se pouze MZARCH define a per-arch podadresáře
# (src/emulator/mzarch/mz<N>/, src/ui-imgui/mz<N>/).
#
# Knihovny v src/libs/ jsou arch-independent a linkují se jen jednou (sdílené
# přes všechny arch executable).

# ----------------------------------------------------------------------------
# Pomocná funkce: rekurzivně sebere .c/.cpp z adresáře.
# (CMake nemá globbing v add_executable jako Makefile $(shell find), tak ho
# tady simulujeme přes file(GLOB_RECURSE).
#
# POZN: GLOB_RECURSE nezachycuje nově přidané soubory bez re-configure!
# Pokud přidáš .c/.cpp soubor, musíš ručně spustit `cmake .` (nebo smazat
# CMakeCache).
# ----------------------------------------------------------------------------
function(mz_glob_sources out_var)
    set(_all "")
    foreach(_dir IN LISTS ARGN)
        file(GLOB_RECURSE _src CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/${_dir}/*.c"
            "${CMAKE_SOURCE_DIR}/${_dir}/*.cpp"
        )
        list(APPEND _all ${_src})
    endforeach()
    set(${out_var} ${_all} PARENT_SCOPE)
endfunction()

# ----------------------------------------------------------------------------
# Pomocná funkce: flat scan adresáře (bez rekurze do podadresářů).
# ----------------------------------------------------------------------------
function(mz_glob_flat out_var)
    set(_all "")
    foreach(_dir IN LISTS ARGN)
        file(GLOB _src CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/${_dir}/*.c"
            "${CMAKE_SOURCE_DIR}/${_dir}/*.cpp"
        )
        list(APPEND _all ${_src})
    endforeach()
    set(${out_var} ${_all} PARENT_SCOPE)
endfunction()

# ----------------------------------------------------------------------------
# mz_add_emulator(<target> <mzarch> <tvsys>)
#
# Vytvoří executable s per-arch sources, definemi a linknutými knihovnami.
# tvsys: PAL nebo NTSC. Definuje MZTVSYS=MZTVSYS_<tvsys> (= 50 nebo 60 dle
# nominal frame rate). MZ-700 má obě varianty (mz700emu-pal, mz700emu-ntsc),
# MZ-800 = vždy PAL, MZ-1500 = vždy NTSC.
#
# Příklad:
#   mz_add_emulator(mz800emu       800  PAL)
#   mz_add_emulator(mz1500emu     1500  NTSC)
#   mz_add_emulator(mz700emu-pal   700  PAL)
#   mz_add_emulator(mz700emu-ntsc  700  NTSC)
# ----------------------------------------------------------------------------
function(mz_add_emulator target mzarch tvsys)
    set(arch_name "mz${mzarch}")

    # ---- Společné zdroje (DIRS_MAIN + DIRS_EMULATOR + flat) ----------------

    # Recursive sběr DIRS_MAIN, ale BEZ per-arch ui-imgui podadresářů
    mz_glob_sources(_sources_main
        src/app
        src/baseui
        src/generic_driver
        src/iface
        src/iface-audio
        src/iface-video
        src/ui
        src/version_check
    )

    # ui-imgui rekurzivně, ale exclude všech ostatních arch podadresářů
    mz_glob_sources(_sources_uiimgui src/ui-imgui)
    list(FILTER _sources_uiimgui EXCLUDE REGEX "/src/ui-imgui/mz[0-9]+/")

    # DIRS_EMULATOR rekurzivně
    mz_glob_sources(_sources_emu
        src/emulator/hw-generic
        src/emulator/debugger
        src/emulator/snapshot
    )

    # Flat src soubory
    mz_glob_flat(_sources_flat_src      src)
    mz_glob_flat(_sources_flat_emu      src/emulator)
    mz_glob_flat(_sources_flat_mzarch   src/emulator/mzarch)

    # Per-arch zdroje - rekurzivně z mz<N>
    mz_glob_sources(_sources_arch
        src/emulator/mzarch/${arch_name}
        src/ui-imgui/${arch_name}
    )

    set(_all_sources
        ${_sources_main}
        ${_sources_uiimgui}
        ${_sources_emu}
        ${_sources_flat_src}
        ${_sources_flat_emu}
        ${_sources_flat_mzarch}
        ${_sources_arch}
        ${MZ_BUILD_REVISION_C}
    )

    # ---- Vytvoření executable ----------------------------------------------

    add_executable(${target} ${_all_sources})

    # build_revision.c se generuje custom targetem - musí proběhnout dřív
    add_dependencies(${target} mz_build_revision)

    # Windows .rc → .o se přidá přes helper. RC je shared per mzarch
    # (mz700emu-pal a mz700emu-ntsc sdílí mz700emu.rc) - per-tvsys ikony
    # by nedávaly smysl.
    if(WIN32)
        mz_add_windows_rc(${target} ${CMAKE_SOURCE_DIR}/src/windows_rc/${arch_name}emu.rc)
    endif()

    # ---- Per-arch a per-tvsys defines --------------------------------------

    target_compile_definitions(${target} PRIVATE
        MZARCH=${mzarch}
        MZARCH_NAME="${arch_name}"
        MZTVSYS_PAL=50
        MZTVSYS_NTSC=60
        MZTVSYS=MZTVSYS_${tvsys}
        MZTVSYS_NAME="${tvsys}"
    )

    # ---- Include paths -----------------------------------------------------

    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/emulator
        ${CMAKE_SOURCE_DIR}/src/emulator/hw-generic
    )

    # Headers adresáře (src/**/headers) - z Makefile HEADER_DIRS
    # POZN: file(GLOB_RECURSE ... LIST_DIRECTORIES true ...) ignoruje pattern
    # a vrací VSECHNY podadresare pod src/. To by zaradilo i src/libs/igfd/dirent
    # (Windows shim s #include <windows.h>) do include path - na Linuxu to padne
    # na chybejici windows.h, na MSYS2/MinGW to projde, ale shim neni potreba
    # (mingw64/glibc maji systemovy <dirent.h>). Proto rucne filtrujeme.
    file(GLOB_RECURSE _header_dirs LIST_DIRECTORIES true
        "${CMAKE_SOURCE_DIR}/src/*/headers"
    )
    list(FILTER _header_dirs INCLUDE REGEX "/headers$")
    foreach(_hd IN LISTS _header_dirs)
        if(IS_DIRECTORY ${_hd})
            target_include_directories(${target} PRIVATE ${_hd})
        endif()
    endforeach()

    # ---- Linkování ---------------------------------------------------------

    target_link_libraries(${target} PRIVATE
        mz::libs
        mz::sdl3
        mz::glib
        mz::json_glib
        mz::minizip
        mz::libcurl
        mz::opengl
        mz::platform
    )

    # POZN: dříve jsme tu měli -static-libstdc++ -static-libgcc jako workaround
    # pro `std::codecvt_utf8_utf16` linker chyby. Zjistilo se ale, že skutečnou
    # příčinou byl ABI mismatch UCRT64 headers (z pkg-config) + MINGW64 libstdc++.
    # Vynucení PKG_CONFIG_PATH na MINGW64 v Makefile wrapperu problém vyřešilo.
    # Static C++ runtime tedy nepotřebujeme - viz Makefile wrapper PKG_CONFIG_PATH.

    # ---- Output directory --------------------------------------------------
    # Binárka jde do build-${target}/ (per target name, ne arch_name) - aby
    # se mz700emu-pal a mz700emu-ntsc nepřepsaly navzájem ve sdíleném dir.
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build-${target}
    )

    # ---- FORCE_CONSOLE: na Windows GUI subsystem nahradíme za CONSOLE -----
    # Pokud MZ_FORCE_CONSOLE=ON, mz::sdl3 už má upravené libs bez -mwindows,
    # ale linker by mohl ještě default mít WIN32_EXECUTABLE. Necháme default
    # (CMake na Windows defaultně linkuje pro console subsystem; SDL3 svým
    # -mwindows přepíná na GUI - když ho odebereme, vrátíme se ke konzoli).
endfunction()
