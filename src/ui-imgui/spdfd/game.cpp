#include "ui-imgui/spdfd/game.h"
#include "ui-imgui/spdfd/audio.h"
#include "ui-imgui/spdfd/states/intro_state.h"
#include "ui-imgui/spdfd/constants.h"

bool Game::init() {
    if (!renderer_.init()) {
        return false;
    }

    // Audio — pokud selže, hra poběží bez zvuku
    if (!g_spdfd_audio.init()) {
        SDL_Log("Audio init failed, continuing without sound.");
    }

    state_ = std::make_unique<IntroState>();
    state_->enter();

    running_ = true;
    return true;
}

void Game::run() {
    double accumulator = 0.0;
    auto prev_time = SDL_GetPerformanceCounter();
    fps_timer_ = 0.0;
    frame_count_ = 0;

    while (running_) {
        auto now = SDL_GetPerformanceCounter();
        double frame_time = static_cast<double>(now - prev_time)
                          / SDL_GetPerformanceFrequency();
        prev_time = now;

        if (frame_time > 0.25) frame_time = 0.25;

        accumulator += frame_time;

        if (!input_.poll_events()) {
            running_ = false;
            break;
        }

        // ESC se řeší v jednotlivých stavech (gameplay → title, title → quit)

        bool did_update = false;
        while (accumulator >= DT) {
            did_update = true;
            if (state_) {
                state_->update(DT, input_);

                // ESC na title screenu — ukončit aplikaci
                if (state_->wants_quit()) {
                    running_ = false;
                    break;
                }

                auto next = state_->next_state();
                if (next) {
                    state_->exit();
                    state_ = std::move(next);
                    state_->enter();
                }
            }
            accumulator -= DT;
        }

        if (did_update) {
            input_.clear_pressed();
        }

        renderer_.begin_frame();
        if (state_) {
            state_->render(renderer_);
        }
        renderer_.end_frame();

        frame_count_++;
        fps_timer_ += frame_time;
        if (fps_timer_ >= 1.0) {
            fps_ = static_cast<float>(frame_count_) / static_cast<float>(fps_timer_);
            frame_count_ = 0;
            fps_timer_ = 0.0;
        }
    }
}

void Game::shutdown() {
    state_.reset();
    g_spdfd_audio.shutdown();
    renderer_.shutdown();
}
