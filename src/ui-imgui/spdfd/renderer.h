#pragma once

#include <SDL3/SDL.h>
#include "ui-imgui/spdfd/constants.h"

// Obalka nad SDL_Renderer s dual-target renderováním:
// hra se kreslí do malé textury (256x224), ta se pak scaluje na okno.
// ImGui se renderuje přímo v nativním rozlišení okna.
class Renderer {
public:
    bool init();
    void shutdown();

    // Začátek herního framu — přepne render target na retro texturu
    void begin_frame();

    // Konec herního framu — scaluje retro texturu na okno, renderuje ImGui, present
    void end_frame();

    // Kreslicí pomocníky (pracují v herních souřadnicích 256x224)
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void draw_rect(float x, float y, float w, float h,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void fill_rect(float x, float y, float w, float h,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void draw_pixel(float x, float y, uint8_t r, uint8_t g, uint8_t b);

    // Vykreslení sprite z bitmapy (pole uint8_t, 1 = pixel, 0 = prázdno)
    void draw_sprite(const uint8_t* data, int sprite_w, int sprite_h,
                     float x, float y,
                     uint8_t r, uint8_t g, uint8_t b);

    SDL_Window* window() { return window_; }
    SDL_Renderer* sdl_renderer() { return renderer_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* game_target_ = nullptr; // retro textura 256x224
};
