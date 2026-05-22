#pragma once

#include "game_state.h"

// Stav mezi levely — zobrazuje zprávu z AI velitelství
// a pak přechází na další level.
class InterlevelState : public GameState {
public:
    InterlevelState(int completed_level, bool hero_survived = false,
                    int aliens_lost = 0);

    void enter() override;
    void update(double dt, const Input& input) override;
    void render(Renderer& renderer) override;
    std::unique_ptr<GameState> next_state() override;

private:
    int completed_level_;
    bool hero_survived_ = false;  // Level 4 hrdina přežil
    int aliens_lost_ = 0;        // kolik vetřelců bylo ztraceno v tomto levelu
    int total_aliens_lost_ = 0;  // kumulativní ztráty (pro perfect game check)
    bool proceed_ = false;
    bool esc_quit_ = false;  // ESC → návrat na title screen

    float timer_ = 0.0f;
    float char_timer_ = 0.0f;   // pro efekt psaní po znacích
    int chars_shown_ = 0;       // kolik znaků zprávy je vidět

    // Zpráva z HQ
    const char* hq_message() const;

    static constexpr float MIN_DISPLAY_TIME = 2.0f;
    static constexpr float CHAR_SPEED = 0.03f; // sekundy na znak
};
