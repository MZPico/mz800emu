#include "main.h"
#include "libs/sdlapp/sdlapp.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "ui-imgui/auto_layout.h"
#include "libs/imgui/imgui.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <glib.h>
#include <math.h>
#include <string.h>

// Lokalizace
#include "i18n.h"

extern "C"
{
    void imgui_fps_measurement(bool *p_open);
};

#define FRAME_HISTORY 500
static float updateInterval = 2.0f; /**< Interval aktualizace zobrazovaných hodnot v sekundách. */

/**
 * @brief Struktura pro uchování dat o FPS.
 */
typedef struct
{
    float frameTimes[FRAME_HISTORY]; /**< Pole pro uchování časů jednotlivých snímků. */
    int frameCount;                  /**< Počet zaznamenaných snímků. */
    int currentIndex;                /**< Aktuální index v poli frameTimes. */
    float lastFrameTime;             /**< Čas posledního snímku. */
    float lastUpdateTime;            /**< Čas poslední aktualizace zobrazených hodnot. */
    float displayFPS;                /**< Zobrazená hodnota FPS. */
    float displayMin;
    float displayMax;
    float displayMedian;
    float displayDeviation;
} FPSCounter;

static FPSCounter fpsCounter = {{0}, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

/**
 * @brief Porovnávací funkce pro qsort.
 */
int compare_floats(const void *a, const void *b)
{
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

/**
 * @brief Spočítá statistické hodnoty FPS.
 */
void calculate_fps_stats(float *fps, float *min, float *max, float *median, float *deviation)
{
    if (fpsCounter.frameCount == 0)
    {
        *fps = 0.0f;
        *min = 0.0f;
        *max = 0.0f;
        *median = 0.0f;
        *deviation = 0.0f;
        return;
    }

    float times[FRAME_HISTORY];
    memcpy(times, fpsCounter.frameTimes, sizeof(float) * fpsCounter.frameCount);

    float totalTime = 0.0f;
    for (int i = 0; i < fpsCounter.frameCount; i++)
    {
        totalTime += times[i];
    }

    *fps = fpsCounter.frameCount / totalTime;

    qsort(times, fpsCounter.frameCount, sizeof(float), compare_floats);

    *min = 1.0f / times[fpsCounter.frameCount - 1];
    *max = 1.0f / times[0];

    if (fpsCounter.frameCount % 2 == 0)
        *median = 1.0f / ((times[fpsCounter.frameCount / 2 - 1] + times[fpsCounter.frameCount / 2]) / 2.0f);
    else
        *median = 1.0f / times[fpsCounter.frameCount / 2];

    float mean = totalTime / fpsCounter.frameCount;
    float variance = 0.0f;
    for (int i = 0; i < fpsCounter.frameCount; i++)
    {
        variance += (times[i] - mean) * (times[i] - mean);
    }
    *deviation = sqrt(variance / fpsCounter.frameCount);
}

/**
 * @brief Aktualizuje FPS měření.
 */
void update_fps_measurement()
{
    float currentTime = SDL_GetTicks() / 1000.0f;
    float deltaTime = currentTime - fpsCounter.lastFrameTime;
    fpsCounter.lastFrameTime = currentTime;

    if (deltaTime > 0)
    {
        fpsCounter.frameTimes[fpsCounter.currentIndex] = deltaTime;
        fpsCounter.currentIndex = (fpsCounter.currentIndex + 1) % FRAME_HISTORY;
        if (fpsCounter.frameCount < FRAME_HISTORY)
        {
            fpsCounter.frameCount++;
        }
    }

    if (currentTime - fpsCounter.lastUpdateTime >= updateInterval)
    {
        fpsCounter.lastUpdateTime = currentTime;
        calculate_fps_stats(&fpsCounter.displayFPS, &fpsCounter.displayMin, &fpsCounter.displayMax, &fpsCounter.displayMedian, &fpsCounter.displayDeviation);
    }
}

/**
 * @brief Vykreslí okno s měřením FPS pomocí ImGui.
 */
void imgui_fps_measurement(bool *p_open)
{
    update_fps_measurement();

    if (!*p_open)
        return;

    ImGui::SetNextWindowSize(ImVec2(280, 320), ImGuiCond_FirstUseEver);
    /* Auto-layout při fresh open - cache _L() do lokální proměnné. */
    const char *fps_title = _L("FPS Measurement");
    auto_layout_first_use_portrait(fps_title, 400.0f, 400.0f);
    if (ImGui::Begin(fps_title, p_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {

        if (ImGui::BeginTable("FPS Table", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 180.0f); // Fixní šířka pro hodnoty

            auto printRow = [](const char *label, float value, const char *format = "%.2f")
            {
                char buffer[32]; // Pro formátovanou hodnotu
                snprintf(buffer, sizeof(buffer), format, value);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(label);
                ImGui::TableNextColumn();

                // Vypočítání šířky textu
                float textWidth = ImGui::CalcTextSize(buffer).x;
                float columnWidth = ImGui::GetColumnWidth();
                float cursorX = ImGui::GetCursorPosX() + columnWidth - textWidth - ImGui::GetStyle().ItemSpacing.x;

                ImGui::SetCursorPosX(cursorX);
                ImGui::TextUnformatted(buffer);
            };

            printRow(_("FPS:"), fpsCounter.displayFPS);
            printRow(_("Min FPS:"), fpsCounter.displayMin);
            printRow(_("Max FPS:"), fpsCounter.displayMax);
            printRow(_("Median FPS:"), fpsCounter.displayMedian);
            printRow(_("Deviation:"), fpsCounter.displayDeviation, "%.5f");

            ImGui::EndTable();

            ImGui::Separator();

            ImGui::Text("\n");
            ImGui::Text(_("Measuring Update Interval (s):"));
            ImGui::SliderFloat("###Update Interval (s) input", &updateInterval, 0.1f, 5.0f, "%.1f");
            ImGui::InputFloat("###Update Interval (s) slider", &updateInterval, 0.1f, 0.5f, "%.1f");
        }

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            *p_open = false;
        };

        ImGui::End();
    }
}
