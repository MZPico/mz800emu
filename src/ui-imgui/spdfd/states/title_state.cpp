#include "title_state.h"
#include "gameplay_state.h"
#include "ui-imgui/spdfd/renderer.h"
#include "ui-imgui/spdfd/input.h"
#include "ui-imgui/spdfd/audio.h"
#include "ui-imgui/spdfd/ui/hud.h"
#include "ui-imgui/spdfd/constants.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

// Text sinusového scrolleru
static const char* SCROLLER_TEXT =
    "    SPACE DEFENDERS --- "
    "ARROWS = MOVE FORMATION / SPACE = FIRE / SURVIVE! --- "
    "YOU ARE THE ALIENS. THE CANNON IS THE ENEMY. --- "
    "PROTECT YOUR FORMATION AT ALL COSTS! --- "
    "A GAME BY MZ800EMU --- "
    "    ";

void TitleState::enter() {
    start_game_ = false;
    blink_timer_ = 0.0f;
    blink_visible_ = true;
    timer_ = 0.0f;
    scroller_offset_ = 0.0f;

    // Pokud intro melodie ještě nehraje, spustit ji
    if (!g_spdfd_audio.melody_playing()) {
        g_spdfd_audio.start_intro_melody();
    }

    // Inicializovat demo formaci
    demo_formation_x_ = (GAME_WIDTH - DEMO_COLS * ALIEN_SPACING_X) / 2.0f;
    demo_formation_dir_ = 1.0f;
    demo_anim_frame_ = 0;
    demo_anim_timer_ = 0.0f;
    demo_cannon_x_ = GAME_WIDTH / 2.0f;
    demo_cannon_dir_ = 1.0f;
    demo_bullet_y_ = -10.0f;
    demo_shoot_timer_ = 0.0f;
    demo_reset_timer_ = 0.0f;

    for (int i = 0; i < DEMO_ROWS * DEMO_COLS; ++i) {
        demo_alive_[i] = true;
    }
}

void TitleState::update(double dt, const Input& input) {
    float fdt = static_cast<float>(dt);
    timer_ += fdt;

    // Audio update pro melodii
    g_spdfd_audio.update(fdt);

    // Blikání textu
    blink_timer_ += fdt;
    if (blink_timer_ >= 0.5f) {
        blink_timer_ -= 0.5f;
        blink_visible_ = !blink_visible_;
    }

    // Sinusový scroller — posun textu doleva
    scroller_offset_ += 30.0f * fdt; // 30 pixelů za sekundu
    float text_total_width = static_cast<float>(std::strlen(SCROLLER_TEXT)) * 6.0f;
    if (scroller_offset_ >= text_total_width) {
        scroller_offset_ -= text_total_width;
    }

    // --- Demo update ---

    // Animace vetřelců
    demo_anim_timer_ += fdt;
    if (demo_anim_timer_ >= 0.5f) {
        demo_anim_timer_ -= 0.5f;
        demo_anim_frame_ = 1 - demo_anim_frame_;
    }

    // Počet živých vetřelců
    int alive_count = 0;
    for (int i = 0; i < DEMO_ROWS * DEMO_COLS; ++i) {
        if (demo_alive_[i]) alive_count++;
    }

    // Reset dema pokud jsou všichni mrtví
    if (alive_count == 0) {
        demo_reset_timer_ += fdt;
        if (demo_reset_timer_ >= 2.0f) {
            for (int i = 0; i < DEMO_ROWS * DEMO_COLS; ++i) {
                demo_alive_[i] = true;
            }
            demo_formation_x_ = (GAME_WIDTH - DEMO_COLS * ALIEN_SPACING_X) / 2.0f;
            demo_reset_timer_ = 0.0f;
        }
    }

    // Pohyb formace — automatický ping-pong
    float formation_width = DEMO_COLS * ALIEN_SPACING_X;
    demo_formation_x_ += demo_formation_dir_ * 15.0f * fdt;
    if (demo_formation_x_ + formation_width > GAME_WIDTH) {
        demo_formation_dir_ = -1.0f;
    }
    if (demo_formation_x_ < 0) {
        demo_formation_dir_ = 1.0f;
    }

    // Pohyb AI děla
    demo_cannon_x_ += demo_cannon_dir_ * 40.0f * fdt;
    if (demo_cannon_x_ > GAME_WIDTH - 20) demo_cannon_dir_ = -1.0f;
    if (demo_cannon_x_ < 20) demo_cannon_dir_ = 1.0f;

    // Střelba AI děla
    demo_shoot_timer_ += fdt;
    if (demo_bullet_y_ < 0 && demo_shoot_timer_ >= 1.2f) {
        demo_bullet_x_ = demo_cannon_x_;
        demo_bullet_y_ = CANNON_Y - 2;
        demo_shoot_timer_ = 0.0f;
    }

    // Pohyb střely
    if (demo_bullet_y_ >= 0) {
        demo_bullet_y_ -= BULLET_SPEED * fdt;

        // Kontrola zásahu
        for (int row = DEMO_ROWS - 1; row >= 0; --row) {
            for (int col = 0; col < DEMO_COLS; ++col) {
                int idx = row * DEMO_COLS + col;
                if (!demo_alive_[idx]) continue;

                float ax = demo_formation_x_ + col * ALIEN_SPACING_X;
                float ay = FORMATION_TOP + row * ALIEN_SPACING_Y;

                if (demo_bullet_x_ >= ax && demo_bullet_x_ <= ax + ALIEN_WIDTH &&
                    demo_bullet_y_ >= ay && demo_bullet_y_ <= ay + ALIEN_HEIGHT) {
                    demo_alive_[idx] = false;
                    demo_bullet_y_ = -10.0f;
                }
            }
        }

        if (demo_bullet_y_ < 0) demo_bullet_y_ = -10.0f;
    }

    if (input.escape_pressed()) {
        quit_ = true;
    }

    if (input.enter_pressed() || input.fire_pressed()) {
        start_game_ = true;
    }
}

void TitleState::render(Renderer& renderer) {
    renderer.clear(0, 0, 0);

    // --- Demo formace na pozadí (přitlumená) ---
    for (int row = 0; row < DEMO_ROWS; ++row) {
        for (int col = 0; col < DEMO_COLS; ++col) {
            int idx = row * DEMO_COLS + col;
            if (!demo_alive_[idx]) continue;

            float ax = demo_formation_x_ + col * ALIEN_SPACING_X;
            float ay = FORMATION_TOP + row * ALIEN_SPACING_Y;

            const uint8_t* sprite;
            uint8_t r, g, b;
            if (row == 0) {
                sprite = (demo_anim_frame_ == 0) ? SQUID_FRAME1 : SQUID_FRAME2;
                r = 120; g = 25; b = 25;  // přitlumená červená
            } else if (row < 3) {
                sprite = (demo_anim_frame_ == 0) ? CRAB_FRAME1 : CRAB_FRAME2;
                r = 25; g = 120; b = 25;  // přitlumená zelená
            } else {
                sprite = (demo_anim_frame_ == 0) ? OCTOPUS_FRAME1 : OCTOPUS_FRAME2;
                r = 50; g = 50; b = 120;  // přitlumená modrá
            }

            renderer.draw_sprite(sprite, ALIEN_WIDTH, ALIEN_HEIGHT, ax, ay, r, g, b);
        }
    }

    // Demo AI dělo (přitlumené)
    renderer.draw_sprite(CANNON_SPRITE, CANNON_SPRITE_W, CANNON_SPRITE_H,
                         demo_cannon_x_ - CANNON_SPRITE_W / 2.0f, CANNON_Y,
                         100, 100, 100);

    // Demo střela
    if (demo_bullet_y_ >= 0) {
        renderer.fill_rect(demo_bullet_x_, demo_bullet_y_, 1, 4, 150, 150, 150);
    }

    // --- Titulky přes demo ---
    // Titulek "SPACE DEFENDERS"
    Hud::draw_text_centered(renderer, "SPACE DEFENDERS", GAME_WIDTH / 2, 80,
                            0, 255, 100);

    // Podtitulek
    Hud::draw_text_centered(renderer, "YOU ARE NOT THE HERO", GAME_WIDTH / 2, 100,
                            180, 180, 180);

    // Blikající "PRESS ENTER"
    if (blink_visible_) {
        Hud::draw_text_centered(renderer, "PRESS ENTER", GAME_WIDTH / 2, 140,
                                255, 255, 255);
    }

    // --- Sinusový scroller dole ---
    float scroller_y_base = static_cast<float>(GAME_HEIGHT - 14);
    int text_len = static_cast<int>(std::strlen(SCROLLER_TEXT));
    float total_width = text_len * 6.0f;

    // Vykreslíme jen viditelné znaky
    for (int i = 0; i < text_len; ++i) {
        float char_x = i * 6.0f - scroller_offset_;

        // Wrap around
        if (char_x < -6.0f) char_x += total_width;
        if (char_x > GAME_WIDTH) continue;
        if (char_x < -6.0f) continue;

        // Sinusový offset v Y
        float wave = std::sin((char_x + timer_ * 40.0f) * 0.04f) * 4.0f;
        float char_y = scroller_y_base + wave;

        // Barva se mění podél textu — duhový efekt
        float hue = std::fmod(char_x * 0.01f + timer_ * 0.3f, 1.0f);
        // Jednoduchá HSV → RGB aproximace
        uint8_t r, g, b;
        float h6 = hue * 6.0f;
        int sector = static_cast<int>(h6);
        float frac = h6 - sector;
        switch (sector % 6) {
            case 0: r = 255; g = static_cast<uint8_t>(255 * frac); b = 0; break;
            case 1: r = static_cast<uint8_t>(255 * (1.0f - frac)); g = 255; b = 0; break;
            case 2: r = 0; g = 255; b = static_cast<uint8_t>(255 * frac); break;
            case 3: r = 0; g = static_cast<uint8_t>(255 * (1.0f - frac)); b = 255; break;
            case 4: r = static_cast<uint8_t>(255 * frac); g = 0; b = 255; break;
            default: r = 255; g = 0; b = static_cast<uint8_t>(255 * (1.0f - frac)); break;
        }

        // Vykreslit jednotlivý znak
        char ch_buf[2] = { SCROLLER_TEXT[i], '\0' };
        Hud::draw_text(renderer, ch_buf, char_x, char_y, r, g, b);
    }
}

std::unique_ptr<GameState> TitleState::next_state() {
    if (start_game_) {
        return std::make_unique<GameplayState>();
    }
    return nullptr;
}
