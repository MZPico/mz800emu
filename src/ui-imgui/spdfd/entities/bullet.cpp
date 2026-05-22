#include "bullet.h"
#include "ui-imgui/spdfd/renderer.h"

void Bullet::update(double dt) {
    if (!active) return;
    y += vy * static_cast<float>(dt);
    if (out_of_bounds()) {
        active = false;
    }
}

void Bullet::render(Renderer& renderer) const {
    if (!active) return;

    uint8_t r, g, b;
    if (owner == BulletOwner::ALIEN) {
        r = 50; g = 255; b = 50; // zelená — střela od formace
    } else {
        r = 255; g = 255; b = 255; // bílá — střela od AI děla
    }

    renderer.fill_rect(x, y, BULLET_WIDTH, BULLET_HEIGHT, r, g, b);
}
