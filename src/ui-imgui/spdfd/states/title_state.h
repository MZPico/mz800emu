#pragma once

#include "game_state.h"
#include "ui-imgui/spdfd/entities/sprites.h"
#include "ui-imgui/spdfd/constants.h"

// Titulní obrazovka s demo hrou na pozadí a sinusovým scrollerem.
// AI dělo střílí na automaticky se pohybující formaci — hráč sleduje
// "svůj tým" jak je decimován. Dole běží sinusový scroller s instrukcemi.
class TitleState : public GameState {
public:
    void enter() override;
    void update(double dt, const Input& input) override;
    void render(Renderer& renderer) override;
    std::unique_ptr<GameState> next_state() override;
    bool wants_quit() const override { return quit_; }

private:
    bool start_game_ = false;
    bool quit_ = false;
    float blink_timer_ = 0.0f; // blikání textu "PRESS ENTER"
    bool blink_visible_ = true;

    // Demo — auto-play formace + AI dělo
    static constexpr int DEMO_ROWS = 5;
    static constexpr int DEMO_COLS = 8;
    bool demo_alive_[DEMO_ROWS * DEMO_COLS] = {};
    float demo_formation_x_ = 0.0f;
    float demo_formation_dir_ = 1.0f;
    int demo_anim_frame_ = 0;
    float demo_anim_timer_ = 0.0f;

    // AI dělo v demu
    float demo_cannon_x_ = GAME_WIDTH / 2.0f;
    float demo_cannon_dir_ = 1.0f;
    float demo_bullet_x_ = 0.0f;
    float demo_bullet_y_ = -10.0f;
    float demo_shoot_timer_ = 0.0f;

    // Timer pro reset dema (když jsou všichni mrtví)
    float demo_reset_timer_ = 0.0f;

    // Sinusový scroller
    float scroller_offset_ = 0.0f;
    float timer_ = 0.0f;
};
