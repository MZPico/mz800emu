#pragma once

#include <memory>

class Renderer;
class Input;

// Abstraktní rozhraní pro herní stav.
// Hra je vždy v jednom stavu (title, gameplay, game over...).
class GameState {
public:
    virtual ~GameState() = default;

    // Volá se při vstupu do stavu
    virtual void enter() {}

    // Volá se při odchodu ze stavu
    virtual void exit() {}

    // Logický krok (fixní dt)
    virtual void update(double dt, const Input& input) = 0;

    // Vykreslení
    virtual void render(Renderer& renderer) = 0;

    // Pokud vrátí non-null, hra přepne na nový stav
    virtual std::unique_ptr<GameState> next_state() = 0;

    // Pokud vrátí true, hra se ukončí (ESC na title screenu)
    virtual bool wants_quit() const { return false; }
};
