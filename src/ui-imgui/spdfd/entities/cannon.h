#pragma once

#include "bullet.h"
#include "ui-imgui/spdfd/constants.h"
#include <memory>
#include <vector>

class Renderer;
class Formation;
class AIController;

// AI dělo — nepřítel ovládaný počítačem.
// V Level 5 existují dvě děla.
class Cannon {
public:
    Cannon();
    ~Cannon();

    void init(int level);
    void update(double dt, const Formation& formation);
    void render(Renderer& renderer) const;

    // Pokusí se vystřelit — přidá střely do vektoru
    void try_fire(const Formation& formation, std::vector<Bullet>& bullets);

    float x() const { return x_; }
    float y() const { return CANNON_Y + facepalm_offset_; }
    bool alive() const { return alive_; }

    // Zásah — ubere HP, při 0 spustí animaci odjezdu
    void hit();
    void hit_second();

    // HP přístup — pro HUD zobrazení
    int hp() const { return hp_; }
    int max_hp() const { return max_hp_; }
    int second_hp() const { return second_hp_; }
    int second_max_hp() const { return second_max_hp_; }
    bool departing() const { return departing_; }
    bool second_departing() const { return second_departing_; }

    // Level 5 — druhé dělo
    bool has_second_cannon() const { return second_alive_; }
    float second_x() const { return second_x_; }

    // Jsou všechna děla zničená? (včetně odjíždějících)
    bool all_dead() const;

    // Facepalm — střela minula, dělo se zastydí
    void trigger_facepalm();

    // AFK — pozastavit střelbu
    void set_paused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    // Zmatení — dělo se motá doleva-doprava (L2 po zničení bunkru)
    void set_confused(float duration);
    bool is_confused() const { return confused_timer_ > 0; }

private:
    float x_ = GAME_WIDTH / 2.0f;
    bool alive_ = true;

    // HP — dělo vydrží víc zásahů na vyšších levelech
    int hp_ = 1;
    int max_hp_ = 1;

    // Blikání po zásahu — krátká nesmrtelnost
    float hit_flash_timer_ = 0.0f;
    static constexpr float HIT_FLASH_DURATION = 0.6f;

    // Animace odjezdu — dělo odjíždí z obrazovky místo exploze
    bool departing_ = false;
    float depart_dir_ = 1.0f;  // 1.0 = doprava, -1.0 = doleva
    static constexpr float DEPART_SPEED = 60.0f;

    // Druhé dělo (Level 5)
    float second_x_ = GAME_WIDTH / 2.0f;
    bool second_alive_ = false;
    bool second_departing_ = false;
    float second_depart_dir_ = -1.0f;
    int second_hp_ = 1;
    int second_max_hp_ = 1;
    float second_hit_flash_timer_ = 0.0f;
    std::unique_ptr<AIController> second_ai_;
    float second_fire_cooldown_ = 0.0f;

    // Facepalm — krátká animace "sklopení"
    float facepalm_timer_ = 0.0f;
    float facepalm_offset_ = 0.0f;
    static constexpr float FACEPALM_DURATION = 0.3f;

    // AFK pauza
    bool paused_ = false;

    // Zmatení — rychlé motání doleva-doprava
    float confused_timer_ = 0.0f;
    float confused_dir_ = 1.0f;
    float confused_dir_timer_ = 0.0f;

    std::unique_ptr<AIController> ai_;
    float fire_cooldown_ = 0.0f;
    int level_ = 1;
};
