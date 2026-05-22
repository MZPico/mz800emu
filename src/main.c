#include "main.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <glib.h>
#include "i18n.h"
#ifdef _WIN32
#include <windows.h>
#endif

#include "libs/sdlapp/sdlapp.h"
#include "libs/sdlapp/sdlapp_options.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "ui-imgui/debugger/dbgapi_dispatcher.h"
#include "ui-imgui/spdfd/spdfd_main.h"
#include "iface/iface.h"
#include "emulator/cfgmain.h"
#include "emulator/debugger/dbgapi_emu.h"
#include "emulator/i18n_lang.h"
#include "emulator.h"

SdlApp *g_sdlapp = NULL;


/**
 * @brief Whitelist všech známých CLI options.
 *
 * Používá se pro:
 *  - validaci v main() (neznámé options = error a exit)
 *  - generování --help výstupu
 *
 * Vyhodnocování hodnot dělají jednotlivé moduly (mzarch_main, debugger,
 * cfgmain) pomocí @c sdlapp_option_value / @c sdlapp_option_present.
 * Tabulka tedy slouží jen jako registr "co je validní".
 */
/* Sdílené allowed-values seznamy pro ENUM options. NULL-terminated. */
static const char *const MODE_VALUES[] = { "off", "window", "always", NULL };

/* FDC voličí pro --fdc-bus-xlate. */
static const char *const FDC_BUS_XLATE_VALUES[] = { "invert", "passthrough", NULL };

/* Sloupce: name, kind, value_type, allowed_values, value_placeholder, description.
 * Pro FLAG: value_type=NONE, allowed_values=NULL, value_placeholder=NULL.
 * Pro VALUE: value_type podle očekávaného obsahu (STRING/ENUM/UINT/BOOL_ON_OFF).
 * ENUM vyžaduje neprázdný allowed_values seznam (jinak se validace přeskočí). */
static const st_SDLAPP_OPTION_DEF g_known_options[] = {
    { "--help",             SDLAPP_OPTION_FLAG,  SDLAPP_OPTVAL_NONE,        NULL,        NULL,
      "Print this option list and exit." },
#ifdef _WIN32
    { "--console",          SDLAPP_OPTION_FLAG,  SDLAPP_OPTVAL_NONE,        NULL,        NULL,
      "Windows: allocate a console window for stdout/stderr." },
#endif
    { "--run-mzf",          SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<filepath>",
      "Automatically load and run the given MZF file after the emulator boots." },
    { "--cdl-mode",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_ENUM,        MODE_VALUES, "<off|window|always>",
      "Set the CDL (Memory Heatmap) recording mode." },
    { "--cdl-dir",          SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<dirpath>",
      "Set the target directory for CDL export. Created at export time if missing." },
    { "--cdl-name",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<basename>",
      "Set the CDL export basename (without .json). Region files are saved as "
      "<basename>_<region>.cdl, meta as <basename>.json." },
    { "--cdl-save-on-exit", SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_BOOL_ON_OFF, NULL,        "<on|off>",
      "Enable/disable automatic CDL export when the emulator exits." },
    /* trace-suite cputrack options. */
    { "--cputrack-mode",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_ENUM,        MODE_VALUES, "<off|window|always>",
      "Set the CPU tracking log recording mode (trace-suite cputrack)." },
    { "--cputrack-dir",          SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<dirpath>",
      "Set the target directory for cputrack recording. Created at start time if missing." },
    { "--cputrack-name",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<basename>",
      "Set the cputrack basename (without extension). Files: <basename>.json, "
      "<basename>.NNN.bin, <basename>_initial_*.bin." },
    { "--cputrack-chunk-mb",     SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_UINT,        NULL,        "<N>",
      "Per-chunk RAM buffer size in MB before sync flush to disk (default 64)." },
    { "--cputrack-max-total-mb", SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_UINT,        NULL,        "<N>",
      "Hard limit on total recording size in MB (0 = unlimited, default 0)." },
    { "--cputrack-save-on-exit", SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_BOOL_ON_OFF, NULL,        "<on|off>",
      "Flush cputrack buffer when the emulator exits (default on)." },
    /* trace-suite iorqlog options. */
    { "--iorqlog-mode",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_ENUM,        MODE_VALUES, "<off|window|always>",
      "Set the IORQ log recording mode (trace-suite iorqlog)." },
    { "--iorqlog-dir",          SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<dirpath>",
      "Set the target directory for iorqlog recording." },
    { "--iorqlog-name",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<basename>",
      "Set the iorqlog basename." },
    { "--iorqlog-chunk-mb",     SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_UINT,        NULL,        "<N>",
      "Per-chunk RAM buffer size in MB (default 64)." },
    { "--iorqlog-max-total-mb", SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_UINT,        NULL,        "<N>",
      "Hard limit on total iorqlog size in MB (0 = unlimited)." },
    { "--iorqlog-save-on-exit", SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_BOOL_ON_OFF, NULL,        "<on|off>",
      "Flush iorqlog buffer at exit (default on)." },
    /* trace-suite intlog options. */
    { "--intlog-mode",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_ENUM,        MODE_VALUES, "<off|window|always>",
      "Set the interrupt log recording mode (trace-suite intlog)." },
    { "--intlog-dir",          SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<dirpath>",
      "Set the target directory for intlog recording." },
    { "--intlog-name",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<basename>",
      "Set the intlog basename." },
    { "--intlog-chunk-mb",     SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_UINT,        NULL,        "<N>",
      "Per-chunk RAM buffer size in MB (default 64)." },
    { "--intlog-max-total-mb", SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_UINT,        NULL,        "<N>",
      "Hard limit on total intlog size in MB (0 = unlimited)." },
    { "--intlog-save-on-exit", SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_BOOL_ON_OFF, NULL,        "<on|off>",
      "Flush intlog buffer at exit (default on)." },
    /* trace-suite hwlog options. */
    { "--hwlog-mode",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_ENUM,        MODE_VALUES, "<off|window|always>",
      "Set the hardware log recording mode (trace-suite hwlog)." },
    { "--hwlog-dir",          SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<dirpath>",
      "Set the target directory for hwlog recording." },
    { "--hwlog-name",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<basename>",
      "Set the hwlog basename." },
    { "--hwlog-chunk-mb",     SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_UINT,        NULL,        "<N>",
      "Per-chunk RAM buffer size in MB (default 64)." },
    { "--hwlog-max-total-mb", SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_UINT,        NULL,        "<N>",
      "Hard limit on total hwlog size in MB (0 = unlimited)." },
    { "--hwlog-save-on-exit", SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_BOOL_ON_OFF, NULL,        "<on|off>",
      "Flush hwlog buffer at exit (default on)." },
    { "--hwlog-hs-decimation", SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_UINT,        NULL,        "<N>",
      "Decimate hwlog GDG_VIDEO HS/HBLN edges: emit every N-th edge "
      "(0 = OFF, default; ~31000 events/sec at full rate)." },
    /* trace-suite shorthand options - apply to all 4 subsystems (cputrack,
     * iorqlog, intlog, hwlog). Per-subsystem options take precedence; these
     * shorthand values fill in only where no per-subsystem option was given. */
    { "--all-traces-mode",    SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_ENUM,        MODE_VALUES, "<off|window|always>",
      "Set the recording mode for ALL trace-suite subsystems "
      "(cputrack, iorqlog, intlog, hwlog). Per-subsystem --<sys>-mode "
      "takes precedence." },
    { "--eventlog-replay",   SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<filepath>",
      "Replay an Event Viewer ring dump (.evlog) at startup: import "
      "the file into the in-memory ring so the Events window opens with "
      "pre-loaded post-mortem events." },
    { "--all-traces-dir",     SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<dirpath>",
      "Set the target directory for ALL trace-suite subsystems. "
      "Per-subsystem --<sys>-dir takes precedence." },
    /* Callstack subsystem (debugger shadow stack). */
    { "--callstack",          SDLAPP_OPTION_FLAG,  SDLAPP_OPTVAL_NONE,        NULL,        NULL,
      "Activate the Callstack subsystem on startup (shadow stack of CALL/RST/IRQ/NMI). "
      "Overrides the [CALLSTACK] active value from the INI." },
    /* Profiler subsystem (per-function CPU profiler, built on Callstack listener). */
    { "--profiler",           SDLAPP_OPTION_FLAG,  SDLAPP_OPTVAL_NONE,        NULL,        NULL,
      "Activate the CPU Profiler subsystem on startup (per-function profilation on top of Callstack). "
      "Implies --callstack (Profiler will force Callstack ON if needed). "
      "Overrides the [PROFILER] active value from the INI." },
    { "--no-save-ini",      SDLAPP_OPTION_FLAG,  SDLAPP_OPTVAL_NONE,        NULL,        NULL,
      "Do not write the .ini file at exit (CLI overrides become session-only)." },
    { "--no-first-run-windows", SDLAPP_OPTION_FLAG, SDLAPP_OPTVAL_NONE,     NULL,        NULL,
      "Suppress About + Version Check Setup windows shown automatically on first run "
      "(when no .ini file exists). Useful for headless / scripted launches." },
    { "--home-dir",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<dirpath>",
      "Override the home directory (where the binary's read-only assets live: "
      "ui_resources/, locale/, certs/). Default: auto-detected from the binary "
      "location." },
    { "--cfg-dir",          SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<dirpath>",
      "Override the config directory (where mz<arch>emu.ini, "
      "mz<arch>emu-imgui.ini, breakpoints.ini and persistent ramdisk images "
      "are stored). Default: same as home-dir." },
    { "--work-dir",         SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<dirpath>",
      "Override the work directory (default open location for file dialogs). "
      "Default: current working directory at startup." },
    { "--config",           SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<filepath>",
      "Path to the main config file (mz<arch>emu.ini). Default: cfg-dir/mz<arch>emu.ini." },
    { "--imgui-ini",        SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_STRING,      NULL,        "<filepath>",
      "Path to the ImGui state file (mz<arch>emu-imgui.ini). Default: "
      "cfg-dir/mz<arch>emu-imgui.ini." },
    { "--spdfd",            SDLAPP_OPTION_FLAG,  SDLAPP_OPTVAL_NONE,        NULL,        NULL,
      "Reserved internal launch mode." },
    /* FDC volby (viz src/emulator/hw-generic/fdc/). */
    { "--fdc-bus-xlate",    SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_ENUM,        FDC_BUS_XLATE_VALUES, "<invert|passthrough>",
      "FDC BUS translation polarity. 'invert' = Sharp default "
      "(XOR 0xFF between CPU bus and WD279x chip), 'passthrough' = experimental "
      "no-inversion mode for future 'true bus' ROMs." },
    { "--fdc-hd-patch",     SDLAPP_OPTION_VALUE, SDLAPP_OPTVAL_BOOL_ON_OFF, NULL,        "<on|off>",
      "Enable/disable the HD Patch circuit (port 0xDF EINT logic) on the FDC." },
    { NULL, SDLAPP_OPTION_FLAG, SDLAPP_OPTVAL_NONE, NULL, NULL, NULL }   /* sentinel */
};


int main(int argc, char *argv[])
{
    /* Inicializace options moduly před čímkoliv jiným, aby si moduly mohly
     * své options vytáhnout v dalších fázích startu. */
    sdlapp_options_init(argc, argv);

    /* --help: vypsat usage a skončit. */
    if (sdlapp_option_present("--help"))
    {
        sdlapp_options_print_help(argv[0], g_known_options);
        return EXIT_SUCCESS;
    };

    /* Validace - neznámé options nebo VALUE option bez hodnoty = chyba. */
    if (!sdlapp_options_validate(g_known_options))
    {
        return EXIT_FAILURE;
    };

    /* --spdfd: alternativní launch mód. Větvíme dřív, než se inicializuje
     * jakákoliv emulátorová infrastruktura - modul si vytváří vlastní SDL
     * kontext, vlastní okno a vlastní ImGui kontext. Pokud bychom prošli
     * sdlapp_new() + iface_init(), dva SDL_Init / dva ImGui::CreateContext
     * by se nemusely snášet.
     */
    if (sdlapp_option_present("--spdfd"))
    {
        int spdfd_rc = spdfd_main(argc, argv);
        if (spdfd_rc != SPDFD_FALLBACK_TO_EMU)
        {
            return spdfd_rc;
        };
    };

#ifdef _WIN32
    if (sdlapp_option_present("--console"))
    {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
    };
#endif

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // printf ("Hello, World!\n"); // prints Hello, World! :-)
    g_print("\n");

    // g_print("Working dir: %s\n", g_get_current_dir());
    // g_print("Exe path: %s\n", g_get_prgname());

    g_sdlapp = sdlapp_new();
    if (!g_sdlapp)
    {
        SDLAPP_ERROR("Failed to create SdlApp");
        return EXIT_FAILURE;
    };

    /* i18n_init musí být po sdlapp_new — používá g_sdlapp->paths->home_dir
     * pro lokalizaci adresáře s .mo soubory. */
    i18n_init();

    // cfgmain musi byt prvni inicializovany modul !!!
    cfgmain_init();

    /* Aplikace jazyka z konfigurace (po cfgmain_init, kde se načte INI) */
    i18n_lang_apply();

    if (!iface_init())
    {
        SDLAPP_ERROR("Failed to initialize interface");
        return EXIT_FAILURE;
    };

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
    /* Inicializace dbgapi CMDRQ fronty - musí být před dispatcherem
     * a před spuštěním EMU vlákna. EMU vlákno frontu drainuje v
     * mzarch hot loop / paused loop, UI vlákno do ní submituje. */
    dbgapi_init(&g_dbgapi_cmdrq_queue);

    /* Registrace dispatcheru pro doručení dbgapi MSG z EMU vlákna do UI.
     * Musí být zaregistrovaný před spuštěním EMU vlákna, jinak by raně
     * odeslané MSGy přišly o doručení. */
    dbgapi_dispatcher_init();
#endif

    // nastartujeme emulator
    GThread *emu_thread = g_thread_new("mz-800-emulator", emulator_thread, NULL);
    if (!emu_thread)
    {
        SDLAPP_ERROR("Failed to start emulator thread");
        return EXIT_FAILURE;
    };

    sdlapp_run(g_sdlapp);

    // pockej na ukonceni emulatoru (g_sdlapp i SDL audio jsou stale aktivni,
    // takze emulatorove vlakno se muze korektne ukoncit pres sdlapp_is_running() check)
    gpointer emuret = g_thread_join(emu_thread);
    int emuretval = (emuret) ? *(int *)emuret : EXIT_FAILURE;

    // az po ukonceni emulatoroveho vlakna muzeme uklidit
#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
    /* Odregistrace dispatcheru - po této funkci se MSGy zahodí. */
    dbgapi_dispatcher_shutdown();

    /* Destrukce dbgapi fronty - po ukončení EMU vlákna a dispatcheru
     * (= žádný producer ani konzument už nebude přistupovat). */
    dbgapi_destroy(&g_dbgapi_cmdrq_queue);
#endif

    iface_exit();
    sdlapp_destroy(g_sdlapp);
    SDL_Quit();

    g_print("Emulator exit value: %d\n", emuretval);
    return emuretval;
}
