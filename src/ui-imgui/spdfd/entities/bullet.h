#pragma once

#include "ui-imgui/spdfd/constants.h"

class Renderer;

// Vlastník střely
enum class BulletOwner {
    ALIEN,  // střela letí dolů (od formace k dělu)
    CANNON, // střela letí nahoru (od děla k formaci)
};

// Střela — jednoduchý projektil
struct Bullet {
    float x = 0.0f;
    float y = 0.0f;
    float vy = 0.0f;   // rychlost v ose Y (kladná = dolů, záporná = nahoru)
    BulletOwner owner = BulletOwner::CANNON;
    bool active = true;

    void update(double dt);
    void render(Renderer& renderer) const;

    // Je střela mimo obrazovku?
    bool out_of_bounds() const {
        return y < 0 || y > GAME_HEIGHT;
    }
};
