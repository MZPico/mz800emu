#pragma once

#include <SDL3/SDL.h>

// Zpracování vstupu z klávesnice.
// Uchovává stav kláves pro aktuální frame.
class Input {
public:
    // Zpracuje SDL eventy. Vrátí false pokud přišel SDL_EVENT_QUIT.
    bool poll_events();

    // Stav kláves
    bool left() const { return left_; }
    bool right() const { return right_; }
    bool fire() const { return fire_; }
    bool enter() const { return enter_; }
    bool escape() const { return escape_; }

    // Jednorázové stisky (true jen v prvním framu)
    bool fire_pressed() const { return fire_pressed_; }
    bool enter_pressed() const { return enter_pressed_; }
    bool escape_pressed() const { return escape_pressed_; }
    bool f1_pressed() const { return f1_pressed_; }

    // Zjistí, jestli hráč stiskl nějakou klávesu v tomto framu
    bool any_key_pressed() const { return any_key_pressed_; }

    // Resetovat jednorázové stisky — volat PO update smyčce
    void clear_pressed() {
        fire_pressed_ = false;
        enter_pressed_ = false;
        escape_pressed_ = false;
        f1_pressed_ = false;
        any_key_pressed_ = false;
    }

private:
    bool left_ = false;
    bool right_ = false;
    bool fire_ = false;
    bool enter_ = false;
    bool escape_ = false;

    bool fire_pressed_ = false;
    bool enter_pressed_ = false;
    bool escape_pressed_ = false;
    bool f1_pressed_ = false;
    bool any_key_pressed_ = false;
};
