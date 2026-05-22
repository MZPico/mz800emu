#pragma once

#include <vector>

class Formation;
class Cannon;
class Bunker;
struct Bullet;

// Kolizní systém — kontroluje zásahy střel proti všem cílům.
class CollisionSystem {
public:
    struct Result {
        bool alien_killed = false;    // byla zasažena formace?
        bool cannon_hit = false;      // bylo zasaženo dělo?
    };

    // Hlavní kontrola kolizí pro jeden frame
    Result update(std::vector<Bullet>& bullets,
                  Formation& formation,
                  Cannon& cannon,
                  std::vector<Bunker>& bunkers);

private:
    // Kontrola střel AI děla vs formace
    void check_cannon_bullets_vs_formation(std::vector<Bullet>& bullets,
                                           Formation& formation,
                                           Result& result);

    // Kontrola střel formace vs AI dělo
    void check_alien_bullets_vs_cannon(std::vector<Bullet>& bullets,
                                       Cannon& cannon,
                                       Result& result);

    // Kontrola střel vs bunkry
    void check_bullets_vs_bunkers(std::vector<Bullet>& bullets,
                                   std::vector<Bunker>& bunkers);

    // Kontrola střela vs střela (vzájemné zrušení)
    void check_bullet_vs_bullet(std::vector<Bullet>& bullets);
};
