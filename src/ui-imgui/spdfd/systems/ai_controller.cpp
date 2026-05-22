#include "ai_controller.h"
#include "ui-imgui/spdfd/entities/formation.h"
#include "ui-imgui/spdfd/constants.h"
#include <cstdlib>
#include <cmath>

// Pomocná funkce — najít X střed nejbližšího živého vetřelce k dělu
static float find_nearest_alien_x(const Formation& formation) {
    float best_x = GAME_WIDTH / 2.0f;
    float best_dist = 99999.0f;

    for (int row = formation.rows() - 1; row >= 0; --row) {
        for (int col = 0; col < formation.cols(); ++col) {
            if (!formation.alien_at(row, col).alive) continue;
            float ax = formation.alien_world_x(col) + ALIEN_WIDTH / 2.0f;
            float ay = formation.alien_world_y(row);
            float dist = (CANNON_Y - ay) + std::abs(ax - GAME_WIDTH / 2.0f) * 0.3f;
            if (dist < best_dist) {
                best_dist = dist;
                best_x = ax;
            }
        }
    }
    return best_x;
}

// Pomocná funkce — plynulý pohyb k cíli
static void move_towards(float& x, float target, float speed, float dt) {
    float diff = target - x;
    float move = speed * dt;
    if (std::abs(diff) < move) {
        x = target;
    } else {
        x += (diff > 0 ? 1.0f : -1.0f) * move;
    }
}

// =============================================================================
// Level 1 — RandomAI (Rookie Gunner)
// Pomalý, střílí řídce a náhodně.
// =============================================================================

void RandomAI::update(double dt, float& cannon_x, const Formation& formation) {
    float fdt = static_cast<float>(dt);

    cannon_x += move_dir_ * CANNON_BASE_SPEED * fdt;

    move_timer_ += fdt;
    if (move_timer_ >= direction_change_) {
        move_timer_ = 0.0f;
        move_dir_ = -move_dir_;
        direction_change_ = 1.0f + static_cast<float>(std::rand() % 200) / 100.0f;
    }

    fire_timer_ += fdt;
}

bool RandomAI::should_fire(const Formation& formation) {
    if (fire_timer_ >= next_fire_) {
        fire_timer_ = 0.0f;
        next_fire_ = 0.5f + static_cast<float>(std::rand() % 100) / 100.0f;
        return true;
    }
    return false;
}

// =============================================================================
// Level 2 — TargetingAI (Trained Gunner)
// Míří na nejbližšího vetřelce, pohybuje se rychleji, střílí častěji.
// =============================================================================

void TargetingAI::update(double dt, float& cannon_x, const Formation& formation) {
    float fdt = static_cast<float>(dt);

    float target_x = find_nearest_alien_x(formation);
    move_towards(cannon_x, target_x, SPEED, fdt);

    fire_timer_ += fdt;
}

bool TargetingAI::should_fire(const Formation& formation) {
    if (fire_timer_ >= next_fire_) {
        fire_timer_ = 0.0f;
        next_fire_ = 0.3f + static_cast<float>(std::rand() % 50) / 100.0f;
        return true;
    }
    return false;
}

// =============================================================================
// Level 3 — PredictiveAI (Veteran Gunner)
// Predikuje pohyb, rychlý, přesný.
// =============================================================================

void PredictiveAI::update(double dt, float& cannon_x, const Formation& formation) {
    float fdt = static_cast<float>(dt);

    float current_fx = formation.x();
    if (fdt > 0) {
        formation_vx_ = (current_fx - prev_formation_x_) / fdt;
    }
    prev_formation_x_ = current_fx;

    float nearest_x = find_nearest_alien_x(formation);
    float flight_time = CANNON_Y / BULLET_SPEED;
    float predicted_x = nearest_x + formation_vx_ * flight_time * 0.8f;

    predicted_x = std::max(0.0f, std::min(predicted_x, static_cast<float>(GAME_WIDTH)));
    move_towards(cannon_x, predicted_x, SPEED, fdt);

    fire_timer_ += fdt;
}

bool PredictiveAI::should_fire(const Formation& formation) {
    if (fire_timer_ >= next_fire_) {
        fire_timer_ = 0.0f;
        next_fire_ = 0.25f + static_cast<float>(std::rand() % 35) / 100.0f;
        return true;
    }
    return false;
}

// =============================================================================
// Level 4 — AceAI (Ace Gunner)
// Cílí na okraje formace, double shot, velmi rychlý.
// =============================================================================

float AceAI::find_edge_alien_x(const Formation& formation) {
    float leftmost = GAME_WIDTH;
    float rightmost = 0;

    for (int row = 0; row < formation.rows(); ++row) {
        for (int col = 0; col < formation.cols(); ++col) {
            if (!formation.alien_at(row, col).alive) continue;
            float ax = formation.alien_world_x(col) + ALIEN_WIDTH / 2.0f;
            if (ax < leftmost) leftmost = ax;
            if (ax > rightmost) rightmost = ax;
        }
    }

    if (std::rand() % 2 == 0) return leftmost;
    return rightmost;
}

void AceAI::update(double dt, float& cannon_x, const Formation& formation) {
    float fdt = static_cast<float>(dt);

    float target_x = find_edge_alien_x(formation);
    move_towards(cannon_x, target_x, SPEED, fdt);

    fire_timer_ += fdt;
}

bool AceAI::should_fire(const Formation& formation) {
    // Double shot
    if (burst_remaining_ > 0) {
        if (fire_timer_ >= BURST_INTERVAL) {
            fire_timer_ = 0.0f;
            burst_remaining_--;
            return true;
        }
        return false;
    }

    if (fire_timer_ >= next_fire_) {
        fire_timer_ = 0.0f;
        next_fire_ = 0.35f + static_cast<float>(std::rand() % 45) / 100.0f;
        burst_remaining_ = 1;
        return true;
    }
    return false;
}

// =============================================================================
// Level 5 — CommanderAI (Commander)
// Prediktivní, rychlé přepínání cover/attack, agresivní střelba.
// =============================================================================

void CommanderAI::update(double dt, float& cannon_x, const Formation& formation) {
    float fdt = static_cast<float>(dt);

    float current_fx = formation.x();
    if (fdt > 0) {
        formation_vx_ = (current_fx - prev_formation_x_) / fdt;
    }
    prev_formation_x_ = current_fx;

    mode_timer_ += fdt;
    if (mode_timer_ >= 1.5f) {
        mode_timer_ = 0.0f;
        cover_mode_ = !cover_mode_;
    }

    float target_x;
    if (cover_mode_) {
        float center = formation.x() + (formation.cols() * ALIEN_SPACING_X) / 2.0f;
        target_x = center;
    } else {
        float nearest_x = find_nearest_alien_x(formation);
        float flight_time = CANNON_Y / BULLET_SPEED;
        target_x = nearest_x + formation_vx_ * flight_time * 0.9f;
    }

    target_x = std::max(0.0f, std::min(target_x, static_cast<float>(GAME_WIDTH)));
    move_towards(cannon_x, target_x, SPEED, fdt);

    fire_timer_ += fdt;
}

bool CommanderAI::should_fire(const Formation& formation) {
    if (fire_timer_ >= next_fire_) {
        fire_timer_ = 0.0f;
        next_fire_ = 0.2f + static_cast<float>(std::rand() % 25) / 100.0f;
        return true;
    }
    return false;
}
