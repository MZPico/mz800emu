#pragma once

#include "ui-imgui/spdfd/constants.h"

class Formation;

// Abstraktní rozhraní pro AI strategii děla.
// Každý level má vlastní implementaci.
class AIController {
public:
    virtual ~AIController() = default;

    // Aktualizuje pozici děla (modifikuje cannon_x)
    virtual void update(double dt, float& cannon_x, const Formation& formation) = 0;

    // Má dělo vystřelit?
    virtual bool should_fire(const Formation& formation) = 0;

    // Interval mezi výstřely
    virtual float fire_interval() const = 0;
};

// Level 1 — Rookie Gunner
// Pohybuje se náhodně, střílí v náhodných intervalech, nemíří.
class RandomAI : public AIController {
public:
    void update(double dt, float& cannon_x, const Formation& formation) override;
    bool should_fire(const Formation& formation) override;
    float fire_interval() const override { return 2.0f; }

private:
    float move_timer_ = 0.0f;
    float move_dir_ = 1.0f;
    float direction_change_ = 1.5f;
    float fire_timer_ = 0.0f;
    float next_fire_ = 1.5f;
};

// Level 2 — Trained Gunner
// Míří na nejbližšího vetřelce, pohybuje se rychleji.
class TargetingAI : public AIController {
public:
    void update(double dt, float& cannon_x, const Formation& formation) override;
    bool should_fire(const Formation& formation) override;
    float fire_interval() const override { return 1.5f; }

private:
    float fire_timer_ = 0.0f;
    float next_fire_ = 1.2f;
    static constexpr float SPEED = CANNON_BASE_SPEED * 2.0f;
};

// Level 3 — Veteran Gunner
// Predikuje pohyb formace — střílí tam, kam se formace posouvá.
class PredictiveAI : public AIController {
public:
    void update(double dt, float& cannon_x, const Formation& formation) override;
    bool should_fire(const Formation& formation) override;
    float fire_interval() const override { return 1.2f; }

private:
    float fire_timer_ = 0.0f;
    float next_fire_ = 1.0f;
    float prev_formation_x_ = 0.0f;
    float formation_vx_ = 0.0f; // odhadnutá rychlost formace
    static constexpr float SPEED = CANNON_BASE_SPEED * 2.2f;
};

// Level 4 — Ace Gunner
// Cílí na krajní vetřelce, umí double shot.
class AceAI : public AIController {
public:
    void update(double dt, float& cannon_x, const Formation& formation) override;
    bool should_fire(const Formation& formation) override;
    float fire_interval() const override { return 0.15f; } // double shot s malou mezerou

private:
    float fire_timer_ = 0.0f;
    float next_fire_ = 0.8f;
    int burst_remaining_ = 0; // pro double shot
    static constexpr float SPEED = CANNON_BASE_SPEED * 2.5f;
    static constexpr float BURST_INTERVAL = 0.12f;
    static constexpr float NORMAL_INTERVAL = 1.0f;

    // Najít X pozici krajního vetřelce
    float find_edge_alien_x(const Formation& formation);
};

// Level 5 — Commander
// Dvě děla — toto AI řídí jedno, druhé se vytvoří v Cannon.
// Chytré, rychlé, spolupracující.
class CommanderAI : public AIController {
public:
    void update(double dt, float& cannon_x, const Formation& formation) override;
    bool should_fire(const Formation& formation) override;
    float fire_interval() const override { return 0.8f; }

private:
    float fire_timer_ = 0.0f;
    float next_fire_ = 0.6f;
    float prev_formation_x_ = 0.0f;
    float formation_vx_ = 0.0f;
    bool cover_mode_ = false; // přepínání mezi krytím a útokem
    float mode_timer_ = 0.0f;
    static constexpr float SPEED = CANNON_BASE_SPEED * 2.8f;
};
