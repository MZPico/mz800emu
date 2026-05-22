#include "alien.h"
#include "sprites.h"
#include "ui-imgui/spdfd/renderer.h"
#include "ui-imgui/spdfd/constants.h"

void Alien::update(double dt) {
    if (exploding) {
        explosion_timer += static_cast<float>(dt);
        if (explosion_timer >= EXPLOSION_DURATION) {
            exploding = false;
            alive = false;
        }
    }
}

void Alien::render(Renderer& renderer, float world_x, float world_y, int anim_frame) const {
    if (!alive && !exploding) return;

    if (exploding) {
        // Vykreslit explozi bíle
        renderer.draw_sprite(EXPLOSION_SPRITE, EXPLOSION_W, EXPLOSION_H,
                             world_x - 1, world_y, 255, 255, 255);
        return;
    }

    uint8_t r, g, b;
    get_color(r, g, b);

    const uint8_t* sprite = nullptr;
    switch (type) {
        case AlienType::SQUID:
            sprite = (anim_frame == 0) ? SQUID_FRAME1 : SQUID_FRAME2;
            break;
        case AlienType::CRAB:
            sprite = (anim_frame == 0) ? CRAB_FRAME1 : CRAB_FRAME2;
            break;
        case AlienType::OCTOPUS:
            sprite = (anim_frame == 0) ? OCTOPUS_FRAME1 : OCTOPUS_FRAME2;
            break;
    }

    float dx = world_x + render_offset_x;

    if (flipped) {
        // Helma pozpátku — zrcadlený sprite
        for (int py = 0; py < ALIEN_HEIGHT; ++py) {
            for (int px = 0; px < ALIEN_WIDTH; ++px) {
                if (sprite[py * ALIEN_WIDTH + px]) {
                    renderer.draw_pixel(dx + (ALIEN_WIDTH - 1 - px), world_y + py, r, g, b);
                }
            }
        }
    } else {
        renderer.draw_sprite(sprite, ALIEN_WIDTH, ALIEN_HEIGHT, dx, world_y, r, g, b);
    }
}

void Alien::get_color(uint8_t& r, uint8_t& g, uint8_t& b) const {
    switch (type) {
        case AlienType::SQUID:   r = 255; g = 50;  b = 50;  break; // červená
        case AlienType::CRAB:    r = 50;  g = 255; b = 50;  break; // zelená
        case AlienType::OCTOPUS: r = 100; g = 100; b = 255; break; // modrá
    }
}
