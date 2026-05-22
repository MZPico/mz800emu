#pragma once

#include "ui-imgui/spdfd/constants.h"
#include <array>

class Renderer;

// Bunkr s pixelovou destrukcí.
// Každý pixel bunkru je buď aktivní (true) nebo zničený (false).
class Bunker {
public:
    void init(float x, float y);
    void render(Renderer& renderer) const;

    // Aplikuje zásah na dané místo — zničí kruhovou oblast pixelů.
    // Vrátí true pokud zásah trefil alespoň jeden aktivní pixel.
    bool hit(float hit_x, float hit_y, int radius = 3);

    float x() const { return x_; }
    float y() const { return y_; }

    // Je bunkr kompletně zničený?
    bool destroyed() const;

private:
    float x_ = 0.0f;
    float y_ = 0.0f;
    std::array<bool, BUNKER_WIDTH * BUNKER_HEIGHT> pixels_{};

    // Tvar bunkru — klasický oblouk
    void generate_shape();
};
