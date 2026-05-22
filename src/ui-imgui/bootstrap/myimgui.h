/* myimgui.h - definitions for My ImGui */
#ifndef MYIMGUI_H
#define MYIMGUI_H

#include <glib.h>
#include "libs/sdlapp/sdlapp.h"

typedef struct FRGBA
{
    float r;
    float g;
    float b;
    float a;
} FRGBA;

typedef void (*MyImGuiEventCb)(SdlAppWindow *win, SDL_Event *event, gpointer user_data);
typedef void (*MyImGuiRenderCb)(SdlAppWindow *win, gpointer user_data);

typedef struct MyImGui
{
    SdlApp *app;
    SdlAppWindow *win;
    gpointer ctxptr;

    gboolean use_bg_color;
    FRGBA bg;

    MyImGuiEventCb event_cb;
    MyImGuiRenderCb render_cb;

    // flagy pro zpracovani klavesovych udalosti pro pio8255
    bool MainWindowHasFocus;
    bool VkbdWindowHasFocus;
    bool haveKeyboardEvents;

    // Overlay
    bool showOverlay;
    bool justOpenedOverlay;

    // Dalsi okna
    bool showVersionCheckSetupWindow;
    bool showVersionCheckResultWindow;
    bool showAboutWindow;
    bool showVideoIfaceMeasurementWindow;
    bool showAudioWindow;
    bool showVirtualCmtWindow;
    bool showCmtFixMzfsizeWindow;
    bool showVirtualCmtTapeIndexWindow;
    bool showVirtualKeyboardWindow;
    bool showSnapshotSetupWindow;
    bool showLanguageWindow;
    bool showPezikSettingsWindow;
    bool showRomSettingsWindow;
    bool showAutotypeWindow;
    bool showMemextMapWindow;
    bool showDskCreateWindow;
    bool showFdcStorageSwitchWindow;
    bool showFdcStateWindow;
    bool showQdiskStorageSwitchWindow;   /**< QD: switch storage mode popup (Faze 3) */
    bool showQdiskStateWindow;           /**< QD: debugger state inspector okno (Faze 6) */
    bool showJoystickSetupWindow;
    bool showDebuggerWindow;
    bool showBreakpointsWindow;
    bool showMemoryMapWindow;       /**< Memory Map debug okno - banking + memext per 4 kB stránku */
    bool showMemoryHeatmapWindow;
    bool showVarsWindow;            /**< $vars panel pro smart BP user vars (D.6.2) */
    bool showWatchWindow;           /**< Watch panel - user-defined paměťové hlídky (V1 Phase A) */
    bool showIoWindow;              /**< I/O Ports viewer panel (D.7) */
    bool showSymbolsWindow;         /**< Symbol Browser panel (D.8.6) */
    bool showBookmarksWindow;       /**< Bookmarks panel (pojmenované adresové záložky) */
    bool showCpuWindow;             /**< CPU Registers - Variant B samostatné plovoucí okno */
    bool showStackWindow;           /**< Stack Monitor - samostatné plovoucí okno (hex dump kolem SP) */
    bool showStackRegionsWindow;    /**< V8: Stack Regions - samostatné okno s tabulkou regionů (1 řádek per region) */
    bool showStackHistoryWindow;    /**< V9: Stack History - samostatné okno s SP history sparkline (plot resize s velikostí okna) */
    bool showCallstackWindow;       /**< Callstack - samostatné plovoucí okno se shadow stackem (CALL/RET/IRQ/NMI frames) */
    bool showProfilerWindow;        /**< CPU Profiler - per-function CPU profilace (mutant profiler V1) */
    bool showEventViewerWindow;     /**< Events - real-time pohled na eventlog ring (Log + Strip tab) */
    /**
     * Sekundární Disassembly okna (#2-#5) - 4 nezávislé instance
     * DisassembledView dostupné z menu Debugger -> Další disassembly.
     * Každé okno má vlastní focus_addr, selected_row, scroll, atd.
     * Sdílí pouze paměť, symboly, breakpointy. Layout perzistuje přes
     * imgui.ini, focus_addr přes cfgmain (klíče debugger.disasm_extra*).
     * Index: 0 = #2, 1 = #3, 2 = #4, 3 = #5.
     */
    bool showDisasmExtraWindow[4];
} MyImGui;

extern MyImGui *g_gui;

#ifdef __cplusplus
extern "C"
{
#endif
    gboolean myimgui_init_cb(SdlAppWindow *win, gpointer user_data);
    void myimgui_render_cb(SdlAppWindow *win, gpointer user_data);
    void myimgui_event_cb(SdlAppWindow *win, SDL_Event *event, gpointer user_data);
    void myimgui_destroy_cb(SdlAppWindow *win, gpointer user_data);

    void myimgui_set_bg_clean(MyImGui *gui, bool enabled);
    gboolean myimgui_get_bg_clean(MyImGui *gui);

    void myimgui_set_bg_color(MyImGui *gui, float r, float g, float b, float a);
    void myimgui_get_bg_color(MyImGui *gui, float *r, float *g, float *b, float *a);

    MyImGuiEventCb myimgui_get_event_cb(MyImGui *gui);
    void myimgui_set_event_cb(MyImGui *gui, MyImGuiEventCb event_cb);

    MyImGuiRenderCb myimgui_get_render_cb(MyImGui *gui);
    void myimgui_set_render_cb(MyImGui *gui, MyImGuiRenderCb render_cb);

#ifdef __cplusplus
}
#endif

#endif /* MYIMGUI_H */
