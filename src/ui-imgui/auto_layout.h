/*
 * auto_layout.h - Automatický layout ImGui oken při prvním otevření.
 *
 * Master helper pro všechna okna aplikace (debugger, settings, dialogy, ...).
 * Pokud okno nemá uloženou pozici v imgui.ini (= fresh install nebo user
 * smazal odpovídající [Window][...] sekci), získá pozici přes tento helper.
 * Algoritmus prohledá rectangle aktuálně viditelných ImGui oken na primárním
 * monitoru a najde nejmenší score pozici (viz auto_layout.cpp komentáře).
 *
 * Po prvním umístění si ImGui pozici uloží do imgui.ini přes
 * ImGuiCond_FirstUseEver - další start aplikace tedy načte user-modified
 * pozici z ini, ne výchozí auto-layout.
 *
 * API:
 *  - auto_layout_first_use_portrait(): okna na výšku/standalone, hledá volné
 *    místo podél boků hlavního viewportu (= SDL emu okno).
 *  - auto_layout_first_use_near_mouse(): modální/popup okna - pozice poblíž
 *    kurzoru, clamped do viewport bounds.
 *
 * Obě funkce volat **PŘED** ImGui::Begin() okna.
 *
 * Self-exclusion: pokud iterace narazí na okno se stejným raw názvem jako
 * window_name (= okno už bylo dříve otevřené a má v ImGui state záznam),
 * vyloučí ho z occupied listu.
 *
 * Vyloučená okna (vždy): "##MainEmulatorWindow", "##OverlayWindow",
 * ChildWindow, Tooltip, Popup. Tj. emulator screen okno se ignoruje
 * i ve fullscreen režimu - debug okna přes něj se kreslí překrytím.
 *
 * Debug log:
 *  Volat aplikaci s env proměnnou DBG_AUTO_LAYOUT=1 zapne podrobné logy
 *  do stdout (rozměry monitoru a viewport, list occupied oken, výsledná
 *  pozice). Bez env nebo s hodnotou "0" / prázdnou jsou logy potlačené.
 *
 * Licence: GPLv3
 */

#ifndef AUTO_LAYOUT_H
#define AUTO_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Aplikuje výchozí pozici a velikost pro okno na výšku/standalone.
 *
 * Pokud okno již má uloženou pozici v imgui.ini, ImGui ji zachová
 * (ImGuiCond_FirstUseEver). Volat PŘED ImGui::Begin().
 *
 * @param window_name  Raw název okna předaný do ImGui::Begin() (vč.
 *                     "##" / "###" suffixu). Použije se pro self-exclusion
 *                     v collision detect.
 * @param pref_w       Preferovaná šířka okna v pixelech.
 * @param pref_h       Preferovaná výška okna v pixelech.
 */
void auto_layout_first_use_portrait(const char *window_name,
                                     float pref_w, float pref_h);


/**
 * @brief Aplikuje výchozí pozici poblíž kurzoru pro modální / popup okno.
 *
 * Pozice = mouse position centrovaná na okno, clampovaná do bounds
 * primárního viewportu. ImGuiCond_FirstUseEver - po manuálním přesunu
 * uživatelem se další otevření respektuje uloženou pozici.
 *
 * @param window_name  Raw název okna (zachováno pro symetrii s portrait API;
 *                     v near-mouse variantě se nepoužívá k collision detect).
 * @param pref_w       Preferovaná šířka.
 * @param pref_h       Preferovaná výška.
 */
void auto_layout_first_use_near_mouse(const char *window_name,
                                       float pref_w, float pref_h);

#ifdef __cplusplus
}
#endif

#endif /* AUTO_LAYOUT_H */
