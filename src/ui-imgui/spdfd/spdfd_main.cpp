/**
 * @file spdfd_main.cpp
 * @brief Implementace vstupního bodu modulu @c spdfd.
 *
 * Validuje sekvenci argumentů následující po @c --spdfd a buď spustí modul,
 * nebo se vrátí volajícímu se sentinel @c SPDFD_FALLBACK_TO_EMU.
 */

#include "ui-imgui/spdfd/spdfd_main.h"
#include "ui-imgui/spdfd/game.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>

/**
 * @brief Ověří argumenty, které není potřeba komentovat.
 *
 * @param argc Počet argumentů v @p argv.
 * @param argv Pole argumentů.
 * @return @c true pokud ano
 *         @c false pokud ani náhodou
 */
static bool spdfd_validate_args(int argc, char *argv[])
{
    /* Klíč pro XOR dekódování. Čtenář source kódu si musí spočítat
     * skutečné očekávané hodnoty sám. */
    static constexpr uint32_t K = 0xC0FEu;
    static constexpr uint32_t TABLE[3] = { 0xC3DEu, 0xC522u, 0xC242u };

    /* Najít '--spdfd' v argv. */
    int idx = -1;
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] && std::strcmp(argv[i], "--spdfd") == 0)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        return false;
    }

    /* Musí následovat alespoň 3 další tokeny. */
    if (idx + 3 >= argc)
    {
        return false;
    }

    /* Každý token musí matchnout dekódovanou tabulkovou hodnotu na
     * své pozici. Pořadí je striktní - prohození hodnot = fail. */
    for (int i = 0; i < 3; ++i)
    {
        const char *tok = argv[idx + 1 + i];
        if (!tok)
        {
            return false;
        }
        char *endp = nullptr;
        long v = std::strtol(tok, &endp, 10);
        if (endp == tok || *endp != '\0')
        {
            return false;
        }
        if (static_cast<uint32_t>(v) != (TABLE[i] ^ K))
        {
            return false;
        }
    }

    return true;
}

extern "C" int spdfd_main(int argc, char *argv[])
{
    /* Validace handshake sekvence. Bez ní se modul nespouští a volající
     * dostane sentinel - main() pak pokračuje normální cestou. */
    if (!spdfd_validate_args(argc, argv))
    {
        return SPDFD_FALLBACK_TO_EMU;
    }

    /* Inicializace RNG - bez seedu by každý běh modulu byl deterministicky
     * stejný (state machine používá std::rand() pro náhodné variace). */
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    Game game;

    if (!game.init())
    {
        SDL_Log("spdfd init failed.");
        return EXIT_FAILURE;
    }

    game.run();
    game.shutdown();

    return EXIT_SUCCESS;
}
