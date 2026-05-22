#pragma once

// Virtuální retro rozlišení (hra se renderuje sem)
constexpr int GAME_WIDTH = 256;
constexpr int GAME_HEIGHT = 224;

// Zvětšení na okno
constexpr int SCALE = 4;
constexpr int WINDOW_WIDTH = GAME_WIDTH * SCALE;
constexpr int WINDOW_HEIGHT = GAME_HEIGHT * SCALE;

// Hlavní smyčka
constexpr double TICK_RATE = 60.0;
constexpr double DT = 1.0 / TICK_RATE;

// Herní rozložení (Y souřadnice v herním prostoru)
constexpr float HUD_HEIGHT = 16.0f;
constexpr float FORMATION_TOP = 24.0f;
constexpr float BUNKER_Y = 176.0f;
constexpr float CANNON_Y = 204.0f;

// Rychlosti
constexpr float BULLET_SPEED = 120.0f;       // pixelů za sekundu
constexpr float FORMATION_BASE_SPEED = 20.0f; // základní rychlost formace
constexpr float CANNON_BASE_SPEED = 40.0f;    // základní rychlost AI děla

// Rozměry entit
constexpr int ALIEN_WIDTH = 11;
constexpr int ALIEN_HEIGHT = 8;
constexpr int ALIEN_SPACING_X = 16;  // rozestup vetřelců v mřížce
constexpr int ALIEN_SPACING_Y = 14;
constexpr int CANNON_WIDTH = 15;
constexpr int CANNON_HEIGHT = 8;
constexpr int BULLET_WIDTH = 1;
constexpr int BULLET_HEIGHT = 4;
constexpr int BUNKER_WIDTH = 24;
constexpr int BUNKER_HEIGHT = 16;
constexpr int BUNKER_COUNT = 4;
