#pragma once

#include <cstdint>

// Sprite data pro vetřelce — klasické Space Invaders tvary.
// Každý sprite má 2 animační framy, rozměry 11x8 pixelů.
// 1 = pixel, 0 = prázdno.

// Typ SQUID (chobotnice — horní řada)
constexpr uint8_t SQUID_FRAME1[] = {
    0,0,0,0,0,1,0,0,0,0,0,
    0,0,0,0,1,1,1,0,0,0,0,
    0,0,0,1,1,1,1,1,0,0,0,
    0,0,1,1,0,1,0,1,1,0,0,
    0,0,1,1,1,1,1,1,1,0,0,
    0,0,0,0,1,0,1,0,0,0,0,
    0,0,0,1,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,1,0,0,
};

constexpr uint8_t SQUID_FRAME2[] = {
    0,0,0,0,0,1,0,0,0,0,0,
    0,0,0,0,1,1,1,0,0,0,0,
    0,0,0,1,1,1,1,1,0,0,0,
    0,0,1,1,0,1,0,1,1,0,0,
    0,0,1,1,1,1,1,1,1,0,0,
    0,0,0,1,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,1,0,0,
    0,0,0,1,0,0,0,1,0,0,0,
};

// Typ CRAB (krab — střední řady)
constexpr uint8_t CRAB_FRAME1[] = {
    0,0,1,0,0,0,0,0,1,0,0,
    0,0,0,1,0,0,0,1,0,0,0,
    0,0,1,1,1,1,1,1,1,0,0,
    0,1,1,0,1,1,1,0,1,1,0,
    1,1,1,1,1,1,1,1,1,1,1,
    1,0,1,1,1,1,1,1,1,0,1,
    1,0,1,0,0,0,0,0,1,0,1,
    0,0,0,1,1,0,1,1,0,0,0,
};

constexpr uint8_t CRAB_FRAME2[] = {
    0,0,1,0,0,0,0,0,1,0,0,
    1,0,0,1,0,0,0,1,0,0,1,
    1,0,1,1,1,1,1,1,1,0,1,
    1,1,1,0,1,1,1,0,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,
    0,1,1,1,1,1,1,1,1,1,0,
    0,0,1,0,0,0,0,0,1,0,0,
    0,1,0,0,0,0,0,0,0,1,0,
};

// Typ OCTOPUS (chobotnice — spodní řady)
constexpr uint8_t OCTOPUS_FRAME1[] = {
    0,0,0,0,1,1,1,0,0,0,0,
    0,0,1,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,1,0,
    1,1,1,0,0,1,0,0,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,
    0,0,1,1,0,0,0,1,1,0,0,
    0,1,1,0,1,1,1,0,1,1,0,
    1,1,0,0,0,0,0,0,0,1,1,
};

constexpr uint8_t OCTOPUS_FRAME2[] = {
    0,0,0,0,1,1,1,0,0,0,0,
    0,0,1,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,1,0,
    1,1,1,0,0,1,0,0,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,
    0,0,0,1,1,0,1,1,0,0,0,
    0,0,1,1,0,1,0,1,1,0,0,
    0,1,0,0,0,0,0,0,0,1,0,
};

// AI dělo (kanon) — 15x8 pixelů
constexpr int CANNON_SPRITE_W = 15;
constexpr int CANNON_SPRITE_H = 8;

constexpr uint8_t CANNON_SPRITE[] = {
    0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

// UFO bonus — 11x5 pixelů
constexpr int UFO_SPRITE_W = 11;
constexpr int UFO_SPRITE_H = 5;

constexpr uint8_t UFO_SPRITE[] = {
    0,0,0,0,1,1,1,0,0,0,0,
    0,0,1,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,1,0,
    1,1,0,1,1,0,1,1,0,1,1,
    0,0,1,0,0,0,0,0,1,0,0,
};

// Malá raketka — 7x5 pixelů (letí doprava, zrcadlit pro opačný směr)
constexpr int ROCKET_SPRITE_W = 7;
constexpr int ROCKET_SPRITE_H = 5;
constexpr uint8_t ROCKET_SPRITE[] = {
    0,0,0,0,1,0,0,
    0,0,1,1,1,1,0,
    1,1,1,1,1,1,1,
    0,0,1,1,1,1,0,
    0,0,0,0,1,0,0,
};

// Indiánská postava — 5x7 pixelů (běží doprava)
constexpr int INDIAN_SPRITE_W = 5;
constexpr int INDIAN_SPRITE_H = 7;
constexpr uint8_t INDIAN_SPRITE[] = {
    0,0,1,0,0,  // péro
    0,1,1,0,0,  // hlava
    0,0,1,0,0,  // krk
    0,1,1,1,0,  // tělo + ruce
    0,0,1,0,0,  // pas
    0,1,0,1,0,  // nohy
    1,0,0,0,1,  // chodidla
};

// Kovbojská postava — 5x7 pixelů (běží doprava)
constexpr int COWBOY_SPRITE_W = 5;
constexpr int COWBOY_SPRITE_H = 7;
constexpr uint8_t COWBOY_SPRITE[] = {
    0,1,1,1,0,  // klobouk
    0,0,1,0,0,  // hlava
    0,0,1,0,0,  // krk
    0,1,1,1,0,  // tělo
    0,0,1,0,0,  // pas
    0,1,0,1,0,  // nohy
    1,0,0,0,1,  // chodidla
};

// Létající talíř — klasický UFO tvar, 9x5 pixelů
constexpr int SAUCER_SPRITE_W = 9;
constexpr int SAUCER_SPRITE_H = 5;
constexpr uint8_t SAUCER_SPRITE[] = {
    0,0,0,1,1,1,0,0,0,
    0,0,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,0,
    1,0,1,0,1,0,1,0,1,
    0,0,0,1,0,1,0,0,0,
};

// Exploze vetřelce — 13x8 pixelů
constexpr int EXPLOSION_W = 13;
constexpr int EXPLOSION_H = 8;

constexpr uint8_t EXPLOSION_SPRITE[] = {
    0,0,0,1,0,0,0,0,0,1,0,0,0,
    1,0,0,0,1,0,0,0,1,0,0,0,1,
    0,1,0,0,0,1,0,1,0,0,0,1,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,1,0,0,0,0,0,0,0,0,0,1,0,
    0,0,1,0,0,1,0,1,0,0,1,0,0,
    1,0,0,0,1,0,0,0,1,0,0,0,1,
    0,0,0,1,0,0,0,0,0,1,0,0,0,
};
