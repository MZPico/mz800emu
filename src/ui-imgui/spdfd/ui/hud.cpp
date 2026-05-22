#include "hud.h"
#include "ui-imgui/spdfd/renderer.h"
#include <cstring>
#include <cstdio>

// Minimalistický 5x5 pixelový font pro základní ASCII znaky (A-Z, 0-9, mezera, interpunkce).
// Každý znak je 5 řádků po 5 bitech, uložených v uint8_t (spodních 5 bitů).
struct FontGlyph {
    uint8_t rows[5];
};

// Font data — pokrývá ASCII 32-95 (mezera až '_')
static constexpr FontGlyph FONT[] = {
    // 32 ' ' (mezera)
    {{0b00000, 0b00000, 0b00000, 0b00000, 0b00000}},
    // 33 '!'
    {{0b00100, 0b00100, 0b00100, 0b00000, 0b00100}},
    // 34 '"'
    {{0b01010, 0b01010, 0b00000, 0b00000, 0b00000}},
    // 35 '#'
    {{0b01010, 0b11111, 0b01010, 0b11111, 0b01010}},
    // 36 '$'
    {{0b01110, 0b10100, 0b01110, 0b00101, 0b01110}},
    // 37 '%'
    {{0b10001, 0b00010, 0b00100, 0b01000, 0b10001}},
    // 38 '&'
    {{0b01100, 0b10010, 0b01100, 0b10010, 0b01101}},
    // 39 '''
    {{0b00100, 0b00100, 0b00000, 0b00000, 0b00000}},
    // 40 '('
    {{0b00010, 0b00100, 0b00100, 0b00100, 0b00010}},
    // 41 ')'
    {{0b01000, 0b00100, 0b00100, 0b00100, 0b01000}},
    // 42 '*'
    {{0b00000, 0b01010, 0b00100, 0b01010, 0b00000}},
    // 43 '+'
    {{0b00000, 0b00100, 0b01110, 0b00100, 0b00000}},
    // 44 ','
    {{0b00000, 0b00000, 0b00000, 0b00100, 0b01000}},
    // 45 '-'
    {{0b00000, 0b00000, 0b01110, 0b00000, 0b00000}},
    // 46 '.'
    {{0b00000, 0b00000, 0b00000, 0b00000, 0b00100}},
    // 47 '/'
    {{0b00001, 0b00010, 0b00100, 0b01000, 0b10000}},
    // 48 '0'
    {{0b01110, 0b10011, 0b10101, 0b11001, 0b01110}},
    // 49 '1'
    {{0b00100, 0b01100, 0b00100, 0b00100, 0b01110}},
    // 50 '2'
    {{0b01110, 0b10001, 0b00110, 0b01000, 0b11111}},
    // 51 '3'
    {{0b01110, 0b10001, 0b00110, 0b10001, 0b01110}},
    // 52 '4'
    {{0b10010, 0b10010, 0b11111, 0b00010, 0b00010}},
    // 53 '5'
    {{0b11111, 0b10000, 0b11110, 0b00001, 0b11110}},
    // 54 '6'
    {{0b01110, 0b10000, 0b11110, 0b10001, 0b01110}},
    // 55 '7'
    {{0b11111, 0b00010, 0b00100, 0b01000, 0b01000}},
    // 56 '8'
    {{0b01110, 0b10001, 0b01110, 0b10001, 0b01110}},
    // 57 '9'
    {{0b01110, 0b10001, 0b01111, 0b00001, 0b01110}},
    // 58 ':'
    {{0b00000, 0b00100, 0b00000, 0b00100, 0b00000}},
    // 59 ';'
    {{0b00000, 0b00100, 0b00000, 0b00100, 0b01000}},
    // 60 '<'
    {{0b00010, 0b00100, 0b01000, 0b00100, 0b00010}},
    // 61 '='
    {{0b00000, 0b01110, 0b00000, 0b01110, 0b00000}},
    // 62 '>'
    {{0b01000, 0b00100, 0b00010, 0b00100, 0b01000}},
    // 63 '?'
    {{0b01110, 0b10001, 0b00110, 0b00000, 0b00100}},
    // 64 '@'
    {{0b01110, 0b10001, 0b10111, 0b10000, 0b01110}},
    // 65 'A'
    {{0b01110, 0b10001, 0b11111, 0b10001, 0b10001}},
    // 66 'B'
    {{0b11110, 0b10001, 0b11110, 0b10001, 0b11110}},
    // 67 'C'
    {{0b01110, 0b10001, 0b10000, 0b10001, 0b01110}},
    // 68 'D'
    {{0b11110, 0b10001, 0b10001, 0b10001, 0b11110}},
    // 69 'E'
    {{0b11111, 0b10000, 0b11110, 0b10000, 0b11111}},
    // 70 'F'
    {{0b11111, 0b10000, 0b11110, 0b10000, 0b10000}},
    // 71 'G'
    {{0b01110, 0b10000, 0b10011, 0b10001, 0b01110}},
    // 72 'H'
    {{0b10001, 0b10001, 0b11111, 0b10001, 0b10001}},
    // 73 'I'
    {{0b01110, 0b00100, 0b00100, 0b00100, 0b01110}},
    // 74 'J'
    {{0b00111, 0b00010, 0b00010, 0b10010, 0b01100}},
    // 75 'K'
    {{0b10001, 0b10010, 0b11100, 0b10010, 0b10001}},
    // 76 'L'
    {{0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
    // 77 'M'
    {{0b10001, 0b11011, 0b10101, 0b10001, 0b10001}},
    // 78 'N'
    {{0b10001, 0b11001, 0b10101, 0b10011, 0b10001}},
    // 79 'O'
    {{0b01110, 0b10001, 0b10001, 0b10001, 0b01110}},
    // 80 'P'
    {{0b11110, 0b10001, 0b11110, 0b10000, 0b10000}},
    // 81 'Q'
    {{0b01110, 0b10001, 0b10101, 0b10010, 0b01101}},
    // 82 'R'
    {{0b11110, 0b10001, 0b11110, 0b10010, 0b10001}},
    // 83 'S'
    {{0b01111, 0b10000, 0b01110, 0b00001, 0b11110}},
    // 84 'T'
    {{0b11111, 0b00100, 0b00100, 0b00100, 0b00100}},
    // 85 'U'
    {{0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    // 86 'V'
    {{0b10001, 0b10001, 0b10001, 0b01010, 0b00100}},
    // 87 'W'
    {{0b10001, 0b10001, 0b10101, 0b11011, 0b10001}},
    // 88 'X'
    {{0b10001, 0b01010, 0b00100, 0b01010, 0b10001}},
    // 89 'Y'
    {{0b10001, 0b01010, 0b00100, 0b00100, 0b00100}},
    // 90 'Z'
    {{0b11111, 0b00010, 0b00100, 0b01000, 0b11111}},
    // 91 '['
    {{0b01110, 0b01000, 0b01000, 0b01000, 0b01110}},
    // 92 '\'
    {{0b10000, 0b01000, 0b00100, 0b00010, 0b00001}},
    // 93 ']'
    {{0b01110, 0b00010, 0b00010, 0b00010, 0b01110}},
    // 94 '^'
    {{0b00100, 0b01010, 0b00000, 0b00000, 0b00000}},
    // 95 '_'
    {{0b00000, 0b00000, 0b00000, 0b00000, 0b11111}},
};

static constexpr int CHAR_WIDTH = 6;  // 5px + 1px mezera
static constexpr int CHAR_HEIGHT = 5;

static void draw_char(Renderer& renderer, char c, float x, float y,
                       uint8_t r, uint8_t g, uint8_t b) {
    // Převést na velká písmena
    if (c >= 'a' && c <= 'z') c -= 32;

    int idx = c - 32;
    if (idx < 0 || idx >= 64) return; // mimo rozsah

    const auto& glyph = FONT[idx];
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (glyph.rows[row] & (1 << (4 - col))) {
                renderer.draw_pixel(x + col, y + row, r, g, b);
            }
        }
    }
}

void Hud::draw_text(Renderer& renderer, const char* text,
                     float x, float y,
                     uint8_t r, uint8_t g, uint8_t b) {
    float cx = x;
    for (const char* p = text; *p; ++p) {
        draw_char(renderer, *p, cx, y, r, g, b);
        cx += CHAR_WIDTH;
    }
}

void Hud::draw_text_centered(Renderer& renderer, const char* text,
                               float center_x, float y,
                               uint8_t r, uint8_t g, uint8_t b) {
    int w = text_width(text);
    draw_text(renderer, text, center_x - w / 2.0f, y, r, g, b);
}

int Hud::text_width(const char* text) {
    int len = static_cast<int>(std::strlen(text));
    if (len == 0) return 0;
    return len * CHAR_WIDTH - 1; // bez mezery za posledním znakem
}

void Hud::draw_cannon_hp(Renderer& renderer, float cannon_x, int hp, int max_hp) {
    // HP tečky pod dělem — malé čtverečky 2x2px
    float total_w = max_hp * 3.0f - 1.0f; // 2px tečka + 1px mezera
    float start_x = cannon_x - total_w / 2.0f;
    float y = CANNON_Y + CANNON_HEIGHT + 2.0f;

    for (int i = 0; i < max_hp; ++i) {
        float dx = start_x + i * 3.0f;
        if (i < hp) {
            // Živé HP — barva podle poměru
            float ratio = static_cast<float>(hp) / max_hp;
            uint8_t r, g, b;
            if (ratio > 0.6f) {
                r = 50; g = 255; b = 50;   // zelená
            } else if (ratio > 0.3f) {
                r = 255; g = 200; b = 50;  // žlutá
            } else {
                r = 255; g = 60; b = 60;   // červená
            }
            renderer.fill_rect(dx, y, 2, 2, r, g, b);
        } else {
            // Ztracené HP — tmavě šedá
            renderer.fill_rect(dx, y, 2, 2, 60, 60, 60);
        }
    }
}

void Hud::draw_hud(Renderer& renderer, int level, int aliens_alive, int aliens_total) {
    char buf[64];

    // Levá strana — level
    std::snprintf(buf, sizeof(buf), "LEVEL %d", level);
    draw_text(renderer, buf, 4, 4, 255, 255, 255);

    // Pravá strana — počet vetřelců (hráč je invader — "YOUR ARMY")
    std::snprintf(buf, sizeof(buf), "YOUR ARMY %d/%d", aliens_alive, aliens_total);
    int w = text_width(buf);
    draw_text(renderer, buf, GAME_WIDTH - w - 4, 4, 100, 255, 100);
}
