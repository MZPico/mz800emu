#pragma once

#include <SDL3/SDL.h>

// PSG-style zvukový systém.
// Generuje zvuky procedurálně (square wave, noise) — žádné externí soubory.
// Emuluje styl zvukového čipu MZ-800.
class Audio {
public:
    bool init();
    void shutdown();

    // Zvukové efekty
    void play_shoot();       // střelba vetřelce dolů
    void play_cannon_shot(); // střelba AI děla nahoru
    void play_explosion();   // exploze vetřelce
    void play_cannon_hit();  // zásah děla
    void play_heartbeat();   // heartbeat tick (volat periodicky)
    void play_type_click();  // terminálový typing zvuk
    void play_miss_ow();     // "ow!" — střela mine formaci
    void play_ufo();         // UFO prolétá — siréna
    void play_speech_char(char ch);  // Syntetizovaný hlas — Animal Crossing styl
    void play_cannon_ouch();         // "Au!" / "Ojojoj!" — zásah děla
    void play_glass_shatter();       // Řinčení skla — havárie raketky
    void play_war_cry();             // Indiánský válečný pokřik
    void play_gunshot();             // Výstřel z koltu
    void play_arrow_shot();          // Výstřel šípu

    // Melodie — intro/title a endgame témata
    void start_intro_melody();    // spustit melancholickou intro melodii (smyčka)
    void start_endgame_melody();  // spustit klidnou durovou uspávanku (smyčka)
    void stop_melody();           // zastavit melodii

    // Heartbeat — tempo se mění podle počtu vetřelců
    void set_heartbeat_interval(float seconds);

    // Gameplay melodická linka — noty mizí s ubývajícími vetřelci
    void set_gameplay_melody(bool active);
    void set_melody_density(float ratio); // 0.0-1.0: poměr hrajících not

    void update(float dt);

    // Ztlumit/odtlumit
    void set_muted(bool muted) { muted_ = muted; }
    bool muted() const { return muted_; }

    // Je melodie aktivní?
    bool melody_playing() const { return melody_playing_; }

    // Nota pro melodii (veřejné kvůli static constexpr definici v .cpp)
    struct Note {
        float freq;      // Hz (0 = pauza)
        float duration;  // sekundy
    };

private:
    SDL_AudioStream* stream_ = nullptr;
    SDL_AudioDeviceID device_ = 0;

    bool muted_ = false;

    // Heartbeat
    float heartbeat_interval_ = 0.5f;
    float heartbeat_timer_ = 0.0f;
    int heartbeat_step_ = 0; // 0-3, střídá 4 tóny jako originál

    // Gameplay melodická linka (noty mizí s vetřelci)
    bool gameplay_melody_active_ = false;
    float gameplay_melody_density_ = 1.0f; // 1.0 = všechny noty, 0.0 = žádné
    float gameplay_melody_timer_ = 0.0f;
    int gameplay_melody_step_ = 0;
    void update_gameplay_melody(float dt);

    // Melodie — jednoduchý sekvenční přehrávač not
    const Note* melody_ = nullptr;
    int melody_len_ = 0;
    int melody_pos_ = 0;
    float melody_timer_ = 0.0f;
    bool melody_playing_ = false;
    bool melody_loop_ = false;
    void update_melody(float dt);

    // Generování zvuku — přidá samply do streamu
    void generate_square_wave(float freq, float duration, float volume = 0.3f);
    void generate_noise(float duration, float volume = 0.2f);
    void generate_sweep(float freq_start, float freq_end, float duration, float volume = 0.3f);

    // Formantová syntéza hlasu — SAM-style 8bitový hlas
    // f1/f2 = formantové frekvence, pitch = základní tón hlasivek
    void generate_formant(float f1, float f2, float pitch,
                          float duration, float volume, bool voiced);

    static constexpr int SAMPLE_RATE = 44100;
    static constexpr SDL_AudioFormat FORMAT = SDL_AUDIO_S16;
};

// Globální audio instance
extern Audio g_spdfd_audio;
