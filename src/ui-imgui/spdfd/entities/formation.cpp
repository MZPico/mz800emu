#include "formation.h"
#include "ui-imgui/spdfd/renderer.h"
#include "ui-imgui/spdfd/input.h"
#include "ui-imgui/spdfd/constants.h"
#include <cstdlib>

void Formation::init(int rows, int cols) {
    rows_ = rows;
    cols_ = cols;
    aliens_.resize(rows * cols);

    // Umístit formaci na střed
    float formation_width = cols * ALIEN_SPACING_X;
    x_ = (GAME_WIDTH - formation_width) / 2.0f;
    y_ = FORMATION_TOP;

    // Přiřadit typy vetřelců podle řad (shora dolů)
    for (int row = 0; row < rows; ++row) {
        AlienType type;
        if (row == 0) {
            type = AlienType::SQUID;     // horní řada
        } else if (row < rows - 2) {
            type = AlienType::CRAB;      // střední řady
        } else {
            type = AlienType::OCTOPUS;   // spodní řady
        }

        for (int col = 0; col < cols; ++col) {
            auto& alien = alien_at(row, col);
            alien.type = type;
            alien.alive = true;
            alien.exploding = false;
        }
    }

    fire_cooldown_ = 0.0f;
    anim_frame_ = 0;
    anim_timer_ = 0.0f;
}

void Formation::update(double dt, const Input& input) {
    float speed = current_speed();

    // Pohyb formace podle vstupu hráče
    if (input.left()) {
        x_ -= speed * static_cast<float>(dt);
    }
    if (input.right()) {
        x_ += speed * static_cast<float>(dt);
    }

    // Omezení na obrazovku
    float formation_width = cols_ * ALIEN_SPACING_X;
    if (x_ < 0) x_ = 0;
    if (x_ + formation_width > GAME_WIDTH) x_ = GAME_WIDTH - formation_width;

    // Animace — přepínání framů
    anim_timer_ += static_cast<float>(dt);
    if (anim_timer_ >= ANIM_INTERVAL) {
        anim_timer_ -= ANIM_INTERVAL;
        anim_frame_ = 1 - anim_frame_;
    }

    // Update vetřelců (exploze)
    for (auto& alien : aliens_) {
        alien.update(dt);
    }

    // Cooldown střelby
    if (fire_cooldown_ > 0) {
        fire_cooldown_ -= static_cast<float>(dt);
    }
}

void Formation::render(Renderer& renderer) const {
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            float wx = alien_world_x(col);
            float wy = alien_world_y(row);
            alien_at(row, col).render(renderer, wx, wy, anim_frame_);
        }
    }
}

Bullet Formation::try_fire() {
    Bullet bullet;
    bullet.active = false;

    if (fire_cooldown_ > 0) return bullet;

    // Najít náhodného živého vetřelce ve spodní řadě (nejnižší živý v každém sloupci)
    // Pro jednoduchost — vybereme náhodný sloupec a najdeme nejnižšího živého
    std::vector<int> valid_cols;
    for (int col = 0; col < cols_; ++col) {
        for (int row = rows_ - 1; row >= 0; --row) {
            if (alien_at(row, col).alive) {
                valid_cols.push_back(col);
                break;
            }
        }
    }

    if (valid_cols.empty()) return bullet;

    int chosen_col = valid_cols[std::rand() % valid_cols.size()];

    // Najít nejnižšího živého v tomto sloupci
    for (int row = rows_ - 1; row >= 0; --row) {
        if (alien_at(row, chosen_col).alive) {
            bullet.x = alien_world_x(chosen_col) + ALIEN_WIDTH / 2.0f;
            bullet.y = alien_world_y(row) + ALIEN_HEIGHT;
            bullet.vy = BULLET_SPEED;  // dolů
            bullet.owner = BulletOwner::ALIEN;
            bullet.active = true;
            fire_cooldown_ = FIRE_RATE;
            break;
        }
    }

    return bullet;
}

int Formation::alive_count() const {
    int count = 0;
    for (const auto& alien : aliens_) {
        if (alien.alive) ++count;
    }
    return count;
}

float Formation::current_speed() const {
    int total = total_count();
    int alive = alive_count();
    if (total == 0) return FORMATION_BASE_SPEED;

    // Čím méně vetřelců, tím rychlejší formace
    float ratio = 1.0f + 2.0f * (1.0f - static_cast<float>(alive) / total);
    return FORMATION_BASE_SPEED * ratio;
}
