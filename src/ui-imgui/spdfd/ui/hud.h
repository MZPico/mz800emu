#pragma once

#include <cstdint>
#include "ui-imgui/spdfd/constants.h"

class Renderer;

// Jednoduchý HUD a textové vykreslování.
// Používá vlastní pixelový font (5x5 znaků) — žádné externí soubory.
namespace Hud {

    // Vykreslí text na danou pozici (levý horní roh)
    void draw_text(Renderer& renderer, const char* text,
                   float x, float y,
                   uint8_t r, uint8_t g, uint8_t b);

    // Vykreslí text centrovaný na dané X souřadnici
    void draw_text_centered(Renderer& renderer, const char* text,
                            float center_x, float y,
                            uint8_t r, uint8_t g, uint8_t b);

    // Vykreslí herní HUD (skóre, level, počet vetřelců)
    void draw_hud(Renderer& renderer, int level, int aliens_alive, int aliens_total);

    // Vykreslí HP tečky pod dělem
    void draw_cannon_hp(Renderer& renderer, float cannon_x, int hp, int max_hp);

    // Šířka textu v pixelech
    int text_width(const char* text);

} // namespace Hud
