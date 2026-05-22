#include "endgame_state.h"
#include "title_state.h"
#include "ui-imgui/spdfd/renderer.h"
#include "ui-imgui/spdfd/input.h"
#include "ui-imgui/spdfd/audio.h"
#include "ui-imgui/spdfd/ui/hud.h"
#include "ui-imgui/spdfd/constants.h"

// Sprite domečku — 24x16 pixelů
static constexpr int HOUSE_W = 24;
static constexpr int HOUSE_H = 16;
static constexpr uint8_t HOUSE_SPRITE[] = {
    0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
    0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,1,1,1,0,0,1,1,1,1,1,1,0,0,1,1,1,0,0,0,0,
    0,0,0,0,1,1,1,0,0,1,1,1,1,1,1,0,0,1,1,1,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,0,0,0,0,
};

void EndgameState::enter() {
    timer_ = 0.0f;
    done_ = false;
    phase_ = Phase::LANDING;
    phase_timer_ = 0.0f;
    formation_y_ = 60.0f;
    house_progress_ = 0;

    // Zastavit heartbeat a spustit klidnou endgame melodii
    g_spdfd_audio.stop_melody();
    g_spdfd_audio.start_endgame_melody();
}

void EndgameState::update(double dt, const Input& input) {
    float fdt = static_cast<float>(dt);
    timer_ += fdt;
    phase_timer_ += fdt;

    // Audio update pro melodii
    g_spdfd_audio.update(fdt);

    // ESC kdykoliv → návrat na title
    if (input.escape_pressed()) {
        done_ = true;
        return;
    }

    switch (phase_) {
        case Phase::LANDING:
            // Formace klesá dolů
            formation_y_ += 15.0f * fdt;
            if (formation_y_ >= CANNON_Y - 30) {
                phase_ = Phase::BUILDING;
                phase_timer_ = 0.0f;
            }
            break;

        case Phase::BUILDING:
            // Postupně stavět dům
            if (phase_timer_ >= 0.3f) {
                phase_timer_ = 0.0f;
                house_progress_++;
                if (house_progress_ >= HOUSE_H) {
                    phase_ = Phase::TEXT;
                    phase_timer_ = 0.0f;
                }
            }
            break;

        case Phase::TEXT:
            if (phase_timer_ >= 3.0f) {
                phase_ = Phase::HQ_MESSAGE;
                phase_timer_ = 0.0f;
            }
            break;

        case Phase::HQ_MESSAGE:
            if (phase_timer_ >= 3.0f) {
                phase_ = Phase::WAIT;
                phase_timer_ = 0.0f;
            }
            break;

        case Phase::WAIT:
            if (input.enter_pressed() || input.fire_pressed() || input.escape_pressed()) {
                done_ = true;
            }
            break;
    }
}

void EndgameState::render(Renderer& renderer) {
    renderer.clear(0, 0, 0);

    // Zelená "země" dole
    renderer.fill_rect(0, CANNON_Y + 10, GAME_WIDTH, GAME_HEIGHT - CANNON_Y - 10,
                       30, 80, 30);

    float house_x = GAME_WIDTH / 2.0f - HOUSE_W / 2.0f;
    float house_y = CANNON_Y + 10 - HOUSE_H;

    if (phase_ == Phase::LANDING) {
        // Malá formace vetřelců klesá
        for (int i = 0; i < 6; ++i) {
            float ax = GAME_WIDTH / 2.0f - 40 + i * 14.0f;
            renderer.fill_rect(ax, formation_y_, 8, 6, 50, 255, 50);
        }
    }

    if (phase_ >= Phase::BUILDING) {
        // Vetřelci stojí vedle domku
        for (int i = 0; i < 3; ++i) {
            float ax = house_x - 20 + i * 10.0f;
            renderer.fill_rect(ax, CANNON_Y + 2, 8, 6, 50, 255, 50);
        }
        for (int i = 0; i < 3; ++i) {
            float ax = house_x + HOUSE_W + 4 + i * 10.0f;
            renderer.fill_rect(ax, CANNON_Y + 2, 8, 6, 50, 255, 50);
        }

        // Dům se postupně staví (řádek po řádku zdola nahoru)
        int rows_to_show = house_progress_;
        if (rows_to_show > HOUSE_H) rows_to_show = HOUSE_H;

        for (int row = HOUSE_H - rows_to_show; row < HOUSE_H; ++row) {
            for (int col = 0; col < HOUSE_W; ++col) {
                if (HOUSE_SPRITE[row * HOUSE_W + col]) {
                    // Střecha = hnědá, stěny = béžová, okna = modrá, dveře = tmavá
                    uint8_t r, g, b;
                    if (row < 8) {
                        r = 150; g = 80; b = 40; // střecha
                    } else if ((col >= 7 && col <= 8 && row >= 10 && row <= 11) ||
                               (col >= 15 && col <= 16 && row >= 10 && row <= 11)) {
                        r = 100; g = 150; b = 255; // okna
                    } else if (col >= 10 && col <= 13 && row >= 12) {
                        r = 80; g = 50; b = 30; // dveře
                    } else {
                        r = 200; g = 180; b = 140; // stěny
                    }
                    renderer.draw_pixel(house_x + col, house_y + row, r, g, b);
                }
            }
        }
    }

    if (phase_ >= Phase::TEXT) {
        if (perfect_) {
            // Perfektní hra — speciální text
            Hud::draw_text_centered(renderer, "PERFECT INVASION.",
                                    GAME_WIDTH / 2, 22, 255, 215, 0);
            Hud::draw_text_centered(renderer, "HR WOULD LIKE A WORD.",
                                    GAME_WIDTH / 2, 34, 255, 215, 0);
        } else {
            Hud::draw_text_centered(renderer, "THEY JUST WANTED A HOME.",
                                    GAME_WIDTH / 2, 30, 255, 255, 200);
        }
    }

    if (phase_ >= Phase::HQ_MESSAGE) {
        Hud::draw_text_centered(renderer, "/// AI HQ ///",
                                GAME_WIDTH / 2, 60, 50, 200, 50);
        Hud::draw_text_centered(renderer, "MEETING AT 9 AM.",
                                GAME_WIDTH / 2, 72, 150, 255, 150);
        Hud::draw_text_centered(renderer, "BRING YOUR RESIGNATION.",
                                GAME_WIDTH / 2, 84, 150, 255, 150);
    }

    if (phase_ == Phase::WAIT) {
        if (static_cast<int>(timer_ * 2) % 2 == 0) {
            Hud::draw_text_centered(renderer, "PRESS ENTER",
                                    GAME_WIDTH / 2, 200, 255, 255, 255);
        }
    }
}

std::unique_ptr<GameState> EndgameState::next_state() {
    if (done_) {
        return std::make_unique<TitleState>();
    }
    return nullptr;
}
