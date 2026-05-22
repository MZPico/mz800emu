#include "gameover_state.h"
#include "title_state.h"
#include "ui-imgui/spdfd/renderer.h"
#include "ui-imgui/spdfd/input.h"
#include "ui-imgui/spdfd/ui/hud.h"
#include "ui-imgui/spdfd/constants.h"
#include <cstdio>

void GameOverState::enter() {
    restart_ = false;
    blink_timer_ = 0.0f;
    blink_visible_ = true;
}

void GameOverState::update(double dt, const Input& input) {
    blink_timer_ += static_cast<float>(dt);
    if (blink_timer_ >= 0.5f) {
        blink_timer_ -= 0.5f;
        blink_visible_ = !blink_visible_;
    }

    if (input.enter_pressed() || input.fire_pressed() || input.escape_pressed()) {
        restart_ = true;
    }
}

void GameOverState::render(Renderer& renderer) {
    renderer.clear(0, 0, 0);

    float cy = GAME_HEIGHT / 2.0f;

    if (won_) {
        Hud::draw_text_centered(renderer, "LEVEL COMPLETE!", GAME_WIDTH / 2, cy - 20,
                                50, 255, 50);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "GUNNER DEFEATED AT LEVEL %d", level_);
        Hud::draw_text_centered(renderer, buf, GAME_WIDTH / 2, cy,
                                200, 200, 200);
    } else {
        Hud::draw_text_centered(renderer, "GAME OVER", GAME_WIDTH / 2, cy - 20,
                                255, 50, 50);
        Hud::draw_text_centered(renderer, "YOUR FORMATION WAS DESTROYED", GAME_WIDTH / 2, cy,
                                200, 200, 200);

        // Meta humor — speciální texty podle situace
        if (afk_loss_) {
            // Idle hráč - nikdy se nehýbal
            Hud::draw_text_centered(renderer,
                "YOU DIDN'T EVEN TRY?",
                GAME_WIDTH / 2, cy + 16, 150, 150, 50);
            Hud::draw_text_centered(renderer,
                "...JUST LIKE THE ORIGINAL PLAYER.",
                GAME_WIDTH / 2, cy + 28, 120, 120, 50);
        } else if (level_ == 1) {
            Hud::draw_text_centered(renderer,
                "EVEN THE INVADERS ARE DISAPPOINTED.",
                GAME_WIDTH / 2, cy + 20, 150, 150, 50);
        }
    }

    if (blink_visible_) {
        Hud::draw_text_centered(renderer, "PRESS ENTER", GAME_WIDTH / 2, cy + 50,
                                255, 255, 255);
    }
}

std::unique_ptr<GameState> GameOverState::next_state() {
    if (restart_) {
        return std::make_unique<TitleState>();
    }
    return nullptr;
}
