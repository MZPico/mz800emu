#include <algorithm>
#include <cstdlib>
#include <cmath>
#include "gameplay_state.h"
#include "gameover_state.h"
#include "interlevel_state.h"
#include "endgame_state.h"
#include "title_state.h"
#include "ui-imgui/spdfd/renderer.h"
#include "ui-imgui/spdfd/input.h"
#include "ui-imgui/spdfd/audio.h"
#include "ui-imgui/spdfd/ui/hud.h"
#include "ui-imgui/spdfd/ui/debug_overlay.h"
#include "ui-imgui/spdfd/entities/sprites.h"
#include "ui-imgui/spdfd/constants.h"

// Texty pro speech bubbles — vetřelci komentují smrt kamaráda
static const char* BUBBLE_TEXTS[] = {
    "I HAVE KIDS!",
    "NOT AGAIN...",
    "I JUST GOT HERE!",
    "MY MORTGAGE!",
    "IS THIS OSHA?",
    "NOOO!",
    "WHY US?",
    "RUN!",
    "STEVE!",
    "OH NO...",
    "MEDIC!",
    "TELL MY WIFE...",
};
static constexpr int BUBBLE_COUNT = sizeof(BUBBLE_TEXTS) / sizeof(BUBBLE_TEXTS[0]);
// Pravděpodobnost spawnu bubliny (30%)
static constexpr float BUBBLE_CHANCE = 0.45f;

// Texty operátora děla — komentáře při zásahu od vetřelců
static const char* CANNON_OP_TEXTS[] = {
    "OUCH!",
    "HEY!",
    "NOT COOL!",
    "MY BONUS!",
    "THAT STINGS!",
    "OW OW OW!",
    "I QUIT!",
    "UNFAIR!",
    "CALL HR!",
};
static constexpr int CANNON_OP_COUNT = sizeof(CANNON_OP_TEXTS) / sizeof(CANNON_OP_TEXTS[0]);

// Intro citáty — čapkovský humor, náhodně se střídají při každém spuštění L1
struct IntroQuote {
    const char* line1;
    const char* line2;
};
static const IntroQuote INTRO_QUOTES[] = {
    { "OUR INVASION FORCES WERE ATTACKED",
      "BY A TREACHEROUSLY DEFENDING ENEMY" },
    { "THE ENEMY HAS RESORTED TO",
      "THE COWARDLY ACT OF SELF-DEFENSE" },
    { "THE LOCALS ARE DEFENDING THEMSELVES",
      "THIS WAS NOT IN THE BROCHURE" },
    { "OUR PEACEFUL INVASION WAS MET",
      "WITH UNPROVOKED RESISTANCE" },
    { "THE ENEMY REFUSES TO COOPERATE",
      "WITH THE INVASION. FILE A COMPLAINT" },
    { "LANDING ZONE IS HOSTILE",
      "WHO APPROVED THIS PLANET?" },
    { "REQUEST FOR CEASEFIRE DENIED",
      "BY BOTH SIDES APPARENTLY" },
    { "INTEL REPORT: ENEMY HAS WEAPONS",
      "THIS CHANGES EVERYTHING" },
    { "REMINDER: INVADING IS A RIGHT",
      "DEFENDING IS JUST RUDE" },
    { "HR MEMO: PLEASE STOP DYING",
      "IT AFFECTS TEAM MORALE" },
};
static constexpr int INTRO_QUOTE_COUNT = sizeof(INTRO_QUOTES) / sizeof(INTRO_QUOTES[0]);

// Statické členy shuffle bagu pro intro citáty
int GameplayState::quote_bag_[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
int GameplayState::quote_bag_index_ = 10;  // vyčerpaný → první použití zamíchá

void GameplayState::enter() {
    game_over_ = false;
    level_won_ = false;
    end_delay_ = 0.0f;
    afk_timer_ = 0.0f;
    afk_active_ = false;
    bubbles_.clear();

    // Zastavit intro melodii — gameplay má vlastní heartbeat + melodii
    g_spdfd_audio.stop_melody();
    g_spdfd_audio.set_gameplay_melody(true);

    init_level();
}

void GameplayState::init_level() {
    int rows = 5, cols = 8;
    switch (level_) {
        case 1: rows = 5; cols = 8; break;
        case 2: rows = 5; cols = 8; break;
        case 3: rows = 4; cols = 8; break;
        case 4: rows = 4; cols = 6; break;
        case 5: rows = 3; cols = 6; break;
    }

    formation_.init(rows, cols);
    cannon_.init(level_);
    bullets_.clear();

    bunkers_.resize(BUNKER_COUNT);
    float spacing = GAME_WIDTH / (BUNKER_COUNT + 1.0f);
    for (int i = 0; i < BUNKER_COUNT; ++i) {
        float bx = spacing * (i + 1) - BUNKER_WIDTH / 2.0f;
        bunkers_[i].init(bx, BUNKER_Y);
    }

    game_over_ = false;
    level_won_ = false;
    timer_ = 0.0f;
    end_delay_ = 0.0f;
    bubbles_.clear();
    debris_.clear();
    cannon_bubbles_.clear();

    // Intro citát na začátku každého levelu — shuffle bag
    intro_text_timer_ = INTRO_TEXT_DURATION;
    if (quote_bag_index_ >= INTRO_QUOTE_COUNT) {
        // Zamíchat bag (Fisher-Yates)
        for (int i = INTRO_QUOTE_COUNT - 1; i > 0; --i) {
            int j = std::rand() % (i + 1);
            std::swap(quote_bag_[i], quote_bag_[j]);
        }
        quote_bag_index_ = 0;
    }
    intro_quote_index_ = quote_bag_[quote_bag_index_++];

    // --- Level-specifické humor inicializace ---

    welcome_banner_active_ = false;
    first_kill_shock_ = false;
    shock_timer_ = 0.0f;
    steve_row_ = -1;
    steve_col_ = -1;
    steve_offset_y_ = 0.0f;
    steve_timer_ = 0.0f;
    steve_escaping_ = false;
    steve_bubble_shown_ = false;
    player_ever_acted_ = false;
    panic_offset_x_ = 0.0f;
    panic_dir_ = 1.0f;
    panic_speed_ = 0.0f;
    megafon_timer_ = 0.0f;
    megafon_shown_ = false;

    // UFO
    ufo_.active = false;
    ufo_.shot_down = false;
    ufo_.type = UfoType::NONE;
    ufo_spawn_timer_ = 0.0f;
    ufo_spawned_ = false;

    // Předstíraná smrt (Level 4)
    fakedeath_row_ = -1;
    fakedeath_col_ = -1;
    fakedeath_timer_ = 0.0f;
    fakedeath_offset_y_ = 0.0f;
    fakedeath_active_ = false;
    fakedeath_used_ = false;

    // Hrdina s korunkou (Level 4)
    hero_row_ = -1;
    hero_col_ = -1;
    if (level_ == 4) {
        // Náhodný vetřelec v poslední řadě — hrdina
        hero_row_ = rows - 1;
        hero_col_ = std::rand() % cols;
    }

    // Reakce na smrt
    grief_timer_ = 0.0f;
    grief_row_ = -1; grief_col_ = -1;

    // Osobnosti vetřelců — opožděný a helma pozpátku
    // Vybrat dva různé náhodné živé vetřelce (ne ve stejné pozici)
    laggy_row_ = 1 + std::rand() % (rows - 1);  // ne horní řada
    laggy_col_ = std::rand() % cols;
    do {
        backward_row_ = std::rand() % rows;
        backward_col_ = std::rand() % cols;
    } while (backward_row_ == laggy_row_ && backward_col_ == laggy_col_);
    formation_.alien_at(backward_row_, backward_col_).flipped = true;
    prev_formation_x_ = formation_.x();

    // Level 2: AI zmatené po zničení bunkru
    bunker_lost_shown_ = false;

    // Náhodná událost z vesmíru — inicializace
    init_random_event();

    // Level 1: Welcome banner — prostřední vetřelec ve spodní řadě
    if (level_ == 1) {
        welcome_banner_active_ = true;
        welcome_alien_row_ = rows - 1;
        welcome_alien_col_ = cols / 2;
    }

    // Level 2: Steve dezertér — náhodný vetřelec ve střední řadě
    if (level_ == 2) {
        steve_row_ = rows / 2;
        steve_col_ = std::rand() % cols;
    }
}

void GameplayState::update(double dt, const Input& input) {
    if (input.f1_pressed()) {
        DebugOverlay::toggle();
    }

    float fdt = static_cast<float>(dt);
    timer_ += fdt;

    // Update bubbles i během end delay — ať doběhnou
    update_bubbles(fdt);

    // Update cannon operátor bubbles
    for (auto& cb : cannon_bubbles_) cb.lifetime += fdt;
    cannon_bubbles_.erase(
        std::remove_if(cannon_bubbles_.begin(), cannon_bubbles_.end(),
                        [](const CannonBubble& b) { return !b.active(); }),
        cannon_bubbles_.end());

    // Intro text odpočet
    if (intro_text_timer_ > 0) intro_text_timer_ -= fdt;

    if (game_over_ || level_won_) {
        end_delay_ += fdt;
        return;
    }

    // ESC — návrat na title screen
    if (input.escape_pressed()) {
        esc_quit_ = true;
        return;
    }

    // Sledování, jestli hráč někdy něco udělal (idle detection)
    if (input.left() || input.right() || input.fire_pressed()) {
        player_ever_acted_ = true;
    }

    // Level 1: šok po prvním zásahu — formace se na chvíli zastaví
    if (first_kill_shock_) {
        shock_timer_ += fdt;
        if (shock_timer_ >= SHOCK_DURATION) {
            first_kill_shock_ = false;
        }
        // Během šoku se formace nehýbe, ale ostatní systémy běží
        // (aby se heartbeat a zvuky nepozastavily)
    }

    // Level 2: Steve dezertér
    if (steve_row_ >= 0 && steve_col_ >= 0 &&
        formation_.alien_at(steve_row_, steve_col_).alive) {
        steve_timer_ += fdt;

        // Každých ~8s Steve zkusí utéct (pomaleji — aby hráč zaregistroval)
        if (!steve_escaping_ && steve_timer_ >= 8.0f) {
            steve_escaping_ = true;
            steve_timer_ = 0.0f;
        }

        if (steve_escaping_) {
            // Steve utíká nahoru
            steve_offset_y_ -= 20.0f * fdt;

            // Po dosažení -12px ho ostatní stáhnou zpátky
            if (steve_offset_y_ <= -12.0f) {
                steve_escaping_ = false;
                steve_timer_ = 0.0f;

                // Ukázat bublinu "STEVE, NO."
                if (!steve_bubble_shown_) {
                    SpeechBubble bubble;
                    // Bublina od souseda
                    int neighbor_col = (steve_col_ > 0) ? steve_col_ - 1 : steve_col_ + 1;
                    bubble.x = formation_.alien_world_x(neighbor_col) + ALIEN_WIDTH / 2.0f;
                    bubble.y = formation_.alien_world_y(steve_row_) - 8.0f;
                    bubble.text = "STEVE, NO.";
                    bubble.lifetime = 0.0f;
                    bubbles_.push_back(bubble);
                    steve_bubble_shown_ = true;
                }
            }

            // Zpětný návrat do formace (po ukázání bubliny)
            if (!steve_escaping_ && steve_offset_y_ < 0) {
                steve_offset_y_ += 30.0f * fdt;
                if (steve_offset_y_ > 0) steve_offset_y_ = 0.0f;
            }
        } else {
            // Pomalu se vracet do formace
            if (steve_offset_y_ < 0) {
                steve_offset_y_ += 30.0f * fdt;
                if (steve_offset_y_ > 0) steve_offset_y_ = 0.0f;
            }
        }
    }

    // Level 5: panický poslední vetřelec
    if (level_ >= 4 && formation_.alive_count() == 1) {
        panic_speed_ += 50.0f * fdt; // Zrychluje
        if (panic_speed_ > 80.0f) panic_speed_ = 80.0f;
        panic_offset_x_ += panic_dir_ * panic_speed_ * fdt;
        if (panic_offset_x_ > 10.0f) { panic_dir_ = -1.0f; }
        if (panic_offset_x_ < -10.0f) { panic_dir_ = 1.0f; }
    } else {
        panic_offset_x_ = 0.0f;
        panic_speed_ = 0.0f;
    }

    // "STAY TOGETHER!" — detekce fragmentace formace (Level 3+)
    if (level_ >= 3 && !megafon_shown_ && formation_.alive_count() >= 3) {
        // Najít min a max sloupec s živými vetřelci
        int min_col = formation_.cols(), max_col = -1;
        for (int r = 0; r < formation_.rows(); ++r) {
            for (int c = 0; c < formation_.cols(); ++c) {
                if (formation_.alien_at(r, c).alive) {
                    if (c < min_col) min_col = c;
                    if (c > max_col) max_col = c;
                }
            }
        }
        // Pokud je mezera > 3 sloupce bez živých vetřelců uprostřed
        if (max_col - min_col >= 4) {
            bool has_gap = false;
            for (int c = min_col + 1; c < max_col; ++c) {
                bool col_alive = false;
                for (int r = 0; r < formation_.rows(); ++r) {
                    if (formation_.alien_at(r, c).alive) { col_alive = true; break; }
                }
                if (!col_alive) { has_gap = true; break; }
            }
            if (has_gap) {
                megafon_shown_ = true;
                megafon_timer_ = 2.5f;
                // Spawn bublinu uprostřed
                SpeechBubble bubble;
                bubble.x = GAME_WIDTH / 2.0f;
                bubble.y = formation_.y() + 5.0f;
                bubble.text = "STAY TOGETHER!";
                bubble.lifetime = 0.0f;
                bubbles_.push_back(bubble);
            }
        }
    }
    if (megafon_timer_ > 0) megafon_timer_ -= fdt;

    // AFK detekce — pokud hráč nic nedělá 15s
    bool player_active = input.left() || input.right() || input.fire_pressed();
    if (player_active) {
        afk_timer_ = 0.0f;
        if (afk_active_) {
            afk_active_ = false;
            cannon_.set_paused(false);
        }
    } else {
        afk_timer_ += fdt;
        if (afk_timer_ >= AFK_TIMEOUT && !afk_active_) {
            afk_active_ = true;
            cannon_.set_paused(true);
        }
    }

    // Reakce na smrt — rozpad grief offsetů zpět na nulu
    if (grief_timer_ > 0) {
        grief_timer_ -= fdt;
        if (grief_timer_ <= 0) {
            // Vyčistit všechny offsety od grifu
            for (int r = 0; r < formation_.rows(); ++r)
                for (int c = 0; c < formation_.cols(); ++c)
                    if (r != laggy_row_ || c != laggy_col_)
                        formation_.alien_at(r, c).render_offset_x = 0.0f;
        }
    }

    // Během šoku (Level 1, první zásah) se formace nehýbe
    if (!first_kill_shock_) {
        formation_.update(dt, input);
    }

    // Opožděný vetřelec — laguje za formací
    if (laggy_row_ >= 0 && laggy_col_ >= 0 &&
        formation_.alien_at(laggy_row_, laggy_col_).alive) {
        float formation_dx = formation_.x() - prev_formation_x_;
        auto& laggy = formation_.alien_at(laggy_row_, laggy_col_);
        // Offset se pomalu vrací k nule, ale při pohybu formace se zvětšuje
        laggy.render_offset_x -= formation_dx * 0.6f; // laguje 60% pohybu
        laggy.render_offset_x *= 0.92f;  // postupně dohání (exponenciální decay)
        // Omezit maximální lag
        if (laggy.render_offset_x > 4.0f) laggy.render_offset_x = 4.0f;
        if (laggy.render_offset_x < -4.0f) laggy.render_offset_x = -4.0f;
    }
    prev_formation_x_ = formation_.x();

    cannon_.update(dt, formation_);

    // Hráč střílí
    if (input.fire_pressed()) {
        Bullet b = formation_.try_fire();
        if (b.active) {
            bullets_.push_back(b);
            g_spdfd_audio.play_shoot();
        }
    }

    // AI dělo střílí — sledujeme přibývající střely
    size_t before = bullets_.size();
    cannon_.try_fire(formation_, bullets_);
    if (bullets_.size() > before) {
        g_spdfd_audio.play_cannon_shot();
    }

    for (auto& b : bullets_) {
        b.update(dt);
    }

    // UFO update
    update_ufo(fdt);

    // Level 4: předstíraná smrt — vetřelec "omdlí" a pak se zvedne
    if (level_ == 4 && !fakedeath_used_ && !fakedeath_active_ && timer_ > 8.0f) {
        // Najít náhodného živého vetřelce
        if (formation_.alive_count() > 3) {
            int attempts = 10;
            while (attempts-- > 0) {
                int r = std::rand() % formation_.rows();
                int c = std::rand() % formation_.cols();
                if (formation_.alien_at(r, c).alive &&
                    !(r == hero_row_ && c == hero_col_)) {
                    fakedeath_row_ = r;
                    fakedeath_col_ = c;
                    fakedeath_active_ = true;
                    fakedeath_used_ = true;
                    fakedeath_timer_ = 0.0f;
                    fakedeath_offset_y_ = 0.0f;
                    break;
                }
            }
        }
    }
    if (fakedeath_active_) {
        fakedeath_timer_ += fdt;
        if (fakedeath_timer_ < 0.5f) {
            // Padá dolů
            fakedeath_offset_y_ += 30.0f * fdt;
        } else if (fakedeath_timer_ < 3.0f) {
            // Leží "mrtvý" (delší — aby si hráč všiml)
        } else if (fakedeath_timer_ < 3.8f) {
            // Vstává zpět
            fakedeath_offset_y_ -= 20.0f * fdt;
            if (fakedeath_offset_y_ < 0) fakedeath_offset_y_ = 0;
        } else {
            // Hotovo — zpět v pozici
            fakedeath_active_ = false;
            fakedeath_offset_y_ = 0.0f;
            // Bublina od "mrtvého" vetřelce
            SpeechBubble bubble;
            bubble.x = formation_.alien_world_x(fakedeath_col_) + ALIEN_WIDTH / 2.0f;
            bubble.y = formation_.alien_world_y(fakedeath_row_) - 8.0f;
            bubble.text = "JUST KIDDING!";
            bubble.lifetime = 0.0f;
            bubbles_.push_back(bubble);
        }
    }

    // Detekce minutí — střela děla opustí horní okraj obrazovky
    for (const auto& b : bullets_) {
        if (b.owner == BulletOwner::CANNON && b.y < -BULLET_HEIGHT && b.active) {
            cannon_.trigger_facepalm();
            // "Ow!" — střela trefila někoho mimo záběr
            if (std::rand() % 3 == 0) {
                g_spdfd_audio.play_miss_ow();
            }
            // Úlomek z vesmíru — padá pixel shora (40% šance)
            if (std::rand() % 5 < 2) {
                Debris d;
                d.x = b.x + (std::rand() % 20) - 10;
                d.y = 0.0f;
                d.vy = 20.0f + (std::rand() % 30);
                d.active = true;
                debris_.push_back(d);
            }
            break; // Maximálně jeden facepalm za tick
        }
    }

    // Update úlomků
    update_debris(fdt);

    // Náhodná událost z vesmíru
    update_random_event(fdt);

    // Kolize
    // Zapamatovat si pozice mrtvých vetřelců pro speech bubbles
    // Nejdřív si uložíme stav "alive" před kolizí
    std::vector<bool> was_alive(formation_.rows() * formation_.cols());
    for (int r = 0; r < formation_.rows(); ++r) {
        for (int c = 0; c < formation_.cols(); ++c) {
            was_alive[r * formation_.cols() + c] = formation_.alien_at(r, c).alive;
        }
    }

    auto result = collision_.update(bullets_, formation_, cannon_, bunkers_);

    // Speech bubbles — najít nově zabité vetřelce
    if (result.alien_killed) {
        g_spdfd_audio.play_explosion();
        for (int r = 0; r < formation_.rows(); ++r) {
            for (int c = 0; c < formation_.cols(); ++c) {
                bool was = was_alive[r * formation_.cols() + c];
                bool now = formation_.alien_at(r, c).alive;
                if (was && !now) {
                    // Level 1: welcome banner vetřelec zabit → šok
                    if (welcome_banner_active_ &&
                        r == welcome_alien_row_ && c == welcome_alien_col_) {
                        welcome_banner_active_ = false;
                        first_kill_shock_ = true;
                        shock_timer_ = 0.0f;
                    }

                    // Reakce sousedů — podívají se směrem k mrtvému
                    grief_timer_ = GRIEF_DURATION;
                    grief_row_ = r; grief_col_ = c;
                    for (int nc = 0; nc < formation_.cols(); ++nc) {
                        if (nc == c) continue;
                        auto& neighbor = formation_.alien_at(r, nc);
                        if (!neighbor.alive) continue;
                        // Posun 1px směrem k mrtvému
                        float dir = (nc < c) ? 1.0f : -1.0f;
                        neighbor.render_offset_x = dir;
                    }
                    // Náhodní vetřelci v sousedních řadách se třesou
                    for (int nr = std::max(0, r - 1);
                         nr <= std::min(formation_.rows() - 1, r + 1); ++nr) {
                        if (nr == r) continue;
                        for (int nc = 0; nc < formation_.cols(); ++nc) {
                            auto& a = formation_.alien_at(nr, nc);
                            if (!a.alive) continue;
                            if (std::rand() % 3 == 0) {
                                a.render_offset_x = (std::rand() % 2 == 0)
                                    ? 1.0f : -1.0f;
                            }
                        }
                    }

                    // Tento vetřelec právě zemřel — možná spawn bubliny
                    spawn_bubble(r, c);
                }
            }
        }
    }

    if (result.cannon_hit) {
        g_spdfd_audio.play_cannon_hit();
        g_spdfd_audio.play_cannon_ouch();  // "Au!" / "Ojojoj!"
        // Bublina operátora děla — humorná reakce na zásah
        spawn_cannon_bubble(cannon_.x());
    }

    // Level 2: AI zmatené po prvním zničení bunkru
    if (level_ == 2 && !bunker_lost_shown_) {
        for (const auto& b : bunkers_) {
            if (b.destroyed()) {
                bunker_lost_shown_ = true;
                cannon_.set_confused(1.2f);
                break;
            }
        }
    }

    cleanup_bullets();

    // Heartbeat — tempo podle zbývajících vetřelců
    int alive = formation_.alive_count();
    int total = formation_.total_count();
    if (total > 0 && alive > 0) {
        float ratio = static_cast<float>(alive) / total;
        // 0.6s při plné formaci → 0.15s při posledním vetřelci
        float interval = 0.15f + 0.45f * ratio;
        g_spdfd_audio.set_heartbeat_interval(interval);
        // Melodická linka — noty mizí s ubývajícími vetřelci
        g_spdfd_audio.set_melody_density(ratio);
    }
    g_spdfd_audio.update(fdt);

    // Kontrola podmínek konce
    if (formation_.alive_count() == 0) {
        game_over_ = true;
        g_spdfd_audio.set_gameplay_melody(false);
    }
    if (cannon_.all_dead()) {
        level_won_ = true;
        g_spdfd_audio.set_gameplay_melody(false);
    }
}

void GameplayState::render(Renderer& renderer) {
    renderer.clear(0, 0, 0);

    Hud::draw_hud(renderer, level_, formation_.alive_count(), formation_.total_count());

    for (const auto& bunker : bunkers_) {
        bunker.render(renderer);
    }

    formation_.render(renderer);

    // Level 2: Steve dezertér — překreslit na posunuté pozici
    if (steve_row_ >= 0 && steve_col_ >= 0 && steve_offset_y_ != 0.0f &&
        formation_.alien_at(steve_row_, steve_col_).alive) {
        float sx = formation_.alien_world_x(steve_col_);
        float sy = formation_.alien_world_y(steve_row_) + steve_offset_y_;
        // Vymazat starou pozici (černý obdélník)
        renderer.fill_rect(formation_.alien_world_x(steve_col_),
                           formation_.alien_world_y(steve_row_),
                           ALIEN_WIDTH, ALIEN_HEIGHT, 0, 0, 0);
        // Vykreslit na nové pozici
        formation_.alien_at(steve_row_, steve_col_).render(
            renderer, sx, sy, formation_.anim_frame());
    }

    // Level 4-5: panický poslední vetřelec — prekreslit s offsetem
    if (panic_offset_x_ != 0.0f && formation_.alive_count() == 1) {
        for (int r = 0; r < formation_.rows(); ++r) {
            for (int c = 0; c < formation_.cols(); ++c) {
                if (formation_.alien_at(r, c).alive) {
                    float ax = formation_.alien_world_x(c);
                    float ay = formation_.alien_world_y(r);
                    // Vymazat starou pozici
                    renderer.fill_rect(ax, ay, ALIEN_WIDTH, ALIEN_HEIGHT, 0, 0, 0);
                    // Překreslit s panikovým offsetem
                    formation_.alien_at(r, c).render(
                        renderer, ax + panic_offset_x_, ay, formation_.anim_frame());
                }
            }
        }
    }

    // Praporek "I SURRENDER" u posledního panického vetřelce
    if (level_ >= 4 && formation_.alive_count() == 1) {
        for (int r = 0; r < formation_.rows(); ++r) {
            for (int c = 0; c < formation_.cols(); ++c) {
                if (formation_.alien_at(r, c).alive) {
                    float fx = formation_.alien_world_x(c) + panic_offset_x_ + ALIEN_WIDTH + 2;
                    float fy = formation_.alien_world_y(r) - 2.0f;
                    // Malý bílý praporek (vlající)
                    float wave = std::sin(timer_ * 8.0f) * 1.0f;
                    renderer.fill_rect(fx, fy + wave, 1, 5, 255, 255, 255); // tyčka
                    renderer.fill_rect(fx + 1, fy + wave, 3, 3, 255, 255, 255); // praporek
                    // Text
                    Hud::draw_text(renderer, "SURRENDER!", fx + 5, fy + wave,
                                   255, 255, 200);
                }
            }
        }
    }

    // Level 4: předstíraná smrt — překreslit "omdlelého" vetřelce
    if (fakedeath_active_ && fakedeath_row_ >= 0 && fakedeath_col_ >= 0 &&
        formation_.alien_at(fakedeath_row_, fakedeath_col_).alive) {
        float fx = formation_.alien_world_x(fakedeath_col_);
        float fy = formation_.alien_world_y(fakedeath_row_);
        // Vymazat starou pozici
        renderer.fill_rect(fx, fy, ALIEN_WIDTH, ALIEN_HEIGHT, 0, 0, 0);
        // Vykreslit na posunuté pozici
        formation_.alien_at(fakedeath_row_, fakedeath_col_).render(
            renderer, fx, fy + fakedeath_offset_y_, formation_.anim_frame());
        // Během ležení zobrazit "X_X" oči
        if (fakedeath_timer_ >= 0.5f && fakedeath_timer_ < 3.0f) {
            Hud::draw_text(renderer, "X_X", fx + 1, fy + fakedeath_offset_y_ - 4,
                           255, 100, 100);
        }
    }

    // Level 4: hrdina s korunkou — bliká jinak než ostatní
    if (hero_row_ >= 0 && hero_col_ >= 0 &&
        formation_.alien_at(hero_row_, hero_col_).alive) {
        float hx = formation_.alien_world_x(hero_col_);
        float hy = formation_.alien_world_y(hero_row_);
        // Malá korunka nad hlavou (3 pixely)
        renderer.draw_pixel(hx + ALIEN_WIDTH / 2 - 1, hy - 2, 255, 215, 0);
        renderer.draw_pixel(hx + ALIEN_WIDTH / 2,     hy - 3, 255, 215, 0);
        renderer.draw_pixel(hx + ALIEN_WIDTH / 2 + 1, hy - 2, 255, 215, 0);
    }

    cannon_.render(renderer);

    // HP tečky pod děly
    if (cannon_.alive() || cannon_.departing()) {
        Hud::draw_cannon_hp(renderer, cannon_.x(), cannon_.hp(), cannon_.max_hp());
    }
    if (cannon_.has_second_cannon() || cannon_.second_departing()) {
        Hud::draw_cannon_hp(renderer, cannon_.second_x(),
                            cannon_.second_hp(), cannon_.second_max_hp());
    }

    // Bubliny operátora děla
    for (const auto& cb : cannon_bubbles_) {
        float alpha = 1.0f;
        if (cb.lifetime > CannonBubble::DURATION - 0.5f) {
            alpha = (CannonBubble::DURATION - cb.lifetime) / 0.5f;
        }
        uint8_t brightness = static_cast<uint8_t>(255 * alpha);
        Hud::draw_text(renderer, cb.text.c_str(), cb.x, cb.y,
                       brightness, brightness / 2, brightness / 4);
    }

    for (const auto& bullet : bullets_) {
        bullet.render(renderer);
    }

    // UFO
    render_ufo(renderer);

    // Padající úlomky z vesmíru
    for (const auto& d : debris_) {
        if (!d.active) continue;
        renderer.draw_pixel(d.x, d.y, 180, 180, 180);
    }

    // Level 5: cedule "WILL INVADE FOR FOOD" když je formace zoufalá
    if (level_ == 5 && formation_.alive_count() > 0 &&
        formation_.alive_count() <= formation_.total_count() / 2) {
        float sign_y = formation_.y() - 4.0f;
        // Blikání cedule
        if (static_cast<int>(timer_ * 1.5f) % 2 == 0) {
            Hud::draw_text_centered(renderer, "WILL INVADE FOR FOOD",
                                    GAME_WIDTH / 2, sign_y, 255, 200, 50);
        }
    }

    // Level 5: zpívající poslední řada — noty nad živými vetřelci
    if (level_ == 5 && formation_.alive_count() > 0 &&
        formation_.alive_count() <= formation_.cols()) {
        for (int r = 0; r < formation_.rows(); ++r) {
            for (int c = 0; c < formation_.cols(); ++c) {
                if (!formation_.alien_at(r, c).alive) continue;
                float nx = formation_.alien_world_x(c) + ALIEN_WIDTH / 2.0f;
                float ny = formation_.alien_world_y(r) - 6.0f;
                // Noty se střídají a vlnkují
                float wave = std::sin(timer_ * 3.0f + c * 1.5f) * 2.0f;
                const char* note = (static_cast<int>(timer_ * 2 + c) % 2 == 0) ? "*" : "+";
                Hud::draw_text(renderer, note, nx, ny + wave, 255, 255, 150);
            }
        }
    }

    // Level 1: Welcome banner nad vetřelcem
    if (welcome_banner_active_) {
        float bx = formation_.alien_world_x(welcome_alien_col_) + ALIEN_WIDTH / 2.0f;
        float by = formation_.alien_world_y(welcome_alien_row_) - 10.0f;
        Hud::draw_text_centered(renderer, "WELCOME HOME!", bx, by, 255, 255, 100);
    }

    // Level 1: šok po prvním zásahu — blikání "!"
    if (first_kill_shock_) {
        if (static_cast<int>(shock_timer_ * 6) % 2 == 0) {
            Hud::draw_text_centered(renderer, "!!", GAME_WIDTH / 2,
                                    formation_.y() - 4, 255, 100, 100);
        }
    }

    // Level 2: Steve offset — vykreslit speciální indikátor
    if (steve_row_ >= 0 && steve_col_ >= 0 &&
        formation_.alien_at(steve_row_, steve_col_).alive &&
        steve_offset_y_ < -2.0f) {
        // Šipka zpátky dolů nad Stevem
        float sx = formation_.alien_world_x(steve_col_) + ALIEN_WIDTH / 2.0f;
        float sy = formation_.alien_world_y(steve_row_) + steve_offset_y_ - 3.0f;
        renderer.draw_pixel(sx, sy, 255, 255, 0);
        renderer.draw_pixel(sx - 1, sy + 1, 255, 255, 0);
        renderer.draw_pixel(sx + 1, sy + 1, 255, 255, 0);
    }

    // Náhodná událost z vesmíru
    render_random_event(renderer);

    // Speech bubbles
    render_bubbles(renderer);

    // AFK zpráva od děla
    if (afk_active_) {
        Hud::draw_text_centered(renderer, "...ARE YOU OK UP THERE?",
                                GAME_WIDTH / 2, CANNON_Y + 10,
                                200, 200, 100);
    }

    // Intro citát na začátku L1 — čapkovský humor
    if (intro_text_timer_ > 0) {
        float alpha = 1.0f;
        if (intro_text_timer_ < 1.5f) {
            alpha = intro_text_timer_ / 1.5f; // fade-out v posledních 1.5s
        }
        uint8_t br = static_cast<uint8_t>(255 * alpha);
        const auto& quote = INTRO_QUOTES[intro_quote_index_];
        Hud::draw_text_centered(renderer, quote.line1,
                                GAME_WIDTH / 2, GAME_HEIGHT / 2 - 4,
                                br, br / 3, br / 5);
        uint8_t sub = static_cast<uint8_t>(180 * alpha);
        Hud::draw_text_centered(renderer, quote.line2,
                                GAME_WIDTH / 2, GAME_HEIGHT / 2 + 6,
                                sub, sub / 2, sub / 5);
    }

    if (game_over_) {
        Hud::draw_text_centered(renderer, "GAME OVER", GAME_WIDTH / 2, GAME_HEIGHT / 2,
                                255, 50, 50);
    } else if (level_won_) {
        Hud::draw_text_centered(renderer, "LEVEL COMPLETE!", GAME_WIDTH / 2, GAME_HEIGHT / 2,
                                50, 255, 50);
    }

    float fps = 60.0f; // TODO: skutečné FPS
    DebugOverlay::render(fps, formation_, cannon_, static_cast<int>(bullets_.size()));
}

std::unique_ptr<GameState> GameplayState::next_state() {
    // ESC — okamžitý návrat na title screen
    if (esc_quit_) {
        g_spdfd_audio.set_gameplay_melody(false);
        return std::make_unique<TitleState>();
    }

    if (end_delay_ < END_DELAY_TIME) return nullptr;

    if (game_over_) {
        // Idle hráč nikdy nehýbal - speciální game over varianta
        return std::make_unique<GameOverState>(false, level_, !player_ever_acted_);
    }
    if (level_won_) {
        int aliens_lost = formation_.total_count() - formation_.alive_count();
        bool hero_survived = (hero_row_ >= 0 && hero_col_ >= 0 &&
                              formation_.alien_at(hero_row_, hero_col_).alive);

        if (level_ >= 5) {
            // Perfect game check — předat do endgame
            return std::make_unique<EndgameState>(aliens_lost == 0);
        }
        return std::make_unique<InterlevelState>(level_, hero_survived, aliens_lost);
    }
    return nullptr;
}

void GameplayState::cleanup_bullets() {
    bullets_.erase(
        std::remove_if(bullets_.begin(), bullets_.end(),
                        [](const Bullet& b) { return !b.active; }),
        bullets_.end());
}

// --- Speech bubbles ---

void GameplayState::spawn_bubble(int dead_row, int dead_col) {
    // Pravděpodobnost spawnu
    if ((std::rand() % 100) >= static_cast<int>(BUBBLE_CHANCE * 100)) return;

    // Najít živého souseda, který "reaguje"
    int best_r = -1, best_c = -1;
    int best_dist = 999;
    for (int r = 0; r < formation_.rows(); ++r) {
        for (int c = 0; c < formation_.cols(); ++c) {
            if (!formation_.alien_at(r, c).alive) continue;
            int dist = std::abs(r - dead_row) + std::abs(c - dead_col);
            if (dist > 0 && dist < best_dist) {
                best_dist = dist;
                best_r = r;
                best_c = c;
            }
        }
    }

    if (best_r < 0) return; // Žádný živý soused

    SpeechBubble bubble;
    bubble.x = formation_.alien_world_x(best_c) + ALIEN_WIDTH / 2.0f;
    bubble.y = formation_.alien_world_y(best_r) - 8.0f;
    bubble.text = BUBBLE_TEXTS[std::rand() % BUBBLE_COUNT];
    bubble.lifetime = 0.0f;
    bubbles_.push_back(bubble);

    // Syntetizovaný hlas — přehrát prvních pár znaků textu
    const char* txt = bubble.text.c_str();
    for (int i = 0; i < 4 && txt[i]; ++i) {
        if (txt[i] != ' ') g_spdfd_audio.play_speech_char(txt[i]);
    }
}

void GameplayState::update_bubbles(float dt) {
    for (auto& b : bubbles_) {
        b.lifetime += dt;
        // Bublina pluje mírně nahoru (pomaleji — aby hráč stíhal číst)
        b.y -= 2.0f * dt;
    }
    // Odstranit expirované
    bubbles_.erase(
        std::remove_if(bubbles_.begin(), bubbles_.end(),
                        [](const SpeechBubble& b) { return !b.active(); }),
        bubbles_.end());
}

void GameplayState::render_bubbles(Renderer& renderer) const {
    for (const auto& b : bubbles_) {
        // Fade-out — snížit jas v posledních 0.5s
        float alpha = 1.0f;
        if (b.lifetime > SpeechBubble::DURATION - 0.5f) {
            alpha = (SpeechBubble::DURATION - b.lifetime) / 0.5f;
        }
        uint8_t brightness = static_cast<uint8_t>(255 * alpha);

        // Malý trojúhelníček dolů (ukazatel na vetřelce)
        float text_w = static_cast<float>(Hud::text_width(b.text.c_str()));
        float tx = b.x - text_w / 2.0f;

        // Pozadí bubliny — tmavý obdélníček
        renderer.fill_rect(tx - 1, b.y - 1, text_w + 2, 7, 0, 0, 0, 180);

        Hud::draw_text(renderer, b.text.c_str(), tx, b.y,
                       brightness, brightness, static_cast<uint8_t>(brightness * 0.6f));
    }
}

// --- Bubliny operátora děla ---

void GameplayState::spawn_cannon_bubble(float cx) {
    // Max 1 bublina najednou — aby se nepřekrývaly
    if (!cannon_bubbles_.empty()) return;

    CannonBubble cb;
    cb.text = CANNON_OP_TEXTS[std::rand() % CANNON_OP_COUNT];
    float tw = static_cast<float>(Hud::text_width(cb.text.c_str()));
    cb.x = cx - tw / 2.0f;
    cb.y = CANNON_Y + CANNON_HEIGHT + 6.0f;  // pod dělem, pod HP tečkami
    cb.lifetime = 0.0f;
    cannon_bubbles_.push_back(cb);

    // Syntetizovaný hlas operátora
    const char* txt = cb.text.c_str();
    for (int i = 0; i < 3 && txt[i]; ++i) {
        if (txt[i] != ' ') g_spdfd_audio.play_speech_char(txt[i]);
    }
}

// --- Padající úlomky z vesmíru ---

void GameplayState::update_debris(float dt) {
    for (auto& d : debris_) {
        if (!d.active) continue;
        d.y += d.vy * dt;
        if (d.y > GAME_HEIGHT) d.active = false;
    }
    debris_.erase(
        std::remove_if(debris_.begin(), debris_.end(),
                        [](const Debris& d) { return !d.active; }),
        debris_.end());
}

// --- UFO bonus (Level 3) ---

void GameplayState::update_ufo(float dt) {
    // Spawn UFO v Level 3 po 10s
    if (level_ == 3 && !ufo_spawned_ && timer_ > 10.0f && !ufo_.active) {
        ufo_.active = true;
        ufo_spawned_ = true;
        ufo_.shot_down = false;
        ufo_.shot_timer = 0.0f;
        // Náhodný typ — repair (opraví bunkr) nebo pizza (zrychlí formaci)
        ufo_.type = (std::rand() % 2 == 0) ? UfoType::REPAIR : UfoType::PIZZA;
        // Přílet z náhodné strany
        if (std::rand() % 2 == 0) {
            ufo_.x = -UFO_SPRITE_W;
            ufo_.dir = 1.0f;
        } else {
            ufo_.x = GAME_WIDTH;
            ufo_.dir = -1.0f;
        }
        ufo_.y = 16.0f;
        g_spdfd_audio.play_ufo();
    }

    if (!ufo_.active) return;

    // Sestřelené UFO — krátká exploze a zmizení
    if (ufo_.shot_down) {
        ufo_.shot_timer += dt;
        if (ufo_.shot_timer >= 0.5f) {
            ufo_.active = false;
            // Pokud bylo pizza UFO — vetřelci zesmutní
            if (ufo_.type == UfoType::PIZZA) {
                SpeechBubble bubble;
                bubble.x = GAME_WIDTH / 2.0f;
                bubble.y = formation_.y() + 5.0f;
                bubble.text = "OUR PIZZA!";
                bubble.lifetime = 0.0f;
                bubbles_.push_back(bubble);
            }
        }
        return;
    }

    // Pohyb UFO
    ufo_.x += ufo_.dir * 35.0f * dt;

    // Kolize UFO se střelami formace (hráč ho může sestřelit)
    for (auto& b : bullets_) {
        if (!b.active || b.owner != BulletOwner::ALIEN) continue;
        if (b.x >= ufo_.x && b.x <= ufo_.x + UFO_SPRITE_W &&
            b.y >= ufo_.y && b.y <= ufo_.y + UFO_SPRITE_H) {
            // Hráč sestřelil UFO!
            ufo_.shot_down = true;
            ufo_.shot_timer = 0.0f;
            b.active = false;
            g_spdfd_audio.play_explosion();
            break;
        }
    }

    // UFO proletělo obrazovkou — efekt podle typu
    if (ufo_.x > GAME_WIDTH + UFO_SPRITE_W || ufo_.x < -UFO_SPRITE_W * 2) {
        ufo_.active = false;

        if (ufo_.type == UfoType::REPAIR) {
            // Opravit nejpoškozenější bunkr
            // (jednoduše — resetovat první bunkr)
            if (!bunkers_.empty()) {
                float spacing = GAME_WIDTH / (BUNKER_COUNT + 1.0f);
                float bx = spacing * 1 - BUNKER_WIDTH / 2.0f;
                bunkers_[0].init(bx, BUNKER_Y);
            }
            SpeechBubble bubble;
            bubble.x = GAME_WIDTH / 2.0f;
            bubble.y = BUNKER_Y - 10.0f;
            bubble.text = "REPAIR!";
            bubble.lifetime = 0.0f;
            bubbles_.push_back(bubble);
        } else if (ufo_.type == UfoType::PIZZA) {
            // Pizza dorazila — vetřelci se radují
            SpeechBubble bubble;
            bubble.x = GAME_WIDTH / 2.0f;
            bubble.y = formation_.y() + 5.0f;
            bubble.text = "PIZZA TIME!";
            bubble.lifetime = 0.0f;
            bubbles_.push_back(bubble);
        }
    }
}

void GameplayState::render_ufo(Renderer& renderer) const {
    if (!ufo_.active) return;

    if (ufo_.shot_down) {
        // Exploze
        renderer.draw_sprite(EXPLOSION_SPRITE, EXPLOSION_W, EXPLOSION_H,
                             ufo_.x, ufo_.y, 255, 200, 50);
        return;
    }

    // UFO sprite — barva podle typu
    uint8_t r, g, b;
    if (ufo_.type == UfoType::REPAIR) {
        r = 255; g = 50; b = 50;   // červená (nepřátelské)
    } else {
        r = 255; g = 200; b = 50;  // žlutá (pizza!)
    }
    renderer.draw_sprite(UFO_SPRITE, UFO_SPRITE_W, UFO_SPRITE_H,
                         ufo_.x, ufo_.y, r, g, b);

    // Popisek nad UFO
    const char* label = (ufo_.type == UfoType::REPAIR) ? "REPAIR" : "PIZZA";
    Hud::draw_text_centered(renderer, label,
                            ufo_.x + UFO_SPRITE_W / 2.0f, ufo_.y - 7.0f,
                            r, g, b);
}

// =============================================================================
// Náhodné události z vesmíru
// =============================================================================

void GameplayState::shuffle_event_bag() {
    event_bag_[0] = RandomEventType::ROCKET_CRASH;
    event_bag_[1] = RandomEventType::ROCKET_COLLISION;
    event_bag_[2] = RandomEventType::COWBOYS_AND_INDIANS;
    event_bag_[3] = RandomEventType::ROGUE_ROCKET;
    event_bag_[4] = RandomEventType::ALIEN_FALL;
    // Fisher-Yates shuffle
    for (int i = EVENT_TYPE_COUNT - 1; i > 0; --i) {
        int j = std::rand() % (i + 1);
        auto tmp = event_bag_[i];
        event_bag_[i] = event_bag_[j];
        event_bag_[j] = tmp;
    }
    event_bag_index_ = 0;
}

GameplayState::RandomEventType GameplayState::next_event_from_bag() {
    if (event_bag_index_ >= EVENT_TYPE_COUNT) {
        shuffle_event_bag();
    }
    return event_bag_[event_bag_index_++];
}

void GameplayState::schedule_next_event() {
    // Reset stavu pro novou událost
    random_event_active_ = false;
    random_event_done_ = false;
    random_event_timer_ = 0.0f;
    random_event_phase_ = 0;
    event_phase_timer_ = 0.0f;
    event_entity_count_ = 0;
    event_sound_timer_ = 0.0f;
    for (auto& e : event_entities_) e.active = false;
    for (auto& p : event_projectiles_) p.active = false;
    fall_row_ = -1; fall_col_ = -1;
    fall_y_ = 0; fall_vy_ = 0; fall_splashed_ = false;
    replacement_active_ = false; replacement_y_ = 0; replacement_x_ = 0;

    // Další událost ze shuffle bagu
    random_event_ = next_event_from_bag();
    // Pauza mezi událostmi: 6-12s
    random_event_trigger_ = 6.0f + static_cast<float>(std::rand() % 6);
    cowboys_chase_first_ = (std::rand() % 2 == 0);
    cowboys_wild_west_first_ = (std::rand() % 2 == 0);
    cowboys_phase1_dir_ = (std::rand() % 2 == 0) ? 1.0f : -1.0f;
}

void GameplayState::init_random_event() {
    // Inicializace shuffle bagu a naplánování první události
    shuffle_event_bag();
    schedule_next_event();
    // První událost přijde o trochu dříve (4-8s)
    random_event_trigger_ = 4.0f + static_cast<float>(std::rand() % 4);
}

void GameplayState::update_random_event(float dt) {
    if (random_event_ == RandomEventType::NONE) return;

    // Pokud předchozí událost skončila, naplánovat další
    if (random_event_done_) {
        schedule_next_event();
        return;
    }

    random_event_timer_ += dt;

    // Čekání na spuštění
    if (!random_event_active_) {
        if (random_event_timer_ >= random_event_trigger_) {
            random_event_active_ = true;
            random_event_timer_ = 0.0f;
            random_event_phase_ = 0;
            event_phase_timer_ = 0.0f;
        }
        return;
    }

    event_phase_timer_ += dt;
    event_sound_timer_ += dt;

    // Update projektilů
    for (auto& p : event_projectiles_) {
        if (!p.active) continue;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.x < -10 || p.x > GAME_WIDTH + 10 ||
            p.y < -10 || p.y > GAME_HEIGHT + 10) {
            p.active = false;
        }
    }

    switch (random_event_) {
        case RandomEventType::ROCKET_CRASH:     update_rocket_crash(dt); break;
        case RandomEventType::ROCKET_COLLISION:  update_rocket_collision(dt); break;
        case RandomEventType::COWBOYS_AND_INDIANS: update_cowboys_indians(dt); break;
        case RandomEventType::ROGUE_ROCKET:     update_rogue_rocket(dt); break;
        case RandomEventType::ALIEN_FALL:       update_alien_fall(dt); break;
        default: break;
    }
}

// --- Havárie raketky o bunkr ---
void GameplayState::update_rocket_crash(float dt) {
    if (random_event_phase_ == 0) {
        // Spawn raketky/talíře z vrchu obrazovky, diagonálně k bunkru
        auto& r = event_entities_[0];
        r.active = true;
        r.type = (std::rand() % 5 < 2) ? 3 : 0; // 40% šance na talíř
        int target_bunker = std::rand() % BUNKER_COUNT;
        float target_x = bunkers_[target_bunker].x() + BUNKER_WIDTH / 2.0f;
        r.x = target_x + (std::rand() % 60) - 30;
        r.y = -10.0f;
        r.vx = (target_x - r.x) * 0.5f;
        r.vy = 80.0f;
        r.flip = (r.vx < 0);
        event_entity_count_ = 1;
        random_event_phase_ = 1;
        event_phase_timer_ = 0.0f;
    }
    if (random_event_phase_ == 1) {
        auto& r = event_entities_[0];
        r.x += r.vx * dt;
        r.y += r.vy * dt;
        // Dosáhla bunkru?
        if (r.y >= BUNKER_Y - 4) {
            r.active = false;
            g_spdfd_audio.play_glass_shatter();
            g_spdfd_audio.play_explosion();
            // Poškodit bunkr — prorazit díru
            for (auto& b : bunkers_) {
                float dist = std::abs(b.x() + BUNKER_WIDTH / 2.0f - r.x);
                if (dist < BUNKER_WIDTH) {
                    b.hit(r.x, BUNKER_Y + 2, 5);
                    b.hit(r.x + 2, BUNKER_Y + 4, 4);
                    b.hit(r.x - 2, BUNKER_Y + 3, 4);
                    break;
                }
            }
            random_event_phase_ = 2;
            event_phase_timer_ = 0.0f;
        }
    }
    if (random_event_phase_ == 2 && event_phase_timer_ > 1.0f) {
        random_event_done_ = true;
    }
}

// --- Srážka dvou raketek ---
void GameplayState::update_rocket_collision(float dt) {
    if (random_event_phase_ == 0) {
        // Dvě raketky/talíře z protilehlých stran
        auto& r1 = event_entities_[0];
        r1.active = true; r1.type = (std::rand() % 5 < 2) ? 3 : 0;
        r1.x = -10.0f;
        r1.y = 30.0f + (std::rand() % 40);
        r1.vx = 60.0f; r1.vy = 5.0f;
        r1.flip = false;

        auto& r2 = event_entities_[1];
        r2.active = true; r2.type = (std::rand() % 5 < 2) ? 3 : 0;
        r2.x = GAME_WIDTH + 10.0f;
        r2.y = r1.y + (std::rand() % 10) - 5;
        r2.vx = -60.0f; r2.vy = -3.0f;
        r2.flip = true;
        event_entity_count_ = 2;
        random_event_phase_ = 1;
        event_phase_timer_ = 0.0f;
    }
    if (random_event_phase_ == 1) {
        auto& r1 = event_entities_[0];
        auto& r2 = event_entities_[1];
        r1.x += r1.vx * dt; r1.y += r1.vy * dt;
        r2.x += r2.vx * dt; r2.y += r2.vy * dt;
        // Srazily se?
        if (std::abs(r1.x - r2.x) < 10.0f) {
            r1.active = false; r2.active = false;
            g_spdfd_audio.play_glass_shatter();
            g_spdfd_audio.play_explosion();
            // Úlomky padají dolů
            float cx = (r1.x + r2.x) / 2.0f;
            float cy = (r1.y + r2.y) / 2.0f;
            for (int i = 0; i < 6 && i < MAX_EVENT_PROJECTILES; ++i) {
                auto& d = event_projectiles_[i];
                d.active = true; d.type = 1;
                d.x = cx + (std::rand() % 20) - 10;
                d.y = cy;
                d.vx = static_cast<float>((std::rand() % 60) - 30);
                d.vy = 20.0f + (std::rand() % 40);
            }
            random_event_phase_ = 2;
            event_phase_timer_ = 0.0f;
        }
        // Minuly se (timeout)?
        if (event_phase_timer_ > 5.0f) {
            r1.active = false; r2.active = false;
            random_event_done_ = true;
        }
    }
    if (random_event_phase_ == 2 && event_phase_timer_ > 1.5f) {
        random_event_done_ = true;
    }
}

// --- Kovbojové a indiáni ---
void GameplayState::update_cowboys_indians(float dt) {
    // Honička ve dvou fázích — divoký západ + rakety (nebo naopak).
    // Vždy opačný směr a prohozené role mezi fázemi.
    //
    // cowboys_wild_west_first_: true = nejdřív běžci, pak rakety
    // cowboys_chase_first_: true = indiáni honí kovboje v 1. fázi
    // cowboys_phase1_dir_: směr 1. fáze (2. fáze je opačný)
    //
    // Fáze 0: setup první honičky → 1
    // Fáze 1: animace první honičky → 2
    // Fáze 2: pauza 2s → setup druhé honičky → 3
    // Fáze 3: animace druhé honičky → done

    float run_y = BUNKER_Y - 12.0f;    // nad bunkry — dobře viditelné
    float rocket_y = 100.0f;            // bojová zóna

    // --- FÁZE 0: setup první honičky ---
    if (random_event_phase_ == 0) {
        float dir = cowboys_phase1_dir_;
        // Kdo koho honí v první fázi
        bool indians_chase = cowboys_chase_first_;

        if (cowboys_wild_west_first_) {
            // Divoký západ: 2 prchající + 3 pronásledovatelé
            float start_x = (dir > 0) ? -20.0f : GAME_WIDTH + 20.0f;
            int flee_type = indians_chase ? 2 : 1;  // kdo prchá
            int chase_type = indians_chase ? 1 : 2;  // kdo honí
            for (int i = 0; i < 2; ++i) {
                auto& e = event_entities_[i];
                e.active = true;
                e.type = flee_type;
                e.x = start_x - dir * (i * 12.0f);
                e.y = run_y;
                e.vx = dir * 50.0f;
                e.vy = 0;
                e.flip = (dir < 0);
            }
            for (int i = 0; i < 3; ++i) {
                auto& e = event_entities_[2 + i];
                e.active = true;
                e.type = chase_type;
                e.x = start_x - dir * (i * 12.0f + 55.0f);
                e.y = run_y;
                e.vx = dir * 50.0f;
                e.vy = 0;
                e.flip = (dir < 0);
            }
            event_entity_count_ = 5;
        } else {
            // Rakety: prchající + pronásledující (barevně odlišené)
            float start_x = (dir > 0) ? -20.0f : GAME_WIDTH + 20.0f;
            // type 4 = indiánská raketka, type 5 = kovbojská raketka
            int flee_type = indians_chase ? 5 : 4;  // kdo prchá
            int chase_type = indians_chase ? 4 : 5;  // kdo honí

            auto& r1 = event_entities_[0];
            r1.active = true; r1.type = flee_type;
            r1.x = start_x;
            r1.y = rocket_y;
            r1.vx = dir * 55.0f; r1.vy = 0;
            r1.flip = (dir < 0);

            auto& r2 = event_entities_[1];
            r2.active = true; r2.type = chase_type;
            r2.x = start_x - dir * 50.0f;
            r2.y = rocket_y;
            r2.vx = dir * 55.0f; r2.vy = 0;
            r2.flip = (dir < 0);
            event_entity_count_ = 2;
        }
        random_event_phase_ = 1;
        event_phase_timer_ = 0.0f;
        event_sound_timer_ = 0.0f;
    }

    // --- FÁZE 1 a 3: animace honičky (divoký západ nebo rakety) ---
    if (random_event_phase_ == 1 || random_event_phase_ == 3) {
        bool any_visible = false;

        if (event_entity_count_ >= 5) {
            // Divoký západ — 2 prchající (0-1) + 3 pronásledovatelé (2-4)
            for (int i = 0; i < 5; ++i) {
                auto& e = event_entities_[i];
                if (!e.active) continue;
                e.x += e.vx * dt;
                if (e.x > -20 && e.x < GAME_WIDTH + 20) any_visible = true;
                // Deaktivovat jen když entita přeběhla přes obrazovku (vzdaluje se)
                if ((e.vx > 0 && e.x > GAME_WIDTH + 40) ||
                    (e.vx < 0 && e.x < -40)) e.active = false;
            }

            if (event_sound_timer_ >= 0.4f) {
                event_sound_timer_ = 0.0f;
                if (std::rand() % 2 == 0) g_spdfd_audio.play_war_cry();
                if (std::rand() % 2 == 0) g_spdfd_audio.play_gunshot();

                // Pronásledovatelé (2-4) střílí dopředu
                for (int i = 2; i < 5; ++i) {
                    auto& e = event_entities_[i];
                    if (!e.active) continue;
                    if (std::rand() % 3 == 0) {
                        for (auto& p : event_projectiles_) {
                            if (p.active) continue;
                            p.active = true;
                            p.x = e.x + (e.flip ? -3.0f : 3.0f);
                            p.y = e.y + 3.0f;
                            p.vx = e.vx * 1.5f;
                            p.vy = 0;
                            p.type = (e.type == 2) ? 1 : 0; // kovboj=kulka, indián=šíp
                            break;
                        }
                    }
                }
                // Prchající (0-1) střílí za sebe
                for (int i = 0; i < 2; ++i) {
                    auto& e = event_entities_[i];
                    if (!e.active) continue;
                    if (std::rand() % 4 == 0) {
                        for (auto& p : event_projectiles_) {
                            if (p.active) continue;
                            p.active = true;
                            p.x = e.x + (e.flip ? 3.0f : -3.0f);
                            p.y = e.y + 3.0f;
                            p.vx = -e.vx * 0.8f;
                            p.vy = 0;
                            p.type = (e.type == 1) ? 0 : 1;
                            break;
                        }
                    }
                }
            }
        } else {
            // Rakety — 2 barevné raketky (type 4=indiánská, 5=kovbojská)
            for (int i = 0; i < 2; ++i) {
                auto& e = event_entities_[i];
                if (!e.active) continue;
                e.x += e.vx * dt;
                if (e.x > -20 && e.x < GAME_WIDTH + 20) any_visible = true;
                // Deaktivovat jen když raketka přeletěla přes obrazovku
                if ((e.vx > 0 && e.x > GAME_WIDTH + 30) ||
                    (e.vx < 0 && e.x < -30)) e.active = false;
            }

            if (event_sound_timer_ >= 0.5f) {
                event_sound_timer_ = 0.0f;
                if (std::rand() % 2 == 0) g_spdfd_audio.play_war_cry();
                if (std::rand() % 2 == 0) g_spdfd_audio.play_gunshot();

                // Pronásledující raketka (entity 1) střílí dopředu
                auto& r2 = event_entities_[1];
                if (r2.active) {
                    for (auto& p : event_projectiles_) {
                        if (p.active) continue;
                        p.active = true;
                        p.x = r2.x + (r2.flip ? -5.0f : 5.0f);
                        p.y = r2.y + 2.0f;
                        p.vx = r2.vx * 1.5f;
                        p.vy = 0;
                        p.type = (r2.type == 5) ? 1 : 0; // kovbojská=kulka, indiánská=šíp
                        break;
                    }
                }
                // Prchající raketka (entity 0) střílí za sebe
                auto& r1 = event_entities_[0];
                if (r1.active && std::rand() % 2 == 0) {
                    for (auto& p : event_projectiles_) {
                        if (p.active) continue;
                        p.active = true;
                        p.x = r1.x + (r1.flip ? 5.0f : -5.0f);
                        p.y = r1.y + 2.0f;
                        p.vx = -r1.vx * 0.8f;
                        p.vy = 0;
                        p.type = (r1.type == 4) ? 0 : 1;
                        break;
                    }
                }
            }
        }

        // Konec honičky
        if (!any_visible && event_phase_timer_ > 2.0f) {
            if (random_event_phase_ == 1) {
                random_event_phase_ = 2;
                event_phase_timer_ = 0.0f;
                for (auto& p : event_projectiles_) p.active = false;
            } else {
                random_event_done_ = true;
            }
        }
    }

    // --- FÁZE 2: pauza, pak setup druhé honičky (opačný směr, prohozené role) ---
    if (random_event_phase_ == 2 && event_phase_timer_ > 2.0f) {
        float dir = -cowboys_phase1_dir_;  // opačný směr
        bool indians_chase = !cowboys_chase_first_;  // prohozené role

        for (auto& e : event_entities_) e.active = false;

        if (cowboys_wild_west_first_) {
            // Druhá fáze = rakety (barevné)
            float start_x = (dir > 0) ? -20.0f : GAME_WIDTH + 20.0f;
            int flee_type = indians_chase ? 5 : 4;
            int chase_type = indians_chase ? 4 : 5;

            auto& r1 = event_entities_[0];
            r1.active = true; r1.type = flee_type;
            r1.x = start_x;
            r1.y = rocket_y;
            r1.vx = dir * 55.0f; r1.vy = 0;
            r1.flip = (dir < 0);

            auto& r2 = event_entities_[1];
            r2.active = true; r2.type = chase_type;
            r2.x = start_x - dir * 50.0f;
            r2.y = rocket_y;
            r2.vx = dir * 55.0f; r2.vy = 0;
            r2.flip = (dir < 0);
            event_entity_count_ = 2;
        } else {
            // Druhá fáze = divoký západ (2 prchající + 3 pronásledovatelé)
            float start_x = (dir > 0) ? -20.0f : GAME_WIDTH + 20.0f;
            int flee_type = indians_chase ? 2 : 1;
            int chase_type = indians_chase ? 1 : 2;
            for (int i = 0; i < 2; ++i) {
                auto& e = event_entities_[i];
                e.active = true;
                e.type = flee_type;
                e.x = start_x - dir * (i * 12.0f);
                e.y = run_y;
                e.vx = dir * 50.0f;
                e.vy = 0;
                e.flip = (dir < 0);
            }
            for (int i = 0; i < 3; ++i) {
                auto& e = event_entities_[2 + i];
                e.active = true;
                e.type = chase_type;
                e.x = start_x - dir * (i * 12.0f + 55.0f);
                e.y = run_y;
                e.vx = dir * 50.0f;
                e.vy = 0;
                e.flip = (dir < 0);
            }
            event_entity_count_ = 5;
        }
        random_event_phase_ = 3;
        event_phase_timer_ = 0.0f;
        event_sound_timer_ = 0.0f;
    }
}

// --- Cizí raketka/talíř střílí po všech ---
void GameplayState::update_rogue_rocket(float dt) {
    if (random_event_phase_ == 0) {
        auto& r = event_entities_[0];
        r.active = true; r.type = (std::rand() % 5 < 2) ? 3 : 0;
        float dir = (std::rand() % 2 == 0) ? 1.0f : -1.0f;
        r.x = (dir > 0) ? -10.0f : GAME_WIDTH + 10.0f;
        r.y = 60.0f + (std::rand() % 30);
        r.vx = dir * 25.0f;
        r.vy = 0;
        r.flip = (dir < 0);
        event_entity_count_ = 1;
        random_event_phase_ = 1;
        event_phase_timer_ = 0.0f;
        event_sound_timer_ = 0.0f;
    }

    if (random_event_phase_ == 1) {
        auto& r = event_entities_[0];
        r.x += r.vx * dt;
        // Lehké vlnění nahoru/dolů
        r.y += std::sin(event_phase_timer_ * 3.0f) * 15.0f * dt;

        // Střelba do všech směrů
        if (event_sound_timer_ >= 0.6f) {
            event_sound_timer_ = 0.0f;
            g_spdfd_audio.play_gunshot();

            for (auto& p : event_projectiles_) {
                if (p.active) continue;
                p.active = true;
                p.x = r.x;
                p.y = r.y;
                float angle = static_cast<float>(std::rand() % 360) * 3.14159f / 180.0f;
                p.vx = std::cos(angle) * 80.0f;
                p.vy = std::sin(angle) * 80.0f;
                p.type = 1;
                break;
            }

            // Nadávky od obou stran (25% šance na bublinu)
            if (std::rand() % 4 == 0) {
                static const char* ROGUE_TEXTS[] = {
                    "HEY!", "NOT COOL!", "GET OUT!",
                    "WHO IS THAT?!", "FRIENDLY FIRE!", "WTF?!"
                };
                SpeechBubble bubble;
                if (std::rand() % 2 == 0) {
                    // Bublina od vetřelce
                    bubble.x = GAME_WIDTH / 2.0f + (std::rand() % 40) - 20;
                    bubble.y = formation_.y() + 5.0f;
                } else {
                    // Bublina od děla
                    bubble.x = cannon_.x();
                    bubble.y = CANNON_Y - 10.0f;
                }
                bubble.text = ROGUE_TEXTS[std::rand() % 6];
                bubble.lifetime = 0.0f;
                bubbles_.push_back(bubble);
                // Syntetizovaný hlas
                const char* txt = bubble.text.c_str();
                for (int i = 0; i < 3 && txt[i]; ++i) {
                    if (txt[i] != ' ') g_spdfd_audio.play_speech_char(txt[i]);
                }
            }

            // Střely do bunkrů (občas trefí)
            for (auto& proj : event_projectiles_) {
                if (!proj.active) continue;
                for (auto& b : bunkers_) {
                    if (proj.x >= b.x() && proj.x <= b.x() + BUNKER_WIDTH &&
                        proj.y >= b.y() && proj.y <= b.y() + BUNKER_HEIGHT) {
                        b.hit(proj.x, proj.y, 2);
                        proj.active = false;
                        break;
                    }
                }
            }
        }

        // Raketka odletí po ~5s, nebo ji hráč může trefit
        if (event_phase_timer_ > 5.0f) {
            r.vx *= 2.0f; // zrychlí a odletí
            random_event_phase_ = 2;
            event_phase_timer_ = 0.0f;
        }

        // Kolize se střelami hráče
        for (auto& b : bullets_) {
            if (!b.active || b.owner != BulletOwner::ALIEN) continue;
            float hw = (r.type == 3) ? SAUCER_SPRITE_W : ROCKET_SPRITE_W;
            float hh = (r.type == 3) ? SAUCER_SPRITE_H : ROCKET_SPRITE_H;
            if (b.x >= r.x - 4 && b.x <= r.x + hw + 4 &&
                b.y >= r.y - 2 && b.y <= r.y + hh + 2) {
                r.active = false;
                b.active = false;
                g_spdfd_audio.play_explosion();
                g_spdfd_audio.play_glass_shatter();
                // Vetřelci se radují
                SpeechBubble bubble;
                bubble.x = GAME_WIDTH / 2.0f;
                bubble.y = formation_.y() + 5.0f;
                bubble.text = "GOT HIM!";
                bubble.lifetime = 0.0f;
                bubbles_.push_back(bubble);
                random_event_phase_ = 2;
                event_phase_timer_ = 0.0f;
                break;
            }
        }

        if (!r.active) {
            random_event_phase_ = 2;
            event_phase_timer_ = 0.0f;
        }
    }

    if (random_event_phase_ == 2) {
        auto& r = event_entities_[0];
        if (r.active) {
            r.x += r.vx * dt;
            if (r.x < -20 || r.x > GAME_WIDTH + 20) r.active = false;
        }
        if (event_phase_timer_ > 1.5f) {
            random_event_done_ = true;
        }
    }
}

// --- Vetřelec spadne dolů, za chvíli přiletí záskok ---
void GameplayState::update_alien_fall(float dt) {
    // Fáze 0: vybrat živého vetřelce a "odpojit" ho
    if (random_event_phase_ == 0) {
        // Najít náhodného živého vetřelce
        if (formation_.alive_count() <= 2) {
            random_event_done_ = true;
            return;
        }
        int attempts = 20;
        while (attempts-- > 0) {
            int r = std::rand() % formation_.rows();
            int c = std::rand() % formation_.cols();
            if (formation_.alien_at(r, c).alive) {
                fall_row_ = r;
                fall_col_ = c;
                break;
            }
        }
        if (fall_row_ < 0) { random_event_done_ = true; return; }
        fall_y_ = 0.0f;
        fall_vy_ = 5.0f;
        fall_splashed_ = false;
        replacement_active_ = false;
        random_event_phase_ = 1;
        event_phase_timer_ = 0.0f;
    }

    // Fáze 1: vetřelec padá dolů (zrychluje — gravitace)
    if (random_event_phase_ == 1) {
        fall_vy_ += 120.0f * dt;  // gravitace
        fall_y_ += fall_vy_ * dt;
        float target_y = GAME_HEIGHT - formation_.alien_world_y(fall_row_);
        if (fall_y_ >= target_y) {
            // Dopad — řinčení skla
            fall_splashed_ = true;
            g_spdfd_audio.play_glass_shatter();
            // Zabít vetřelce (spadl a rozbil se)
            formation_.alien_at(fall_row_, fall_col_).alive = false;
            random_event_phase_ = 2;
            event_phase_timer_ = 0.0f;
        }
    }

    // Fáze 2: pauza — čekání na záskok
    if (random_event_phase_ == 2 && event_phase_timer_ > 2.0f) {
        // Záskok letí shora na pozici padlého vetřelce
        replacement_active_ = true;
        replacement_x_ = formation_.alien_world_x(fall_col_) + ALIEN_WIDTH / 2.0f;
        replacement_y_ = -10.0f;
        random_event_phase_ = 3;
        event_phase_timer_ = 0.0f;
    }

    // Fáze 3: záskok letí dolů z vesmíru na pozici
    if (random_event_phase_ == 3) {
        float target_y = formation_.alien_world_y(fall_row_);
        replacement_y_ += 60.0f * dt;
        // Aktualizovat X podle aktuální pozice formace (formace se pohybuje)
        replacement_x_ = formation_.alien_world_x(fall_col_) + ALIEN_WIDTH / 2.0f;
        if (replacement_y_ >= target_y) {
            // Záskok se zařadil do formace
            replacement_active_ = false;
            formation_.alien_at(fall_row_, fall_col_).alive = true;
            // Bublina od záskoku
            SpeechBubble bubble;
            bubble.x = formation_.alien_world_x(fall_col_) + ALIEN_WIDTH / 2.0f;
            bubble.y = formation_.alien_world_y(fall_row_) - 8.0f;
            bubble.text = "REPORTING!";
            bubble.lifetime = 0.0f;
            bubbles_.push_back(bubble);
            g_spdfd_audio.play_speech_char('R');
            g_spdfd_audio.play_speech_char('E');
            random_event_phase_ = 4;
            event_phase_timer_ = 0.0f;
        }
    }

    // Fáze 4: hotovo
    if (random_event_phase_ == 4 && event_phase_timer_ > 1.0f) {
        random_event_done_ = true;
    }
}

void GameplayState::render_random_event(Renderer& renderer) const {
    if (!random_event_active_ || random_event_done_) return;

    // Vykreslení entit
    for (int i = 0; i < MAX_EVENT_ENTITIES; ++i) {
        const auto& e = event_entities_[i];
        if (!e.active) continue;

        if (e.type == 0) {
            // Raketka — barva podle kontextu
            uint8_t r = 200, g = 200, b = 100;
            if (random_event_ == RandomEventType::ROGUE_ROCKET) {
                r = 255; g = 80; b = 80; // červená — nepřátelská
            }
            if (e.flip) {
                for (int py = 0; py < ROCKET_SPRITE_H; ++py) {
                    for (int px = 0; px < ROCKET_SPRITE_W; ++px) {
                        if (ROCKET_SPRITE[py * ROCKET_SPRITE_W + px]) {
                            renderer.draw_pixel(
                                e.x + (ROCKET_SPRITE_W - 1 - px), e.y + py,
                                r, g, b);
                        }
                    }
                }
            } else {
                renderer.draw_sprite(ROCKET_SPRITE, ROCKET_SPRITE_W,
                                     ROCKET_SPRITE_H, e.x, e.y, r, g, b);
            }
        } else if (e.type == 3) {
            // Létající talíř — stříbrný s blikajícími světly
            uint8_t r = 180, g = 200, b = 220;
            if (random_event_ == RandomEventType::ROGUE_ROCKET) {
                r = 255; g = 100; b = 100;
            }
            renderer.draw_sprite(SAUCER_SPRITE, SAUCER_SPRITE_W,
                                 SAUCER_SPRITE_H, e.x, e.y, r, g, b);
            // Blikající světla na spodku
            bool blink = (static_cast<int>(event_phase_timer_ * 6) % 2 == 0);
            if (blink) {
                renderer.draw_pixel(e.x + 2, e.y + 4, 255, 255, 100);
                renderer.draw_pixel(e.x + 6, e.y + 4, 100, 255, 255);
            } else {
                renderer.draw_pixel(e.x + 2, e.y + 4, 100, 255, 255);
                renderer.draw_pixel(e.x + 6, e.y + 4, 255, 255, 100);
            }
        } else if (e.type == 1) {
            // Indián — výrazný červeno-oranžový
            const uint8_t* sprite = INDIAN_SPRITE;
            int sw = INDIAN_SPRITE_W, sh = INDIAN_SPRITE_H;
            uint8_t r = 255, g = 140, b = 50;
            if (e.flip) {
                for (int py = 0; py < sh; ++py) {
                    for (int px = 0; px < sw; ++px) {
                        if (sprite[py * sw + px]) {
                            renderer.draw_pixel(
                                e.x + (sw - 1 - px), e.y + py, r, g, b);
                        }
                    }
                }
            } else {
                renderer.draw_sprite(sprite, sw, sh, e.x, e.y, r, g, b);
            }
        } else if (e.type == 2) {
            // Kovboj — výrazný modrý
            const uint8_t* sprite = COWBOY_SPRITE;
            int sw = COWBOY_SPRITE_W, sh = COWBOY_SPRITE_H;
            uint8_t r = 100, g = 180, b = 255;
            if (e.flip) {
                for (int py = 0; py < sh; ++py) {
                    for (int px = 0; px < sw; ++px) {
                        if (sprite[py * sw + px]) {
                            renderer.draw_pixel(
                                e.x + (sw - 1 - px), e.y + py, r, g, b);
                        }
                    }
                }
            } else {
                renderer.draw_sprite(sprite, sw, sh, e.x, e.y, r, g, b);
            }
        } else if (e.type == 4 || e.type == 5) {
            // Barevná raketka — indiánská (4, oranžová) nebo kovbojská (5, modrá)
            uint8_t r, g, b;
            if (e.type == 4) { r = 255; g = 140; b = 50; }
            else              { r = 100; g = 180; b = 255; }
            if (e.flip) {
                for (int py = 0; py < ROCKET_SPRITE_H; ++py) {
                    for (int px = 0; px < ROCKET_SPRITE_W; ++px) {
                        if (ROCKET_SPRITE[py * ROCKET_SPRITE_W + px]) {
                            renderer.draw_pixel(
                                e.x + (ROCKET_SPRITE_W - 1 - px), e.y + py,
                                r, g, b);
                        }
                    }
                }
            } else {
                renderer.draw_sprite(ROCKET_SPRITE, ROCKET_SPRITE_W,
                                     ROCKET_SPRITE_H, e.x, e.y, r, g, b);
            }
        }
    }

    // Vykreslení projektilů
    for (const auto& p : event_projectiles_) {
        if (!p.active) continue;
        if (p.type == 0) {
            // Šíp — krátká čárka
            renderer.draw_pixel(p.x, p.y, 200, 150, 50);
            renderer.draw_pixel(p.x + (p.vx > 0 ? -1 : 1), p.y, 180, 130, 40);
        } else {
            // Kulka — bílý pixel
            renderer.draw_pixel(p.x, p.y, 255, 255, 200);
        }
    }

    // Srážka raketek — exploze (úlomky padají jako bílé pixely)
    if (random_event_ == RandomEventType::ROCKET_COLLISION &&
        random_event_phase_ == 2) {
        for (const auto& p : event_projectiles_) {
            if (!p.active) continue;
            renderer.draw_pixel(p.x, p.y, 255, 200, 100);
        }
    }

    // Pád vetřelce — vykreslení padajícího a záskoku
    if (random_event_ == RandomEventType::ALIEN_FALL) {
        if (random_event_phase_ == 1 && fall_row_ >= 0 && fall_col_ >= 0) {
            // Padající vetřelec — na posunuté Y pozici
            float fx = formation_.alien_world_x(fall_col_);
            float fy = formation_.alien_world_y(fall_row_) + fall_y_;
            // Vymazat originální pozici
            renderer.fill_rect(formation_.alien_world_x(fall_col_),
                               formation_.alien_world_y(fall_row_),
                               ALIEN_WIDTH, ALIEN_HEIGHT, 0, 0, 0);
            // Vykreslení s rotací (padá → otáčí se)
            formation_.alien_at(fall_row_, fall_col_).render(
                renderer, fx, fy, formation_.anim_frame());
            // Vykřičník nad prázdným místem
            Hud::draw_text(renderer, "!?",
                           fx + ALIEN_WIDTH / 2.0f - 3, formation_.alien_world_y(fall_row_) - 5,
                           255, 255, 100);
        }
        if (replacement_active_) {
            // Záskok letí z vesmíru — zelený blikající vetřelec
            float rx = replacement_x_ - ALIEN_WIDTH / 2.0f;
            bool blink = (static_cast<int>(event_phase_timer_ * 8) % 2 == 0);
            if (blink) {
                // Jednoduchý sprite — zelený obdélníček s očima
                renderer.fill_rect(rx + 2, replacement_y_, ALIEN_WIDTH - 4,
                                   ALIEN_HEIGHT, 50, 255, 50);
                renderer.draw_pixel(rx + 4, replacement_y_ + 2, 255, 255, 255);
                renderer.draw_pixel(rx + 6, replacement_y_ + 2, 255, 255, 255);
            }
            // Plameny dole (raketový pohon)
            if (static_cast<int>(event_phase_timer_ * 12) % 2 == 0) {
                renderer.draw_pixel(rx + 4, replacement_y_ - 2, 255, 150, 50);
                renderer.draw_pixel(rx + 5, replacement_y_ - 1, 255, 200, 50);
            }
        }
    }
}
