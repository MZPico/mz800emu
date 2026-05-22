#include "collision.h"
#include "ui-imgui/spdfd/entities/formation.h"
#include "ui-imgui/spdfd/entities/cannon.h"
#include "ui-imgui/spdfd/entities/bunker.h"
#include "ui-imgui/spdfd/entities/bullet.h"
#include "ui-imgui/spdfd/constants.h"
#include <cmath>

// Jednoduchá AABB kolize
static bool aabb_overlap(float x1, float y1, float w1, float h1,
                          float x2, float y2, float w2, float h2) {
    return x1 < x2 + w2 && x1 + w1 > x2 &&
           y1 < y2 + h2 && y1 + h1 > y2;
}

CollisionSystem::Result CollisionSystem::update(
    std::vector<Bullet>& bullets,
    Formation& formation,
    Cannon& cannon,
    std::vector<Bunker>& bunkers)
{
    Result result;

    check_cannon_bullets_vs_formation(bullets, formation, result);
    check_alien_bullets_vs_cannon(bullets, cannon, result);
    check_bullets_vs_bunkers(bullets, bunkers);
    check_bullet_vs_bullet(bullets);

    return result;
}

void CollisionSystem::check_cannon_bullets_vs_formation(
    std::vector<Bullet>& bullets,
    Formation& formation,
    Result& result)
{
    for (auto& bullet : bullets) {
        if (!bullet.active || bullet.owner != BulletOwner::CANNON) continue;

        for (int row = 0; row < formation.rows(); ++row) {
            for (int col = 0; col < formation.cols(); ++col) {
                auto& alien = formation.alien_at(row, col);
                if (!alien.alive || alien.exploding) continue;

                float ax = formation.alien_world_x(col);
                float ay = formation.alien_world_y(row);

                if (aabb_overlap(bullet.x, bullet.y, BULLET_WIDTH, BULLET_HEIGHT,
                                 ax, ay, ALIEN_WIDTH, ALIEN_HEIGHT)) {
                    // Zásah!
                    alien.exploding = true;
                    alien.explosion_timer = 0.0f;
                    bullet.active = false;
                    result.alien_killed = true;
                    goto next_bullet_cannon; // jedna střela = jeden zásah
                }
            }
        }
        next_bullet_cannon:;
    }
}

void CollisionSystem::check_alien_bullets_vs_cannon(
    std::vector<Bullet>& bullets,
    Cannon& cannon,
    Result& result)
{
    float cy = cannon.y();

    for (auto& bullet : bullets) {
        if (!bullet.active || bullet.owner != BulletOwner::ALIEN) continue;

        // Hlavní dělo
        if (cannon.alive()) {
            float cx = cannon.x() - CANNON_WIDTH / 2.0f;
            if (aabb_overlap(bullet.x, bullet.y, BULLET_WIDTH, BULLET_HEIGHT,
                             cx, cy, CANNON_WIDTH, CANNON_HEIGHT)) {
                cannon.hit();
                bullet.active = false;
                result.cannon_hit = true;
                continue;
            }
        }

        // Druhé dělo (Level 5)
        if (cannon.has_second_cannon()) {
            float cx2 = cannon.second_x() - CANNON_WIDTH / 2.0f;
            if (aabb_overlap(bullet.x, bullet.y, BULLET_WIDTH, BULLET_HEIGHT,
                             cx2, cy, CANNON_WIDTH, CANNON_HEIGHT)) {
                cannon.hit_second();
                bullet.active = false;
                result.cannon_hit = true;
                continue;
            }
        }
    }
}

void CollisionSystem::check_bullets_vs_bunkers(
    std::vector<Bullet>& bullets,
    std::vector<Bunker>& bunkers)
{
    for (auto& bullet : bullets) {
        if (!bullet.active) continue;

        for (auto& bunker : bunkers) {
            // Kontrola jestli je střela v oblasti bunkru
            float bx = bunker.x();
            float by = bunker.y();

            if (aabb_overlap(bullet.x, bullet.y, BULLET_WIDTH, BULLET_HEIGHT,
                             bx, by, BUNKER_WIDTH, BUNKER_HEIGHT)) {
                if (bunker.hit(bullet.x, bullet.y, 3)) {
                    bullet.active = false;
                    break;
                }
            }
        }
    }
}

void CollisionSystem::check_bullet_vs_bullet(std::vector<Bullet>& bullets) {
    for (size_t i = 0; i < bullets.size(); ++i) {
        if (!bullets[i].active) continue;
        for (size_t j = i + 1; j < bullets.size(); ++j) {
            if (!bullets[j].active) continue;

            // Střely opačných vlastníků se mohou zrušit
            if (bullets[i].owner == bullets[j].owner) continue;

            if (aabb_overlap(bullets[i].x, bullets[i].y, BULLET_WIDTH, BULLET_HEIGHT,
                             bullets[j].x, bullets[j].y, BULLET_WIDTH, BULLET_HEIGHT)) {
                bullets[i].active = false;
                bullets[j].active = false;
            }
        }
    }
}
