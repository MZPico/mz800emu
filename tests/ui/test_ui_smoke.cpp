/*
 * test_ui_smoke.cpp — smoke testy SDL3 + OpenGL + ImGui infrastruktury
 *
 * Testuje: SDL3 inicializaci, skryté okno, GL kontext, ImGui lifecycle,
 *          renderování framů bez pádu.
 *
 * Nelinkuje se s emulátorovou UI vrstvou — ověřuje jen základní
 * infrastrukturu pro vykreslování.
 *
 * Licence: GPLv3
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "libs/imgui/imgui.h"
#include "libs/imgui/backends/imgui_impl_sdl3.h"
#include "libs/imgui/backends/imgui_impl_opengl3.h"

extern "C" {
#include "unity.h"
}

#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#include <stdlib.h>  /* _set_error_mode, _OUT_TO_STDERR (s dllimport attribute) */
#include <process.h> /* _exit */
static void sigabrt_handler(int) { _exit(1); }
#endif

/* ================================================================
 * Globální stav pro testy
 * ================================================================ */

static SDL_Window *g_test_window = NULL;
static SDL_GLContext g_test_gl_context = NULL;
static ImGuiContext *g_test_imgui_ctx = NULL;

/* ================================================================
 * Helpers pro SDL3 + GL + ImGui init/teardown
 * ================================================================ */

/* Inicializace SDL3 + GL atributů */
static bool test_sdl3_setup(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    return true;
}

/* Vytvoření skrytého okna s GL kontextem */
static bool test_create_hidden_gl_window(void)
{
    g_test_window = SDL_CreateWindow(
        "mz800emu-test", 640, 480,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL
    );
    if (!g_test_window) return false;

    g_test_gl_context = SDL_GL_CreateContext(g_test_window);
    if (!g_test_gl_context) {
        SDL_DestroyWindow(g_test_window);
        g_test_window = NULL;
        return false;
    }

    SDL_GL_MakeCurrent(g_test_window, g_test_gl_context);
    return true;
}

/* Inicializace ImGui s SDL3 + OpenGL backendy */
static bool test_imgui_setup(void)
{
    IMGUI_CHECKVERSION();
    g_test_imgui_ctx = ImGui::CreateContext();
    if (!g_test_imgui_ctx) return false;

    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL; /* netvoříme .ini soubor v testech */
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForOpenGL(g_test_window, g_test_gl_context)) {
        ImGui::DestroyContext(g_test_imgui_ctx);
        g_test_imgui_ctx = NULL;
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 130")) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(g_test_imgui_ctx);
        g_test_imgui_ctx = NULL;
        return false;
    }

    return true;
}

/* Provedení jednoho ImGui framu */
static void test_pump_frame(void)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

/* Dokončení framu (Render + GL draw) */
static void test_end_frame(void)
{
    ImGui::Render();

    ImGuiIO &io = ImGui::GetIO();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g_test_window);
}

/* Kompletní teardown */
static void test_full_teardown(void)
{
    if (g_test_imgui_ctx && ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(g_test_imgui_ctx);
        g_test_imgui_ctx = NULL;
    }
    if (g_test_gl_context) {
        SDL_GL_DestroyContext(g_test_gl_context);
        g_test_gl_context = NULL;
    }
    if (g_test_window) {
        SDL_DestroyWindow(g_test_window);
        g_test_window = NULL;
    }
    SDL_Quit();
}

/* ================================================================
 * Unity setUp/tearDown
 * ================================================================ */

void setUp(void) { }
void tearDown(void) { }

/* ================================================================
 * SMOKE TESTY — SDL3 infrastruktura
 * ================================================================ */

/* SDL3 — inicializace video subsystému */
void test_sdl3_video_init(void)
{
    TEST_ASSERT_TRUE_MESSAGE(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    SDL_Quit();
}

/* SDL3 — vytvoření a zničení skrytého okna */
void test_sdl3_hidden_window(void)
{
    TEST_ASSERT_TRUE_MESSAGE(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    SDL_Window *win = SDL_CreateWindow("test", 320, 240, SDL_WINDOW_HIDDEN);
    TEST_ASSERT_NOT_NULL_MESSAGE(win, SDL_GetError());

    SDL_DestroyWindow(win);
    SDL_Quit();
}

/* SDL3 — vytvoření skrytého okna s OpenGL */
void test_sdl3_opengl_context(void)
{
    TEST_ASSERT_TRUE(test_sdl3_setup());

    SDL_Window *win = SDL_CreateWindow("test-gl", 320, 240,
                                        SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    TEST_ASSERT_NOT_NULL_MESSAGE(win, SDL_GetError());

    SDL_GLContext gl = SDL_GL_CreateContext(win);
    TEST_ASSERT_NOT_NULL_MESSAGE(gl, SDL_GetError());

    /* ověříme, že GL kontext je aktivní */
    SDL_GL_MakeCurrent(win, gl);
    const GLubyte *version = glGetString(GL_VERSION);
    TEST_ASSERT_NOT_NULL(version);

    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

/* ================================================================
 * SMOKE TESTY — ImGui infrastruktura
 * ================================================================ */

/* ImGui — vytvoření a zničení kontextu */
void test_imgui_create_context(void)
{
    ImGuiContext *ctx = ImGui::CreateContext();
    TEST_ASSERT_NOT_NULL(ctx);
    ImGui::DestroyContext(ctx);
}

/* ImGui — inicializace s SDL3 + OpenGL backendy */
void test_imgui_init_with_backends(void)
{
    TEST_ASSERT_TRUE(test_sdl3_setup());
    TEST_ASSERT_TRUE(test_create_hidden_gl_window());
    TEST_ASSERT_TRUE(test_imgui_setup());

    test_full_teardown();
}

/* ================================================================
 * UNIT TESTY — ImGui rendering
 * ================================================================ */

/* ImGui — jeden prázdný frame nepadne */
void test_imgui_single_empty_frame(void)
{
    TEST_ASSERT_TRUE(test_sdl3_setup());
    TEST_ASSERT_TRUE(test_create_hidden_gl_window());
    TEST_ASSERT_TRUE(test_imgui_setup());

    test_pump_frame();
    /* prázdný frame — žádné widgety */
    test_end_frame();

    test_full_teardown();
}

/* ImGui — frame s jednoduchými widgety */
void test_imgui_frame_with_widgets(void)
{
    TEST_ASSERT_TRUE(test_sdl3_setup());
    TEST_ASSERT_TRUE(test_create_hidden_gl_window());
    TEST_ASSERT_TRUE(test_imgui_setup());

    test_pump_frame();

    /* základní widgety */
    ImGui::Begin("Test Window");
    ImGui::Text("Hello from test");
    ImGui::Button("Click me");
    ImGui::End();

    test_end_frame();
    test_full_teardown();
}

/* ImGui — 100 framů bez pádu */
void test_imgui_100_frames_no_crash(void)
{
    TEST_ASSERT_TRUE(test_sdl3_setup());
    TEST_ASSERT_TRUE(test_create_hidden_gl_window());
    TEST_ASSERT_TRUE(test_imgui_setup());

    for (int i = 0; i < 100; i++) {
        test_pump_frame();

        /* různé widgety v každém framu */
        ImGui::Begin("Stability Test");
        ImGui::Text("Frame %d", i);

        if (i % 10 == 0) {
            ImGui::Button("Button A");
        }
        if (i % 7 == 0) {
            bool checkbox = (i % 2 == 0);
            ImGui::Checkbox("Check", &checkbox);
        }
        if (i % 5 == 0) {
            float slider = (float)i / 100.0f;
            ImGui::SliderFloat("Slider", &slider, 0.0f, 1.0f);
        }

        ImGui::End();

        /* občas otevřeme/zavřeme druhé okno */
        if (i >= 20 && i < 40) {
            ImGui::Begin("Secondary Window");
            ImGui::Text("Secondary content");
            ImGui::End();
        }

        test_end_frame();
    }

    test_full_teardown();
}

/* ImGui — BeginMainMenuBar + podmenu */
void test_imgui_menu_bar(void)
{
    TEST_ASSERT_TRUE(test_sdl3_setup());
    TEST_ASSERT_TRUE(test_create_hidden_gl_window());
    TEST_ASSERT_TRUE(test_imgui_setup());

    for (int i = 0; i < 5; i++) {
        test_pump_frame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                ImGui::MenuItem("Open");
                ImGui::MenuItem("Save");
                ImGui::Separator();
                ImGui::MenuItem("Exit");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                ImGui::MenuItem("Copy");
                ImGui::MenuItem("Paste");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        test_end_frame();
    }

    test_full_teardown();
}

/* ImGui — BeginTable + sloupce */
void test_imgui_table(void)
{
    TEST_ASSERT_TRUE(test_sdl3_setup());
    TEST_ASSERT_TRUE(test_create_hidden_gl_window());
    TEST_ASSERT_TRUE(test_imgui_setup());

    test_pump_frame();

    ImGui::Begin("Table Test");
    if (ImGui::BeginTable("TestTable", 3, ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("A");
        ImGui::TableSetupColumn("B");
        ImGui::TableSetupColumn("C");
        ImGui::TableHeadersRow();

        for (int row = 0; row < 5; row++) {
            ImGui::TableNextRow();
            for (int col = 0; col < 3; col++) {
                ImGui::TableSetColumnIndex(col);
                ImGui::Text("R%d C%d", row, col);
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();

    test_end_frame();
    test_full_teardown();
}

/* ImGui — popup okno */
void test_imgui_popup(void)
{
    TEST_ASSERT_TRUE(test_sdl3_setup());
    TEST_ASSERT_TRUE(test_create_hidden_gl_window());
    TEST_ASSERT_TRUE(test_imgui_setup());

    for (int i = 0; i < 5; i++) {
        test_pump_frame();

        ImGui::Begin("Popup Test");

        /* první frame otevře popup, další framy ho vykreslí */
        if (i == 0) {
            ImGui::OpenPopup("TestPopup");
        }

        if (ImGui::BeginPopup("TestPopup")) {
            ImGui::Text("Popup content");
            ImGui::Button("OK");
            ImGui::EndPopup();
        }

        ImGui::End();
        test_end_frame();
    }

    test_full_teardown();
}

/* ImGui — SDL event processing */
void test_imgui_sdl_event_processing(void)
{
    TEST_ASSERT_TRUE(test_sdl3_setup());
    TEST_ASSERT_TRUE(test_create_hidden_gl_window());
    TEST_ASSERT_TRUE(test_imgui_setup());

    /* simulace několika framů s event pollingem */
    for (int i = 0; i < 10; i++) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        test_pump_frame();
        ImGui::Begin("Event Test");
        ImGui::Text("Processing events...");
        ImGui::End();
        test_end_frame();
    }

    test_full_teardown();
}

/* ================================================================
 * MAIN
 * ================================================================ */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    signal(SIGABRT, sigabrt_handler);
#endif

    UNITY_BEGIN();

    /* smoke — SDL3 infrastruktura */
    RUN_TEST(test_sdl3_video_init);
    RUN_TEST(test_sdl3_hidden_window);
    RUN_TEST(test_sdl3_opengl_context);

    /* smoke — ImGui infrastruktura */
    RUN_TEST(test_imgui_create_context);
    RUN_TEST(test_imgui_init_with_backends);

    /* unit — ImGui rendering */
    RUN_TEST(test_imgui_single_empty_frame);
    RUN_TEST(test_imgui_frame_with_widgets);
    RUN_TEST(test_imgui_100_frames_no_crash);
    RUN_TEST(test_imgui_menu_bar);
    RUN_TEST(test_imgui_table);
    RUN_TEST(test_imgui_popup);
    RUN_TEST(test_imgui_sdl_event_processing);

    return UNITY_END();
}
