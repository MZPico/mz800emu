#include "cannon.h"
#include "ui-imgui/spdfd/renderer.h"
#include "ui-imgui/spdfd/entities/sprites.h"
#include "ui-imgui/spdfd/systems/ai_controller.h"

Cannon::Cannon() = default;
Cannon::~Cannon() = default;

void Cannon::init(int level) {
    level_ = level;
    x_ = GAME_WIDTH / 2.0f;
    alive_ = true;
    departing_ = false;
    fire_cooldown_ = 0.0f;
    facepalm_timer_ = 0.0f;
    facepalm_offset_ = 0.0f;
    paused_ = false;
    hit_flash_timer_ = 0.0f;

    // HP děla — stejné ve všech levelech
    hp_ = 5;
    max_hp_ = hp_;

    second_alive_ = false;
    second_departing_ = false;
    second_ai_.reset();
    second_fire_cooldown_ = 0.0f;
    second_hp_ = 1;
    second_max_hp_ = 1;
    second_hit_flash_timer_ = 0.0f;

    // Vybrat AI strategii podle levelu
    switch (level) {
        case 1:  ai_ = std::make_unique<RandomAI>(); break;
        case 2:  ai_ = std::make_unique<TargetingAI>(); break;
        case 3:  ai_ = std::make_unique<PredictiveAI>(); break;
        case 4:  ai_ = std::make_unique<AceAI>(); break;
        case 5:
            ai_ = std::make_unique<CommanderAI>();
            // Druhé dělo — cover strategie
            second_alive_ = true;
            second_x_ = GAME_WIDTH / 3.0f;
            second_ai_ = std::make_unique<PredictiveAI>();
            second_fire_cooldown_ = 0.0f;
            second_hp_ = 5;
            second_max_hp_ = 5;
            break;
        default: ai_ = std::make_unique<RandomAI>(); break;
    }
}

void Cannon::hit() {
    if (!alive_ || hit_flash_timer_ > 0) return; // nesmrtelnost během blikání
    hp_--;
    hit_flash_timer_ = HIT_FLASH_DURATION;  // červený záblesk vždy — i při posledním zásahu
    if (hp_ <= 0) {
        alive_ = false;
        // Spustit animaci odjezdu — dělo odjede směrem k bližšímu okraji
        departing_ = true;
        depart_dir_ = (x_ > GAME_WIDTH / 2.0f) ? 1.0f : -1.0f;
    }
}

void Cannon::hit_second() {
    if (!second_alive_ || second_hit_flash_timer_ > 0) return;
    second_hp_--;
    second_hit_flash_timer_ = HIT_FLASH_DURATION;  // červený záblesk vždy
    if (second_hp_ <= 0) {
        second_alive_ = false;
        second_departing_ = true;
        second_depart_dir_ = (second_x_ > GAME_WIDTH / 2.0f) ? 1.0f : -1.0f;
    }
}

bool Cannon::all_dead() const {
    // Mrtvé = ani živé, ani odjíždějící
    bool main_gone = !alive_ && !departing_;
    bool second_gone = !second_alive_ && !second_departing_;
    return main_gone && second_gone;
}

void Cannon::trigger_facepalm() {
    facepalm_timer_ = FACEPALM_DURATION;
}

void Cannon::set_confused(float duration) {
    confused_timer_ = duration;
    confused_dir_ = 1.0f;
    confused_dir_timer_ = 0.0f;
}

void Cannon::update(double dt, const Formation& formation) {
    float fdt = static_cast<float>(dt);
    float half_w = CANNON_WIDTH / 2.0f;

    // Facepalm animace — krátké "sklopení" děla
    if (facepalm_timer_ > 0) {
        facepalm_timer_ -= fdt;
        // Dip dolů a zpět — sinusový profil
        float t = facepalm_timer_ / FACEPALM_DURATION;
        facepalm_offset_ = 2.0f * (1.0f - (2.0f * t - 1.0f) * (2.0f * t - 1.0f));
    } else {
        facepalm_offset_ = 0.0f;
    }

    // Blikání po zásahu — odpočet
    if (hit_flash_timer_ > 0) hit_flash_timer_ -= fdt;
    if (second_hit_flash_timer_ > 0) second_hit_flash_timer_ -= fdt;

    // Hlavní dělo — odjezd
    if (departing_) {
        x_ += depart_dir_ * DEPART_SPEED * fdt;
        if (x_ < -CANNON_WIDTH || x_ > GAME_WIDTH + CANNON_WIDTH) {
            departing_ = false;
        }
    }

    // Hlavní dělo — zmatení (motá se doleva-doprava, nestřílí)
    if (alive_ && confused_timer_ > 0) {
        confused_timer_ -= fdt;
        confused_dir_timer_ -= fdt;
        if (confused_dir_timer_ <= 0) {
            confused_dir_ = -confused_dir_;
            confused_dir_timer_ = 0.15f;
        }
        x_ += confused_dir_ * CANNON_BASE_SPEED * 1.5f * fdt;
        if (x_ - half_w < 0) x_ = half_w;
        if (x_ + half_w > GAME_WIDTH) x_ = GAME_WIDTH - half_w;
    }
    // Hlavní dělo — normální update
    else if (alive_ && ai_ && !paused_) {
        ai_->update(dt, x_, formation);
        if (x_ - half_w < 0) x_ = half_w;
        if (x_ + half_w > GAME_WIDTH) x_ = GAME_WIDTH - half_w;
        if (fire_cooldown_ > 0) fire_cooldown_ -= fdt;
    }

    // Druhé dělo — odjezd
    if (second_departing_) {
        second_x_ += second_depart_dir_ * DEPART_SPEED * fdt;
        if (second_x_ < -CANNON_WIDTH || second_x_ > GAME_WIDTH + CANNON_WIDTH) {
            second_departing_ = false;
        }
    }

    // Druhé dělo (Level 5) — normální update
    if (second_alive_ && second_ai_ && !paused_) {
        second_ai_->update(dt, second_x_, formation);
        if (second_x_ - half_w < 0) second_x_ = half_w;
        if (second_x_ + half_w > GAME_WIDTH) second_x_ = GAME_WIDTH - half_w;
        if (second_fire_cooldown_ > 0) second_fire_cooldown_ -= fdt;
    }
}

void Cannon::render(Renderer& renderer) const {
    // Hlavní dělo (i během odjezdu)
    if (alive_ || departing_) {
        // Blikání po zásahu — skrýt každý druhý frame
        bool visible = true;
        if (hit_flash_timer_ > 0) {
            visible = (static_cast<int>(hit_flash_timer_ * 12) % 2 == 0);
        }
        if (visible) {
            float draw_x = x_ - CANNON_SPRITE_W / 2.0f;
            float draw_y = CANNON_Y + facepalm_offset_;
            // Barva podle HP — progresivní zhoršení
            uint8_t r = 200, g = 200, b = 200;
            if (hit_flash_timer_ > 0) {
                r = 255; g = 100; b = 100; // červený záblesk
            } else if (max_hp_ > 1 && hp_ < max_hp_) {
                // Barva se mění s klesajícím HP: bílá → žlutá → oranžová → červená
                float ratio = static_cast<float>(hp_) / max_hp_;
                if (ratio > 0.6f) {
                    r = 200; g = 200; b = 130; // lehce žlutá
                } else if (ratio > 0.4f) {
                    r = 220; g = 170; b = 80;  // oranžová
                } else {
                    r = 220; g = 120; b = 80;  // červeno-oranžová
                }
            }
            renderer.draw_sprite(CANNON_SPRITE, CANNON_SPRITE_W, CANNON_SPRITE_H,
                                 draw_x, draw_y, r, g, b);
        }
    }

    // Druhé dělo — mírně jiná barva (načervenalá)
    if (second_alive_ || second_departing_) {
        bool visible = true;
        if (second_hit_flash_timer_ > 0) {
            visible = (static_cast<int>(second_hit_flash_timer_ * 12) % 2 == 0);
        }
        if (visible) {
            float draw_x = second_x_ - CANNON_SPRITE_W / 2.0f;
            // Druhé dělo — načervenalá + progresivní poškození
            uint8_t r = 200, g = 150, b = 150;
            if (second_hit_flash_timer_ > 0) {
                r = 255; g = 100; b = 100;
            } else if (second_max_hp_ > 1 && second_hp_ < second_max_hp_) {
                float ratio = static_cast<float>(second_hp_) / second_max_hp_;
                if (ratio > 0.6f) {
                    r = 200; g = 160; b = 120;
                } else if (ratio > 0.4f) {
                    r = 220; g = 140; b = 90;
                } else {
                    r = 220; g = 110; b = 80;
                }
            }
            renderer.draw_sprite(CANNON_SPRITE, CANNON_SPRITE_W, CANNON_SPRITE_H,
                                 draw_x, CANNON_Y, r, g, b);
        }
    }
}

void Cannon::try_fire(const Formation& formation, std::vector<Bullet>& bullets) {
    // Při pauze (AFK) nestřílet
    if (paused_) return;

    // Hlavní dělo — nestřílet při odjezdu
    if (alive_ && !departing_ && ai_ && fire_cooldown_ <= 0) {
        if (ai_->should_fire(formation)) {
            Bullet b;
            b.x = x_;
            b.y = CANNON_Y - 1;
            b.vy = -BULLET_SPEED;
            b.owner = BulletOwner::CANNON;
            b.active = true;
            bullets.push_back(b);
            fire_cooldown_ = ai_->fire_interval();
        }
    }

    // Druhé dělo — nestřílet při odjezdu
    if (second_alive_ && !second_departing_ && second_ai_ && second_fire_cooldown_ <= 0) {
        if (second_ai_->should_fire(formation)) {
            Bullet b;
            b.x = second_x_;
            b.y = CANNON_Y - 1;
            b.vy = -BULLET_SPEED;
            b.owner = BulletOwner::CANNON;
            b.active = true;
            bullets.push_back(b);
            second_fire_cooldown_ = second_ai_->fire_interval();
        }
    }
}
