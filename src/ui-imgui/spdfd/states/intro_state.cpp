#include "intro_state.h"
#include "title_state.h"
#include "ui-imgui/spdfd/renderer.h"
#include "ui-imgui/spdfd/input.h"
#include "ui-imgui/spdfd/audio.h"
#include "ui-imgui/spdfd/ui/hud.h"
#include "ui-imgui/spdfd/constants.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

static constexpr int INTRO_ROWS = 5;
static constexpr int INTRO_COLS = 8;
static constexpr float CLASSIC_DURATION = 4.0f;
static constexpr float PAN_DURATION = 2.5f;
static constexpr float REVEAL_DURATION = 3.5f;
static constexpr float TITLE_DURATION = 2.5f;

void IntroState::enter() {
    phase_ = Phase::CLASSIC_VIEW;
    timer_ = 0.0f;
    phase_timer_ = 0.0f;
    camera_y_ = 0.0f;
    anim_frame_ = 0;
    anim_timer_ = 0.0f;
    text_alpha_ = 0.0f;
    skip_ = false;
    cannon_x_ = GAME_WIDTH / 2.0f;
    cannon_dir_ = 1.0f;
    bullet_y_ = -10.0f;
    shoot_timer_ = 0.0f;
    std::memset(intro_aliens_dead_, 0, sizeof(intro_aliens_dead_));
    intro_kill_count_ = 0;

    // Spustit melancholickou intro melodii
    g_spdfd_audio.start_intro_melody();
}

void IntroState::update(double dt, const Input& input) {
    float fdt = static_cast<float>(dt);
    timer_ += fdt;
    phase_timer_ += fdt;

    // Audio update pro melodii
    g_spdfd_audio.update(fdt);

    // Skip celého intra
    if (input.enter_pressed() || input.fire_pressed() || input.escape_pressed()) {
        skip_ = true;
    }

    // Animace vetřelců
    anim_timer_ += fdt;
    if (anim_timer_ >= 0.5f) {
        anim_timer_ -= 0.5f;
        anim_frame_ = 1 - anim_frame_;
    }

    switch (phase_) {
        case Phase::CLASSIC_VIEW: {
            // Klasický pohled — AI dělo se pohybuje a střílí
            cannon_x_ += cannon_dir_ * 50.0f * fdt;
            if (cannon_x_ > GAME_WIDTH - 20) cannon_dir_ = -1.0f;
            if (cannon_x_ < 20) cannon_dir_ = 1.0f;

            // Střelba
            shoot_timer_ += fdt;
            if (bullet_y_ < 0 && shoot_timer_ >= 0.8f) {
                bullet_x_ = cannon_x_;
                bullet_y_ = CANNON_Y - 2;
                shoot_timer_ = 0.0f;
                g_spdfd_audio.play_cannon_shot();
            }

            // Pohyb střely nahoru
            if (bullet_y_ >= 0) {
                bullet_y_ -= BULLET_SPEED * fdt;

                // Kontrola zásahu
                for (int row = INTRO_ROWS - 1; row >= 0; --row) {
                    for (int col = 0; col < INTRO_COLS; ++col) {
                        int idx = row * INTRO_COLS + col;
                        if (intro_aliens_dead_[idx]) continue;

                        float ax = 32.0f + col * ALIEN_SPACING_X;
                        float ay = FORMATION_TOP + row * ALIEN_SPACING_Y;

                        if (bullet_x_ >= ax && bullet_x_ <= ax + ALIEN_WIDTH &&
                            bullet_y_ >= ay && bullet_y_ <= ay + ALIEN_HEIGHT) {
                            intro_aliens_dead_[idx] = true;
                            intro_kill_count_++;
                            bullet_y_ = -10.0f;
                            g_spdfd_audio.play_explosion();
                        }
                    }
                }

                if (bullet_y_ < 0) bullet_y_ = -10.0f;
            }

            if (phase_timer_ >= CLASSIC_DURATION) {
                phase_ = Phase::CAMERA_PAN;
                phase_timer_ = 0.0f;
            }
            break;
        }

        case Phase::CAMERA_PAN: {
            // Plynulý posun kamery nahoru — ease-in-out
            float t = phase_timer_ / PAN_DURATION;
            if (t > 1.0f) t = 1.0f;
            // Smoothstep
            float smooth = t * t * (3.0f - 2.0f * t);
            camera_y_ = smooth * 100.0f; // posun o 100 pixelů nahoru

            if (phase_timer_ >= PAN_DURATION) {
                phase_ = Phase::REVEAL_TEXT;
                phase_timer_ = 0.0f;
                text_alpha_ = 0.0f;
            }
            break;
        }

        case Phase::REVEAL_TEXT: {
            // Fade-in textu
            float t = phase_timer_ / 1.5f;
            if (t > 1.0f) t = 1.0f;
            text_alpha_ = t * 255.0f;

            if (phase_timer_ >= REVEAL_DURATION) {
                phase_ = Phase::TITLE;
                phase_timer_ = 0.0f;
            }
            break;
        }

        case Phase::TITLE: {
            if (phase_timer_ >= TITLE_DURATION) {
                phase_ = Phase::DONE;
            }
            break;
        }

        case Phase::DONE:
            break;
    }
}

void IntroState::render(Renderer& renderer) {
    renderer.clear(0, 0, 0);

    float cam = camera_y_;

    if (phase_ <= Phase::CAMERA_PAN) {
        // Vykreslit klasický Space Invaders pohled (s kamerou)

        // Formace vetřelců nahoře
        for (int row = 0; row < INTRO_ROWS; ++row) {
            for (int col = 0; col < INTRO_COLS; ++col) {
                int idx = row * INTRO_COLS + col;
                if (intro_aliens_dead_[idx]) continue;

                float ax = 32.0f + col * ALIEN_SPACING_X;
                float ay = FORMATION_TOP + row * ALIEN_SPACING_Y - cam;

                // Typ podle řady
                const uint8_t* sprite;
                uint8_t r, g, b;
                if (row == 0) {
                    sprite = (anim_frame_ == 0) ? SQUID_FRAME1 : SQUID_FRAME2;
                    r = 255; g = 50; b = 50;
                } else if (row < 3) {
                    sprite = (anim_frame_ == 0) ? CRAB_FRAME1 : CRAB_FRAME2;
                    r = 50; g = 255; b = 50;
                } else {
                    sprite = (anim_frame_ == 0) ? OCTOPUS_FRAME1 : OCTOPUS_FRAME2;
                    r = 100; g = 100; b = 255;
                }

                // Jen vykreslit pokud je na obrazovce
                if (ay > -ALIEN_HEIGHT && ay < GAME_HEIGHT) {
                    renderer.draw_sprite(sprite, ALIEN_WIDTH, ALIEN_HEIGHT,
                                         ax, ay, r, g, b);
                }
            }
        }

        // AI dělo dole
        float cannon_draw_y = CANNON_Y - cam;
        if (cannon_draw_y < GAME_HEIGHT) {
            renderer.draw_sprite(CANNON_SPRITE, CANNON_SPRITE_W, CANNON_SPRITE_H,
                                 cannon_x_ - CANNON_SPRITE_W / 2.0f, cannon_draw_y,
                                 200, 200, 200);
        }

        // Střela
        if (bullet_y_ >= 0) {
            float by = bullet_y_ - cam;
            if (by >= 0 && by < GAME_HEIGHT) {
                renderer.fill_rect(bullet_x_, by, 1, 4, 255, 255, 255);
            }
        }
    }

    // Text fáze
    if (phase_ == Phase::REVEAL_TEXT || phase_ == Phase::TITLE) {
        uint8_t a = static_cast<uint8_t>(text_alpha_);

        Hud::draw_text_centered(renderer, "YOU ARE NOT THE HERO.",
                                GAME_WIDTH / 2, 70, a, a, a);
        Hud::draw_text_centered(renderer, "YOU NEVER WERE.",
                                GAME_WIDTH / 2, 90, a, a, a);
    }

    if (phase_ == Phase::TITLE) {
        // Titulek fade-in
        float t = phase_timer_ / 1.0f;
        if (t > 1.0f) t = 1.0f;
        uint8_t g = static_cast<uint8_t>(255 * t);
        uint8_t r = static_cast<uint8_t>(0);
        uint8_t b = static_cast<uint8_t>(100 * t);

        Hud::draw_text_centered(renderer, "SPACE DEFENDERS",
                                GAME_WIDTH / 2, 130, r, g, b);
    }

    // Nápověda pro skip
    if (phase_ != Phase::DONE && timer_ > 1.5f) {
        if (static_cast<int>(timer_ * 2) % 2 == 0) {
            Hud::draw_text(renderer, "PRESS ENTER TO SKIP", 2, GAME_HEIGHT - 8,
                           80, 80, 80);
        }
    }
}

std::unique_ptr<GameState> IntroState::next_state() {
    if (skip_ || phase_ == Phase::DONE) {
        return std::make_unique<TitleState>();
    }
    return nullptr;
}
