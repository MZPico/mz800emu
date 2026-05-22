#include "bunker.h"
#include "ui-imgui/spdfd/renderer.h"
#include <cmath>

void Bunker::init(float x, float y) {
    x_ = x;
    y_ = y;
    generate_shape();
}

void Bunker::generate_shape() {
    // Klasický tvar bunkru — obdélník s obloukovým vrškem a výřezem dole
    for (int row = 0; row < BUNKER_HEIGHT; ++row) {
        for (int col = 0; col < BUNKER_WIDTH; ++col) {
            bool active = true;

            // Horní zaoblení — oříznout rohy
            if (row < 4) {
                int margin = 4 - row;
                if (col < margin || col >= BUNKER_WIDTH - margin) {
                    active = false;
                }
            }

            // Spodní výřez uprostřed (vchod do bunkru)
            if (row >= BUNKER_HEIGHT - 5) {
                int center = BUNKER_WIDTH / 2;
                int half_gap = 3;
                if (col >= center - half_gap && col <= center + half_gap) {
                    active = false;
                }
            }

            pixels_[row * BUNKER_WIDTH + col] = active;
        }
    }
}

void Bunker::render(Renderer& renderer) const {
    for (int row = 0; row < BUNKER_HEIGHT; ++row) {
        for (int col = 0; col < BUNKER_WIDTH; ++col) {
            if (pixels_[row * BUNKER_WIDTH + col]) {
                renderer.draw_pixel(x_ + col, y_ + row, 50, 200, 50);
            }
        }
    }
}

bool Bunker::hit(float hit_x, float hit_y, int radius) {
    // Převést na lokální souřadnice bunkru
    int local_x = static_cast<int>(hit_x - x_);
    int local_y = static_cast<int>(hit_y - y_);

    bool any_hit = false;

    // Zničit kruhovou oblast
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius) continue;

            int px = local_x + dx;
            int py = local_y + dy;

            if (px < 0 || px >= BUNKER_WIDTH || py < 0 || py >= BUNKER_HEIGHT)
                continue;

            int idx = py * BUNKER_WIDTH + px;
            if (pixels_[idx]) {
                pixels_[idx] = false;
                any_hit = true;
            }
        }
    }

    return any_hit;
}

bool Bunker::destroyed() const {
    for (bool p : pixels_) {
        if (p) return false;
    }
    return true;
}
