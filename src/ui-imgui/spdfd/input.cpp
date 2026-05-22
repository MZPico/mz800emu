#include "ui-imgui/spdfd/input.h"
#include "libs/imgui/backends/imgui_impl_sdl3.h"

bool Input::poll_events() {
    // Poznámka: jednorázové _pressed flagy se neresetují tady,
    // ale až po update smyčce přes clear_pressed(),
    // aby se neztratily když frame neobsahuje logický tick.

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // ImGui potřebuje zpracovat eventy taky
        ImGui_ImplSDL3_ProcessEvent(&event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                return false;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.repeat) break;
                any_key_pressed_ = true;
                switch (event.key.key) {
                    case SDLK_LEFT:  left_ = true; break;
                    case SDLK_RIGHT: right_ = true; break;
                    case SDLK_SPACE: fire_ = true; fire_pressed_ = true; break;
                    case SDLK_RETURN: enter_ = true; enter_pressed_ = true; break;
                    case SDLK_ESCAPE: escape_ = true; escape_pressed_ = true; break;
                    case SDLK_F1: f1_pressed_ = true; break;
                    default: break;
                }
                break;

            case SDL_EVENT_KEY_UP:
                switch (event.key.key) {
                    case SDLK_LEFT:  left_ = false; break;
                    case SDLK_RIGHT: right_ = false; break;
                    case SDLK_SPACE: fire_ = false; break;
                    case SDLK_RETURN: enter_ = false; break;
                    case SDLK_ESCAPE: escape_ = false; break;
                    default: break;
                }
                break;
        }
    }

    return true;
}
