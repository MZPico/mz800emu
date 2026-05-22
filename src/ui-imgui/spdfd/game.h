#pragma once

#include "ui-imgui/spdfd/renderer.h"
#include "ui-imgui/spdfd/input.h"
#include "ui-imgui/spdfd/states/game_state.h"
#include <memory>

// Hlavní třída hry — vlastní smyčku, renderer a stavový automat.
class Game {
public:
    bool init();
    void run();
    void shutdown();

private:
    Renderer renderer_;
    Input input_;
    std::unique_ptr<GameState> state_;
    bool running_ = false;

    // FPS měření
    float fps_ = 0.0f;
    int frame_count_ = 0;
    double fps_timer_ = 0.0;
};
