#pragma once

#include "game_state.h"
#include "ui-imgui/spdfd/entities/formation.h"
#include "ui-imgui/spdfd/entities/cannon.h"
#include "ui-imgui/spdfd/entities/bullet.h"
#include "ui-imgui/spdfd/entities/bunker.h"
#include "ui-imgui/spdfd/systems/collision.h"
#include <vector>
#include <string>

// Řečová bublina nad vetřelcem — humor prvek
struct SpeechBubble {
    float x = 0.0f;
    float y = 0.0f;
    std::string text;
    float lifetime = 0.0f;
    static constexpr float DURATION = 3.5f;
    bool active() const { return lifetime < DURATION; }
};

// Hlavní herní stav — gameplay.
class GameplayState : public GameState {
public:
    // Výchozí konstruktor — začíná na Level 1
    GameplayState() : level_(1) {}

    // Konstruktor pro konkrétní level (volá se z InterlevelState)
    explicit GameplayState(int level) : level_(level) {}

    void enter() override;
    void update(double dt, const Input& input) override;
    void render(Renderer& renderer) override;
    std::unique_ptr<GameState> next_state() override;

private:
    Formation formation_;
    Cannon cannon_;
    std::vector<Bullet> bullets_;
    std::vector<Bunker> bunkers_;
    CollisionSystem collision_;

    int level_ = 1;
    bool game_over_ = false;    // hráč prohrál (všichni vetřelci mrtví)
    bool level_won_ = false;    // hráč vyhrál level (všechna děla zničena)
    bool esc_quit_ = false;     // ESC — návrat na title screen

    float timer_ = 0.0f;      // celkový čas v levelu (pro animace)
    float end_delay_ = 0.0f;   // krátká pauza po konci
    static constexpr float END_DELAY_TIME = 1.5f;

    // Inicializace levelu
    void init_level();

    // Odstranit neaktivní střely
    void cleanup_bullets();

    // --- Humor prvky ---

    // Speech bubbles — bubliny nad vetřelci po smrti kamaráda
    std::vector<SpeechBubble> bubbles_;
    void spawn_bubble(int dead_row, int dead_col);
    void update_bubbles(float dt);
    void render_bubbles(Renderer& renderer) const;

    // AFK detekce — dělo přestane střílet a ptá se "...are you okay up there?"
    float afk_timer_ = 0.0f;
    bool afk_active_ = false;
    static constexpr float AFK_TIMEOUT = 15.0f;

    // --- Level-specifické humor momenty ---

    // Level 1: Welcome banner — vetřelec drží transparent
    bool welcome_banner_active_ = false;
    int welcome_alien_row_ = 0;
    int welcome_alien_col_ = 0;
    bool first_kill_shock_ = false;  // krátká pauza po prvním zásahu
    float shock_timer_ = 0.0f;
    static constexpr float SHOCK_DURATION = 1.5f;

    // Reakce na smrt vetřelce — sousedé se podívají, někteří se třesou
    float grief_timer_ = 0.0f;
    static constexpr float GRIEF_DURATION = 0.4f;
    int grief_row_ = -1, grief_col_ = -1;  // kde zemřel vetřelec

    // Osobnosti vetřelců
    int laggy_row_ = -1, laggy_col_ = -1;      // opožděný vetřelec
    int backward_row_ = -1, backward_col_ = -1; // helma pozpátku
    float prev_formation_x_ = 0.0f;             // pro výpočet směru pohybu formace

    // Level 2: AI zmatené po zničení bunkru
    bool bunker_lost_shown_ = false;

    // Level 2: Steve dezertér — jeden vetřelec se pokouší utéct
    int steve_row_ = -1;
    int steve_col_ = -1;
    float steve_offset_y_ = 0.0f;    // vertikální offset (utíká nahoru)
    float steve_timer_ = 0.0f;
    bool steve_escaping_ = false;
    bool steve_bubble_shown_ = false; // "STEVE, NO." už se ukázalo

    // Sledování idle hráče - hráč nikdy nehýbal ani nestřílel
    bool player_ever_acted_ = false;

    // Intro citát na začátku L1 — čapkovský humor, shuffle bag
    float intro_text_timer_ = 0.0f;
    static constexpr float INTRO_TEXT_DURATION = 4.0f;
    int intro_quote_index_ = 0;
    static constexpr int QUOTE_BAG_SIZE = 10;
    static int quote_bag_[QUOTE_BAG_SIZE];
    static int quote_bag_index_;

    // Operátor děla — bubliny při zásahu
    struct CannonBubble {
        float x = 0.0f;
        float y = 0.0f;
        std::string text;
        float lifetime = 0.0f;
        static constexpr float DURATION = 2.5f;
        bool active() const { return lifetime < DURATION; }
    };
    std::vector<CannonBubble> cannon_bubbles_;
    void spawn_cannon_bubble(float cx);

    // Level 4-5: panický poslední vetřelec — běhá sem tam
    float panic_offset_x_ = 0.0f;
    float panic_dir_ = 1.0f;
    float panic_speed_ = 0.0f;

    // Padající úlomky z vesmíru — po minutí střely
    struct Debris {
        float x = 0.0f;
        float y = 0.0f;
        float vy = 0.0f;
        bool active = false;
    };
    std::vector<Debris> debris_;
    void update_debris(float dt);

    // "STAY TOGETHER!" — megafon když se formace rozpadne
    float megafon_timer_ = 0.0f;
    bool megafon_shown_ = false;  // aby se nezobrazoval opakovaně

    // --- UFO bonus (Level 3) ---
    enum class UfoType { NONE, REPAIR, PIZZA };
    struct Ufo {
        float x = 0.0f;
        float y = 16.0f;       // těsně pod HUD
        float dir = 1.0f;      // 1.0 = doprava, -1.0 = doleva
        UfoType type = UfoType::NONE;
        bool active = false;
        bool shot_down = false;
        float shot_timer = 0.0f;
    };
    Ufo ufo_;
    float ufo_spawn_timer_ = 0.0f;
    bool ufo_spawned_ = false;  // max 1 UFO za level
    void update_ufo(float dt);
    void render_ufo(Renderer& renderer) const;

    // --- Level 4: předstíraná smrt ---
    int fakedeath_row_ = -1;
    int fakedeath_col_ = -1;
    float fakedeath_timer_ = 0.0f;
    float fakedeath_offset_y_ = 0.0f;
    bool fakedeath_active_ = false;
    bool fakedeath_used_ = false;  // max 1x za level

    // --- Level 4: hrdina s korunkou ---
    int hero_row_ = -1;
    int hero_col_ = -1;

    // --- Náhodné události z vesmíru ---
    enum class RandomEventType {
        NONE,
        ROCKET_CRASH,           // raketka se rozfláká o bunkr
        ROCKET_COLLISION,       // dvě raketky se srazí
        COWBOYS_AND_INDIANS,    // kovbojové a indiáni + raketky
        ROGUE_ROCKET,           // cizí raketka střílí po všech
        ALIEN_FALL              // vetřelec spadne, za chvíli přiletí záskok
    };

    // Entita v události (raketka/indián/kovboj/talíř)
    struct EventEntity {
        float x = 0, y = 0;
        float vx = 0, vy = 0;
        bool active = false;
        int type = 0;     // 0=raketka, 1=indián, 2=kovboj, 3=létající talíř
        bool flip = false; // horizontální zrcadlení
    };

    // Projektil v události (šíp/kulka)
    struct EventProjectile {
        float x = 0, y = 0;
        float vx = 0, vy = 0;
        bool active = false;
        int type = 0;     // 0=šíp, 1=kulka
    };

    RandomEventType random_event_ = RandomEventType::NONE;
    float random_event_timer_ = 0.0f;
    float random_event_trigger_ = 0.0f;  // čas do dalšího spuštění
    int random_event_phase_ = 0;
    float event_phase_timer_ = 0.0f;
    bool random_event_active_ = false;
    bool random_event_done_ = false;
    bool cowboys_chase_first_ = true;    // true = indiáni honí kovboje v 1. fázi
    bool cowboys_wild_west_first_ = true; // true = divoký západ první, pak rakety
    float cowboys_phase1_dir_ = 1.0f;    // směr první honičky (pro opačný ve druhé)

    // Shuffle bag — všech 5 typů se prostřídá, než se začnou opakovat
    static constexpr int EVENT_TYPE_COUNT = 5;
    RandomEventType event_bag_[EVENT_TYPE_COUNT];
    int event_bag_index_ = 0;
    void shuffle_event_bag();
    RandomEventType next_event_from_bag();
    void schedule_next_event();

    static constexpr int MAX_EVENT_ENTITIES = 8;
    static constexpr int MAX_EVENT_PROJECTILES = 16;
    EventEntity event_entities_[MAX_EVENT_ENTITIES];
    EventProjectile event_projectiles_[MAX_EVENT_PROJECTILES];
    int event_entity_count_ = 0;
    float event_sound_timer_ = 0.0f; // pro periodické zvuky

    void init_random_event();
    void update_random_event(float dt);
    void render_random_event(Renderer& renderer) const;

    // Události — individuální update
    void update_rocket_crash(float dt);
    void update_rocket_collision(float dt);
    void update_cowboys_indians(float dt);
    void update_rogue_rocket(float dt);
    void update_alien_fall(float dt);

    // Pád vetřelce — pozice padajícího a záskoku
    int fall_row_ = -1, fall_col_ = -1;
    float fall_y_ = 0.0f;         // Y offset padajícího
    float fall_vy_ = 0.0f;        // rychlost pádu
    bool fall_splashed_ = false;   // už dopadl
    float replacement_y_ = 0.0f;  // Y pozice záskoku (letí shora)
    bool replacement_active_ = false;
    float replacement_x_ = 0.0f;
};
