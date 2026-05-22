#pragma once

#include <cstdint>

class Renderer;

// Typ vetřelce — ovlivňuje sprite a barvu
enum class AlienType {
    SQUID,    // horní řada — 30 bodů
    CRAB,     // střední řady — 20 bodů
    OCTOPUS,  // spodní řady — 10 bodů
};

// Jeden vetřelec ve formaci
struct Alien {
    AlienType type = AlienType::CRAB;
    bool alive = true;

    // Vizuální efekty
    float render_offset_x = 0.0f;  // posun při reakci na smrt souseda / opožděný
    bool flipped = false;           // helma pozpátku (zrcadlený sprite)

    // Animace exploze
    bool exploding = false;
    float explosion_timer = 0.0f;
    static constexpr float EXPLOSION_DURATION = 0.2f;

    void update(double dt);
    void render(Renderer& renderer, float world_x, float world_y, int anim_frame) const;

    // Barva podle typu
    void get_color(uint8_t& r, uint8_t& g, uint8_t& b) const;
};
