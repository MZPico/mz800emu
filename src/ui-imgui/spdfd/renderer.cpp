#include "ui-imgui/spdfd/renderer.h"
#include "libs/imgui/backends/imgui_impl_sdl3.h"
#include "libs/imgui/backends/imgui_impl_sdlrenderer3.h"
#include "libs/imgui/imgui.h"

bool Renderer::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow("Space Defenders", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    // Retro textura — sem se kreslí celá hra
    game_target_ = SDL_CreateTexture(renderer_,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        GAME_WIDTH, GAME_HEIGHT);
    if (!game_target_) {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        return false;
    }

    // Nearest-neighbor filtrování pro ostrý pixel art
    SDL_SetTextureScaleMode(game_target_, SDL_SCALEMODE_NEAREST);

    // ImGui inicializace
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer3_Init(renderer_);

    return true;
}

void Renderer::shutdown() {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (game_target_) SDL_DestroyTexture(game_target_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void Renderer::begin_frame() {
    // ImGui nový frame
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Přepnout na herní texturu
    SDL_SetRenderTarget(renderer_, game_target_);
}

void Renderer::end_frame() {
    // Přepnout na okno
    SDL_SetRenderTarget(renderer_, nullptr);

    // Vyčistit okno černou
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    // Scalovat herní texturu na celé okno
    SDL_FRect dst = {0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT};
    SDL_RenderTexture(renderer_, game_target_, nullptr, &dst);

    // Renderovat ImGui overlay (v nativním rozlišení)
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);

    SDL_RenderPresent(renderer_);
}

void Renderer::clear(uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
    SDL_RenderClear(renderer_);
}

void Renderer::draw_rect(float x, float y, float w, float h,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_FRect rect = {x, y, w, h};
    SDL_RenderRect(renderer_, &rect);
}

void Renderer::fill_rect(float x, float y, float w, float h,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_FRect rect = {x, y, w, h};
    SDL_RenderFillRect(renderer_, &rect);
}

void Renderer::draw_pixel(float x, float y, uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
    SDL_RenderPoint(renderer_, x, y);
}

void Renderer::draw_sprite(const uint8_t* data, int sprite_w, int sprite_h,
                            float x, float y,
                            uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
    for (int row = 0; row < sprite_h; ++row) {
        for (int col = 0; col < sprite_w; ++col) {
            if (data[row * sprite_w + col]) {
                SDL_RenderPoint(renderer_, x + col, y + row);
            }
        }
    }
}
