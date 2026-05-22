/**
 * @file spdfd_main.h
 * @brief Vstupní bod alternativního launch módu @c spdfd.
 *
 * Modul @c spdfd je samostatná SDL3 + ImGui aplikace zabudovaná do binárky
 * emulátoru. Aktivuje se v @c main() po vyhodnocení CLI options, pokud
 * @c --spdfd flag je doprovázen třemi specifickými parametry ve správném
 * pořadí. Bez nich (nebo se špatnými/přeházenými hodnotami) modul vrací
 * @c SPDFD_FALLBACK_TO_EMU a @c main() pokračuje běžnou emulátorovou
 * inicializací.
 *
 * Modul si vytváří vlastní SDL kontext a vlastní ImGui kontext, proto nesmí
 * běžet ve stejném procesu, kde už emulátor inicializoval své kontexty.
 */

#ifndef SPDFD_MAIN_H
#define SPDFD_MAIN_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Sentinel návratová hodnota: validace argumentů selhala.
 *
 * Když @c spdfd_main vrátí tuto hodnotu, znamená to že @c --spdfd byl
 * sice na CLI, ale chyběly/byly špatné/přeházené následující 3 parametry.
 * V tom případě @c main() ignoruje větvení a pokračuje normální cestou
 * (= spustí se emulátor).
 *
 * Hodnota je úmyslně mimo rozsah standardních exit kódů @c EXIT_SUCCESS
 * (0) a @c EXIT_FAILURE (1), aby kolize byla nemožná.
 */
#define SPDFD_FALLBACK_TO_EMU (-2)

/**
 * @brief Spustí modul @c spdfd s jeho vlastní hlavní smyčkou, pokud
 *        prošla validace argumentů.
 *
 * Funkce nejdřív ověří, že po @c --spdfd následují přesně 3 očekávané
 * parametry ve správném pořadí. Při selhání kontroly se okamžitě vrací
 * @c SPDFD_FALLBACK_TO_EMU bez jakékoliv inicializace SDL/ImGui.
 *
 * Při úspěšné validaci inicializuje vlastní SDL3 + ImGui kontext, otevře
 * vlastní okno a běží, dokud uživatel okno nezavře nebo modul sám neukončí.
 *
 * @param argc Počet CLI argumentů (předaný z main()).
 * @param argv Pole CLI argumentů (předané z main()).
 * @return @c EXIT_SUCCESS při normálním ukončení modulu,
 *         @c EXIT_FAILURE při chybě inicializace SDL/ImGui,
 *         @c SPDFD_FALLBACK_TO_EMU při neúspěšné validaci argumentů.
 *
 * @note Při návratu jiném než @c SPDFD_FALLBACK_TO_EMU nesmí být voláno
 *       z procesu, který už má inicializovaný emulátorový SDL/ImGui
 *       (= konflikt SDL_Init, konflikt ImGui kontextu).
 */
int spdfd_main(int argc, char *argv[]);

#ifdef __cplusplus
};
#endif

#endif /* SPDFD_MAIN_H */
