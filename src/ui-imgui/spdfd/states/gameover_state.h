#pragma once

#include "game_state.h"

// Stav konce hry — zobrazuje výsledek a čeká na restart.
class GameOverState : public GameState {
public:
    GameOverState(bool won, int level, bool afk_loss = false)
        : won_(won), level_(level), afk_loss_(afk_loss) {}

    void enter() override;
    void update(double dt, const Input& input) override;
    void render(Renderer& renderer) override;
    std::unique_ptr<GameState> next_state() override;

private:
    bool won_ = false;
    int level_ = 1;
    bool afk_loss_ = false;  // hráč nikdy nehýbal ani nestřílel
    bool restart_ = false;

    float blink_timer_ = 0.0f;
    bool blink_visible_ = true;
};
