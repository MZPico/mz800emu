#include "interlevel_state.h"
#include "gameplay_state.h"
#include "title_state.h"
#include "ui-imgui/spdfd/renderer.h"
#include "ui-imgui/spdfd/input.h"
#include "ui-imgui/spdfd/audio.h"
#include "ui-imgui/spdfd/ui/hud.h"
#include "ui-imgui/spdfd/constants.h"
#include <cstring>

InterlevelState::InterlevelState(int completed_level, bool hero_survived,
                                 int aliens_lost)
    : completed_level_(completed_level),
      hero_survived_(hero_survived),
      aliens_lost_(aliens_lost),
      total_aliens_lost_(aliens_lost) {}

void InterlevelState::enter() {
    proceed_ = false;
    timer_ = 0.0f;
    char_timer_ = 0.0f;
    chars_shown_ = 0;
}

const char* InterlevelState::hq_message() const {
    switch (completed_level_) {
        case 1: return "SEND BETTER GUNNER.\nTHIS ONE IS EMBARRASSING.";
        case 2: return "THEY'RE STILL ALIVE?\nINCREASE BUDGET.";
        case 3: return "WHO APPROVED\nTHEIR TRAVEL VISA?";
        case 4: return "DEPLOY THE TWINS.\nYES, BOTH OF THEM.";
        default: return "...";
    }
}

void InterlevelState::update(double dt, const Input& input) {
    float fdt = static_cast<float>(dt);
    timer_ += fdt;

    // Efekt psaní po znacích
    char_timer_ += fdt;
    int msg_len = static_cast<int>(std::strlen(hq_message()));
    while (char_timer_ >= CHAR_SPEED && chars_shown_ < msg_len) {
        char_timer_ -= CHAR_SPEED;
        chars_shown_++;
        // Syntetizovaný hlas — Animal Crossing styl (přeskočit mezery a newliny)
        char ch = hq_message()[chars_shown_ - 1];
        if (ch != ' ' && ch != '\n') {
            g_spdfd_audio.play_speech_char(ch);
        }
    }

    // ESC — přeskočit na title screen
    if (input.escape_pressed()) {
        esc_quit_ = true;
    }

    // Po minimální době zobrazení lze pokračovat
    if (timer_ >= MIN_DISPLAY_TIME) {
        if (input.enter_pressed() || input.fire_pressed()) {
            proceed_ = true;
        }
    }
}

void InterlevelState::render(Renderer& renderer) {
    renderer.clear(0, 0, 0);

    // Hlavička — terminálový styl
    Hud::draw_text_centered(renderer, "/// AI HQ TRANSMISSION ///",
                            GAME_WIDTH / 2, 40, 50, 200, 50);

    // Zpráva s efektem postupného psaní
    const char* msg = hq_message();
    int msg_len = static_cast<int>(std::strlen(msg));
    int show = (chars_shown_ < msg_len) ? chars_shown_ : msg_len;

    // Rozdělit zprávu na řádky a vykreslit
    char buf[256];
    std::strncpy(buf, msg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (show < static_cast<int>(sizeof(buf))) buf[show] = '\0';

    // Najít řádky (oddělené \n)
    float y = 90.0f;
    char* line = buf;
    for (char* p = buf; ; ++p) {
        if (*p == '\n' || *p == '\0') {
            bool end = (*p == '\0');
            *p = '\0';
            Hud::draw_text_centered(renderer, line, GAME_WIDTH / 2, y,
                                    150, 255, 150);
            y += 12.0f;
            if (end) break;
            line = p + 1;
        }
    }

    // Blikající kurzor — na konci posledního vypsaného řádku
    if (chars_shown_ < msg_len || static_cast<int>(timer_ * 2) % 2 == 0) {
        float last_w = static_cast<float>(Hud::text_width(line));
        float cursor_x = (GAME_WIDTH / 2.0f) + last_w / 2.0f + 1.0f;
        Hud::draw_text(renderer, "_", cursor_x, y - 12.0f,
                       150, 255, 150);
    }

    // Level info
    char level_buf[64];
    std::snprintf(level_buf, sizeof(level_buf), "LEVEL %d COMPLETE", completed_level_);
    Hud::draw_text_centered(renderer, level_buf, GAME_WIDTH / 2, 140,
                            200, 200, 200);

    // Ztráty
    if (aliens_lost_ > 0) {
        char loss_buf[64];
        std::snprintf(loss_buf, sizeof(loss_buf), "CASUALTIES: %d", aliens_lost_);
        Hud::draw_text_centered(renderer, loss_buf, GAME_WIDTH / 2, 152,
                                200, 100, 100);
    } else {
        Hud::draw_text_centered(renderer, "NO CASUALTIES!", GAME_WIDTH / 2, 152,
                                50, 255, 50);
    }

    // Level 4: hrdina přežil
    if (hero_survived_ && completed_level_ == 4) {
        Hud::draw_text_centered(renderer, "HERO PROMOTED!", GAME_WIDTH / 2, 166,
                                255, 215, 0);
    }

    // Pokračování
    if (timer_ >= MIN_DISPLAY_TIME) {
        if (static_cast<int>(timer_ * 2) % 2 == 0) {
            Hud::draw_text_centered(renderer, "PRESS ENTER", GAME_WIDTH / 2, 190,
                                    255, 255, 255);
        }
    }
}

std::unique_ptr<GameState> InterlevelState::next_state() {
    if (esc_quit_) {
        return std::make_unique<TitleState>();
    }
    if (proceed_) {
        return std::make_unique<GameplayState>(completed_level_ + 1);
    }
    return nullptr;
}
