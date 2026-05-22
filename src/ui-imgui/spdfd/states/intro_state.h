#pragma once

#include "game_state.h"
#include "ui-imgui/spdfd/entities/sprites.h"
#include "ui-imgui/spdfd/constants.h"

// Intro animace — perspektivní flip.
// 1. Klasický Space Invaders layout (dělo dole, formace nahoře)
// 2. Kamera se posune nahoru, jako bys vletěl do formace
// 3. Text: "You are not the hero. You never were."
// 4. Titulek SPACE DEFENDERS
// 5. Přechod na TitleState
class IntroState : public GameState {
public:
    void enter() override;
    void update(double dt, const Input& input) override;
    void render(Renderer& renderer) override;
    std::unique_ptr<GameState> next_state() override;

private:
    enum class Phase {
        CLASSIC_VIEW,   // klasický pohled — dělo střílí na formaci
        CAMERA_PAN,     // kamera se posouvá nahoru do formace
        REVEAL_TEXT,    // "You are not the hero. You never were."
        TITLE,          // SPACE DEFENDERS + fade
        DONE,           // přechod na title screen
    };

    Phase phase_ = Phase::CLASSIC_VIEW;
    float timer_ = 0.0f;
    float phase_timer_ = 0.0f;

    // Kamera offset (posun celé scény v Y)
    float camera_y_ = 0.0f;

    // Animace formace
    int anim_frame_ = 0;
    float anim_timer_ = 0.0f;

    // Fade alpha pro text (0-255)
    float text_alpha_ = 0.0f;

    // Skip na stisk klávesy
    bool skip_ = false;

    // Klasická AI střelba v intro
    float cannon_x_ = GAME_WIDTH / 2.0f;
    float cannon_dir_ = 1.0f;
    float bullet_y_ = -10.0f; // -10 = neaktivní
    float bullet_x_ = 0.0f;
    float shoot_timer_ = 0.0f;

    // Zásahy v intro (pro dramatický efekt)
    bool intro_aliens_dead_[5 * 8] = {};
    int intro_kill_count_ = 0;
};
