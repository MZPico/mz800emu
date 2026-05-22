#pragma once

#include "game_state.h"

// Endgame cutscéna — vetřelci dosáhli země a staví si dům.
// "They just wanted a home."
class EndgameState : public GameState {
public:
    EndgameState(bool perfect = false) : perfect_(perfect) {}

    void enter() override;
    void update(double dt, const Input& input) override;
    void render(Renderer& renderer) override;
    std::unique_ptr<GameState> next_state() override;

private:
    bool perfect_ = false;  // perfektní hra — žádná ztráta v tomto levelu
    float timer_ = 0.0f;
    bool done_ = false;

    // Fáze cutscény
    enum class Phase {
        LANDING,       // vetřelci sestupují
        BUILDING,      // staví dům (postupně se kreslí)
        TEXT,          // "They just wanted a home."
        HQ_MESSAGE,   // poslední zpráva z HQ
        WAIT,          // čekání na stisk
    };
    Phase phase_ = Phase::LANDING;
    float phase_timer_ = 0.0f;

    float formation_y_ = 0.0f;    // Y pozice formace (klesá dolů)
    int house_progress_ = 0;      // kolik řad domku je postaveno (0-8)
};
