#include "debug_overlay.h"
#include "libs/imgui/imgui.h"
#include "ui-imgui/spdfd/entities/formation.h"
#include "ui-imgui/spdfd/entities/cannon.h"

static bool visible_ = false;

void DebugOverlay::toggle() {
    visible_ = !visible_;
}

bool DebugOverlay::visible() {
    return visible_;
}

void DebugOverlay::render(float fps, const Formation& formation, const Cannon& cannon,
                           int bullet_count) {
    if (!visible_) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Debug", &visible_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Separator();
        ImGui::Text("Formation: %.1f, %.1f", formation.x(), formation.y());
        ImGui::Text("Aliens: %d / %d", formation.alive_count(), formation.total_count());
        ImGui::Separator();
        ImGui::Text("Cannon: %.1f (alive: %s)", cannon.x(), cannon.alive() ? "yes" : "no");
        ImGui::Text("Bullets: %d", bullet_count);
    }
    ImGui::End();
}
