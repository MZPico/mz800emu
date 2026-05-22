#pragma once

#include <vector>
#include "alien.h"
#include "bullet.h"

class Renderer;
class Input;

// Formace vetřelců — hlavní hráčem ovládaná entita.
// Mřížka vetřelců, která se pohybuje jako celek.
class Formation {
public:
    Formation() = default;

    // Inicializace formace pro daný level
    void init(int rows, int cols);

    void update(double dt, const Input& input);
    void render(Renderer& renderer) const;

    // Vystřelí z formace dolů — vrátí střelu, nebo neaktivní pokud nelze
    Bullet try_fire();

    // Počet živých vetřelců
    int alive_count() const;

    // Přístup k vetřelcům pro kolize
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    Alien& alien_at(int row, int col) { return aliens_[row * cols_ + col]; }
    const Alien& alien_at(int row, int col) const { return aliens_[row * cols_ + col]; }

    // Světová pozice vetřelce (levý horní roh spritu)
    float alien_world_x(int col) const { return x_ + col * ALIEN_SPACING_X; }
    float alien_world_y(int row) const { return y_ + row * ALIEN_SPACING_Y; }

    // Pozice celé formace
    float x() const { return x_; }
    float y() const { return y_; }

    // Celkový počet vetřelců (živých + mrtvých)
    int total_count() const { return rows_ * cols_; }

    // Aktuální animační frame (pro externí vykreslení)
    int anim_frame() const { return anim_frame_; }

private:
    std::vector<Alien> aliens_;
    int rows_ = 0;
    int cols_ = 0;

    float x_ = 0.0f;  // pozice levého horního rohu formace
    float y_ = FORMATION_TOP;

    int anim_frame_ = 0;
    float anim_timer_ = 0.0f;
    static constexpr float ANIM_INTERVAL = 0.5f;

    // Výpočet aktuální rychlosti podle počtu živých vetřelců
    float current_speed() const;

    // Střelba — cooldown
    float fire_cooldown_ = 0.0f;
    static constexpr float FIRE_RATE = 0.4f;  // sekund mezi střelami
};
