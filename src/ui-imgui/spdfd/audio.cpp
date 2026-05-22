#include "ui-imgui/spdfd/audio.h"
#include <cmath>
#include <vector>
#include <cstdlib>

Audio g_spdfd_audio;

bool Audio::init() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("SDL_INIT_AUDIO failed: %s", SDL_GetError());
        return false;
    }

    SDL_AudioSpec spec;
    spec.freq = SAMPLE_RATE;
    spec.format = FORMAT;
    spec.channels = 1;

    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                         &spec, nullptr, nullptr);
    if (!stream_) {
        SDL_Log("SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }

    // Spustit přehrávání
    SDL_ResumeAudioStreamDevice(stream_);

    return true;
}

void Audio::shutdown() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void Audio::generate_square_wave(float freq, float duration, float volume) {
    if (!stream_ || muted_) return;

    int num_samples = static_cast<int>(SAMPLE_RATE * duration);
    std::vector<int16_t> samples(num_samples);

    float period = SAMPLE_RATE / freq;
    int16_t amplitude = static_cast<int16_t>(32767 * volume);

    for (int i = 0; i < num_samples; ++i) {
        // Square wave — 50% duty cycle
        float phase = std::fmod(static_cast<float>(i), period) / period;
        samples[i] = (phase < 0.5f) ? amplitude : -amplitude;

        // Jednoduchý fade-out v posledních 20%
        float t = static_cast<float>(i) / num_samples;
        if (t > 0.8f) {
            float fade = 1.0f - (t - 0.8f) / 0.2f;
            samples[i] = static_cast<int16_t>(samples[i] * fade);
        }
    }

    SDL_PutAudioStreamData(stream_, samples.data(),
                            num_samples * sizeof(int16_t));
}

void Audio::generate_noise(float duration, float volume) {
    if (!stream_ || muted_) return;

    int num_samples = static_cast<int>(SAMPLE_RATE * duration);
    std::vector<int16_t> samples(num_samples);

    int16_t amplitude = static_cast<int16_t>(32767 * volume);

    for (int i = 0; i < num_samples; ++i) {
        // Bílý šum
        samples[i] = static_cast<int16_t>((std::rand() % (amplitude * 2)) - amplitude);

        // Fade-out
        float t = static_cast<float>(i) / num_samples;
        if (t > 0.3f) {
            float fade = 1.0f - (t - 0.3f) / 0.7f;
            samples[i] = static_cast<int16_t>(samples[i] * fade);
        }
    }

    SDL_PutAudioStreamData(stream_, samples.data(),
                            num_samples * sizeof(int16_t));
}

void Audio::generate_sweep(float freq_start, float freq_end, float duration, float volume) {
    if (!stream_ || muted_) return;

    int num_samples = static_cast<int>(SAMPLE_RATE * duration);
    std::vector<int16_t> samples(num_samples);

    int16_t amplitude = static_cast<int16_t>(32767 * volume);
    float phase = 0.0f;

    for (int i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / num_samples;
        float freq = freq_start + (freq_end - freq_start) * t;

        // Square wave s proměnnou frekvencí
        float period = SAMPLE_RATE / freq;
        phase += 1.0f;
        if (phase >= period) phase -= period;

        samples[i] = (phase < period * 0.5f) ? amplitude : -amplitude;

        // Fade-out
        if (t > 0.6f) {
            float fade = 1.0f - (t - 0.6f) / 0.4f;
            samples[i] = static_cast<int16_t>(samples[i] * fade);
        }
    }

    SDL_PutAudioStreamData(stream_, samples.data(),
                            num_samples * sizeof(int16_t));
}

void Audio::play_shoot() {
    // Krátký sestupný sweep — "pew"
    generate_sweep(800.0f, 200.0f, 0.1f, 0.15f);
}

void Audio::play_cannon_shot() {
    // Krátký vzestupný sweep — střela AI
    generate_sweep(200.0f, 600.0f, 0.08f, 0.12f);
}

void Audio::play_explosion() {
    // Šum + nízký tón — exploze
    generate_noise(0.15f, 0.2f);
}

void Audio::play_cannon_hit() {
    // Delší exploze s hlubším tónem
    generate_sweep(300.0f, 50.0f, 0.3f, 0.25f);
}

void Audio::play_heartbeat() {
    // Čtyři tóny jako v originálu Space Invaders
    // Sestupná sekvence: C-B-A-G#
    static constexpr float TONES[] = { 130.8f, 123.5f, 110.0f, 103.8f };
    float freq = TONES[heartbeat_step_ % 4];
    generate_square_wave(freq, 0.08f, 0.15f);
    heartbeat_step_ = (heartbeat_step_ + 1) % 4;
}

void Audio::play_type_click() {
    // Krátký vysoký klik — terminálový zvuk psaní
    generate_square_wave(1200.0f, 0.01f, 0.05f);
}

void Audio::play_miss_ow() {
    // Krátký vysoký blip — "ow!" z vesmíru
    generate_sweep(600.0f, 1200.0f, 0.06f, 0.08f);
}

void Audio::play_ufo() {
    // Siréna — rychlý sweep nahoru a dolů
    generate_sweep(300.0f, 800.0f, 0.15f, 0.1f);
}

// =============================================================================
// Formantová syntéza hlasu — SAM-style 8bitový hlas
// Inspirováno Software Automatic Mouth (1982) a formantovými syntezátory
// MZ-800 éry. Sawtooth/noise zdroj → dva paralelní rezonátory (F1, F2).
// =============================================================================

// Tabulka fonémů — mapování písmen na formantové frekvence
// Zjednodušená verze inspirovaná SAM a Klattovým modelem
struct PhonemeData {
    float f1;       // první formant (Hz), 0 = neznělé bez F1
    float f2;       // druhý formant (Hz)
    bool voiced;    // znělý (sawtooth) vs neznělý (šum)
    float dur;      // relativní trvání (1.0 = normální)
};

static const PhonemeData PHONEME_TABLE[26] = {
    {800, 1200, true,  1.3f}, // A — otevřená samohláska
    {200, 1000, true,  0.5f}, // B — znělá okluzíva
    {0,   2500, false, 0.4f}, // C — neznělá
    {200, 1800, true,  0.5f}, // D — znělá okluzíva
    {400, 2200, true,  1.3f}, // E — přední samohláska
    {0,   1500, false, 0.5f}, // F — neznělá frikativa
    {200, 1800, true,  0.5f}, // G — znělá okluzíva
    {0,   1200, false, 0.4f}, // H — aspirace
    {300, 2500, true,  1.2f}, // I — vysoká přední samohláska
    {250, 2200, true,  0.6f}, // J — polosamohláska
    {0,   1800, false, 0.4f}, // K — neznělá okluzíva
    {400, 1100, true,  0.6f}, // L — likvida
    {300, 1200, true,  0.7f}, // M — nazála
    {300, 1500, true,  0.6f}, // N — nazála
    {500,  900, true,  1.3f}, // O — zadní samohláska
    {0,   1000, false, 0.3f}, // P — neznělá okluzíva
    {0,   1500, false, 0.4f}, // Q — neznělá
    {400, 1300, true,  0.6f}, // R — aproximanta
    {0,   2500, false, 0.6f}, // S — neznělá sibilanta
    {0,   1800, false, 0.3f}, // T — neznělá okluzíva
    {350,  700, true,  1.3f}, // U — zadní vysoká samohláska
    {300, 1200, true,  0.5f}, // V — znělá frikativa
    {350,  800, true,  0.5f}, // W — polosamohláska
    {0,   2000, false, 0.4f}, // X — neznělá
    {300, 2200, true,  0.6f}, // Y — polosamohláska
    {250, 1500, true,  0.5f}, // Z — znělá sibilanta
};

void Audio::generate_formant(float f1, float f2, float pitch,
                              float duration, float volume, bool voiced) {
    if (!stream_ || muted_) return;

    int num_samples = static_cast<int>(SAMPLE_RATE * duration);
    if (num_samples < 1) return;
    std::vector<int16_t> samples(num_samples);

    // Koeficienty rezonátoru — 2-pólový IIR bandpass filter
    // y[n] = gain * x[n] - a1 * y[n-1] - a2 * y[n-2]
    auto calc_resonator = [&](float freq, float bw,
                               float& a1, float& a2, float& gain) {
        float R = std::exp(-3.14159f * bw / SAMPLE_RATE);
        float cosw = std::cos(2.0f * 3.14159f * freq / SAMPLE_RATE);
        a1 = -2.0f * R * cosw;
        a2 = R * R;
        gain = 1.0f - R;  // zisk pro jednotkovou odezvu na impulz
    };

    // F1 rezonátor (nižší formant — "otevřenost" úst)
    float a1_1, a2_1, g1;
    calc_resonator(f1 > 0 ? f1 : 400.0f, 90.0f, a1_1, a2_1, g1);
    float r1y1 = 0, r1y2 = 0;

    // F2 rezonátor (vyšší formant — "pozice jazyka")
    float a1_2, a2_2, g2;
    calc_resonator(f2, 110.0f, a1_2, a2_2, g2);
    float r2y1 = 0, r2y2 = 0;

    // Zdrojový signál
    float phase = 0.0f;
    float period = (pitch > 0) ? (static_cast<float>(SAMPLE_RATE) / pitch) : 0;

    for (int i = 0; i < num_samples; ++i) {
        float src;
        if (voiced && period > 0) {
            // Sawtooth vlna — bohatý harmonický obsah, typický pro 8bit syntézu
            phase += 1.0f;
            if (phase >= period) phase -= period;
            src = 2.0f * (phase / period) - 1.0f;
        } else {
            // Bílý šum pro neznělé hlásky (sykavky, okluzivy)
            src = ((std::rand() & 0x7FFF) / 16383.5f) - 1.0f;
        }

        // Paralelní formantové filtry
        float o1 = g1 * src - a1_1 * r1y1 - a2_1 * r1y2;
        r1y2 = r1y1;
        r1y1 = o1;

        float o2 = g2 * src - a1_2 * r2y1 - a2_2 * r2y2;
        r2y2 = r2y1;
        r2y1 = o2;

        // Smíchat — F1 dominuje, F2 dodává barvu hlasu
        float out = o1 * 1.0f + o2 * 0.5f;

        // Amplitudová obálka (attack-sustain-release)
        float t = static_cast<float>(i) / num_samples;
        float env = 1.0f;
        if (t < 0.06f) env = t / 0.06f;
        if (t > 0.75f) env = (1.0f - t) / 0.25f;

        out *= env * volume;

        // Tvrdý clip — přidává "8bitovou drsnotu"
        if (out > 1.0f) out = 1.0f;
        if (out < -1.0f) out = -1.0f;
        samples[i] = static_cast<int16_t>(out * 32767);
    }

    SDL_PutAudioStreamData(stream_, samples.data(),
                            num_samples * sizeof(int16_t));
}

void Audio::play_speech_char(char ch) {
    // Formantová syntéza — skutečný 8bitový hlas jako SAM
    if (!stream_ || muted_) return;

    char upper = (ch >= 'a' && ch <= 'z') ? (ch - 32) : ch;
    int idx = (upper >= 'A' && upper <= 'Z') ? (upper - 'A') : 0;

    const auto& p = PHONEME_TABLE[idx];
    // Základní tón 220Hz (vyšší = vetřelecký hlas) + náhodná variace
    float pitch = 220.0f + static_cast<float>((std::rand() % 40) - 20);
    float dur = 0.07f * p.dur;

    generate_formant(p.f1, p.f2, pitch, dur, 0.5f, p.voiced);
}

void Audio::play_cannon_ouch() {
    // Formantový "Au!" / "Ojojoj!" — 8bitový hlas děla
    if (!stream_ || muted_) return;

    // Nižší hlas pro dělo (lidský operátor)
    float pitch = 150.0f + static_cast<float>(std::rand() % 20);
    int pattern = std::rand() % 3;

    if (pattern == 0) {
        // "Au!"
        generate_formant(800, 1200, pitch, 0.08f, 0.6f, true);       // A
        generate_formant(350,  700, pitch * 0.9f, 0.12f, 0.5f, true); // U (klesající)
    } else if (pattern == 1) {
        // "Ojojoj!"
        for (int i = 0; i < 3; ++i) {
            float p = pitch * (1.0f - i * 0.03f); // mírně klesající
            generate_formant(500, 900, p, 0.05f, 0.6f, true);          // O
            generate_formant(300, 2200, p * 1.1f, 0.04f, 0.4f, true);  // j
        }
    } else {
        // "Ajaj!"
        generate_formant(800, 1200, pitch, 0.07f, 0.6f, true);         // A
        generate_formant(300, 2200, pitch * 1.1f, 0.04f, 0.4f, true);  // j
        generate_formant(800, 1200, pitch * 0.95f, 0.09f, 0.6f, true); // A
        generate_formant(300, 2200, pitch * 1.05f, 0.04f, 0.3f, true); // j
    }
}

void Audio::play_glass_shatter() {
    // Řinčení skla — vysokofrekvenční šum s metalickým sweepem
    if (!stream_ || muted_) return;
    generate_noise(0.25f, 0.3f);
    generate_sweep(3000.0f, 800.0f, 0.15f, 0.15f);
    generate_noise(0.1f, 0.15f);
}

void Audio::play_war_cry() {
    // Indiánský válečný pokřik — rychlé střídání vysokých tónů "wa-wa-wa"
    if (!stream_ || muted_) return;
    for (int i = 0; i < 4; ++i) {
        generate_formant(800, 1200, 300.0f, 0.04f, 0.4f, true);  // wa
        generate_formant(300, 800, 350.0f, 0.03f, 0.3f, true);   // -
    }
}

void Audio::play_gunshot() {
    // Výstřel z koltu — ostrý šum s krátkým úderem
    if (!stream_ || muted_) return;
    generate_noise(0.03f, 0.35f);
    generate_sweep(400.0f, 100.0f, 0.05f, 0.15f);
}

void Audio::play_arrow_shot() {
    // Výstřel šípu — krátký vysoký svist
    if (!stream_ || muted_) return;
    generate_sweep(1500.0f, 600.0f, 0.06f, 0.1f);
}

void Audio::set_heartbeat_interval(float seconds) {
    if (seconds < 0.1f) seconds = 0.1f;
    heartbeat_interval_ = seconds;
}

void Audio::set_gameplay_melody(bool active) {
    gameplay_melody_active_ = active;
    gameplay_melody_timer_ = 0.0f;
    gameplay_melody_step_ = 0;
}

void Audio::set_melody_density(float ratio) {
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    gameplay_melody_density_ = ratio;
}

// 8-notová smyčka pro gameplay — jednoduchá, tichá, melancholická
static constexpr float GAMEPLAY_NOTES[] = {
    196.0f,  // G3
    220.0f,  // A3
    261.6f,  // C4
    246.9f,  // B3
    220.0f,  // A3
    196.0f,  // G3
    174.6f,  // F3
    164.8f,  // E3
};
static constexpr int GAMEPLAY_NOTE_COUNT = 8;

void Audio::update_gameplay_melody(float dt) {
    if (!gameplay_melody_active_ || muted_) return;

    // Tempo melodie je 2x pomalejší než heartbeat
    float melody_interval = heartbeat_interval_ * 2.0f;
    gameplay_melody_timer_ += dt;

    if (gameplay_melody_timer_ >= melody_interval) {
        gameplay_melody_timer_ -= melody_interval;

        // Rozhodnutí jestli tato nota hraje — závisí na density
        // Nota hraje pokud její pozice v smyčce je menší než density * count
        int threshold = static_cast<int>(gameplay_melody_density_ * GAMEPLAY_NOTE_COUNT);
        if (threshold < 1 && gameplay_melody_density_ > 0.05f) threshold = 1;

        if (gameplay_melody_step_ < threshold) {
            float freq = GAMEPLAY_NOTES[gameplay_melody_step_];
            generate_square_wave(freq, 0.12f, 0.04f); // velmi tiché
        }

        gameplay_melody_step_ = (gameplay_melody_step_ + 1) % GAMEPLAY_NOTE_COUNT;
    }
}

// Melancholická intro melodie — C moll, pomalá, smutná
// Jako Space Invaders heartbeat z pohledu vetřelce — ne hrozivý, ale smutný
static constexpr Audio::Note INTRO_MELODY[] = {
    // Fráze 1 — sestupná moll linka
    {130.8f, 0.5f},  // C3
    {0.0f,   0.1f},  // pauza
    {155.6f, 0.5f},  // Eb3
    {0.0f,   0.1f},
    {196.0f, 0.7f},  // G3
    {0.0f,   0.2f},
    {155.6f, 0.4f},  // Eb3
    {0.0f,   0.1f},

    // Fráze 2 — smutný oblouk dolů
    {174.6f, 0.5f},  // F3
    {0.0f,   0.1f},
    {146.8f, 0.5f},  // D3
    {0.0f,   0.1f},
    {130.8f, 0.8f},  // C3 (delší — důraz)
    {0.0f,   0.4f},  // pauza

    // Fráze 3 — nádech naděje, pak pád
    {196.0f, 0.4f},  // G3
    {0.0f,   0.1f},
    {185.0f, 0.4f},  // F#3 (chromatický průchod)
    {0.0f,   0.1f},
    {174.6f, 0.4f},  // F3
    {0.0f,   0.1f},
    {155.6f, 0.6f},  // Eb3
    {0.0f,   0.1f},
    {130.8f, 0.9f},  // C3 (závěr)
    {0.0f,   0.8f},  // dlouhá pauza před opakováním
};

// Klidná endgame melodie — C dur, jednoduchá uspávanka
// Kontrast k celé hře. Čistá, jako pozvání domů.
static constexpr Audio::Note ENDGAME_MELODY[] = {
    {261.6f, 0.6f},  // C4
    {0.0f,   0.1f},
    {329.6f, 0.5f},  // E4
    {0.0f,   0.1f},
    {392.0f, 0.7f},  // G4
    {0.0f,   0.2f},
    {329.6f, 0.4f},  // E4
    {0.0f,   0.1f},

    {349.2f, 0.5f},  // F4
    {0.0f,   0.1f},
    {329.6f, 0.4f},  // E4
    {0.0f,   0.1f},
    {293.7f, 0.5f},  // D4
    {0.0f,   0.1f},
    {261.6f, 0.8f},  // C4
    {0.0f,   0.3f},

    // Druhá fráze — vzestup a klidný konec
    {293.7f, 0.4f},  // D4
    {0.0f,   0.1f},
    {329.6f, 0.5f},  // E4
    {0.0f,   0.1f},
    {349.2f, 0.4f},  // F4
    {0.0f,   0.1f},
    {329.6f, 0.6f},  // E4
    {0.0f,   0.1f},
    {261.6f, 1.0f},  // C4 (dlouhý závěr)
    {0.0f,   1.0f},  // dlouhá pauza
};

void Audio::start_intro_melody() {
    melody_ = INTRO_MELODY;
    melody_len_ = sizeof(INTRO_MELODY) / sizeof(INTRO_MELODY[0]);
    melody_pos_ = 0;
    melody_timer_ = 0.0f;
    melody_playing_ = true;
    melody_loop_ = true;
}

void Audio::start_endgame_melody() {
    melody_ = ENDGAME_MELODY;
    melody_len_ = sizeof(ENDGAME_MELODY) / sizeof(ENDGAME_MELODY[0]);
    melody_pos_ = 0;
    melody_timer_ = 0.0f;
    melody_playing_ = true;
    melody_loop_ = true;
}

void Audio::stop_melody() {
    melody_playing_ = false;
    melody_ = nullptr;
}

void Audio::update_melody(float dt) {
    if (!melody_playing_ || !melody_) return;

    melody_timer_ += dt;

    if (melody_timer_ >= melody_[melody_pos_].duration) {
        melody_timer_ -= melody_[melody_pos_].duration;
        melody_pos_++;

        // Konec melodie — loop nebo stop
        if (melody_pos_ >= melody_len_) {
            if (melody_loop_) {
                melody_pos_ = 0;
            } else {
                melody_playing_ = false;
                return;
            }
        }

        // Zahrát další notu (pokud není pauza)
        float freq = melody_[melody_pos_].freq;
        if (freq > 0.0f) {
            // Kratší tón než nota — legato efekt
            float note_dur = melody_[melody_pos_].duration * 0.85f;
            generate_square_wave(freq, note_dur, 0.08f);
        }
    }
}

void Audio::update(float dt) {
    if (muted_) return;

    // Intro/endgame melodie
    update_melody(dt);

    // Gameplay melodická linka
    update_gameplay_melody(dt);
}
