/**
 * @file sdl_backend.c
 * @brief Implementace SDL backendu
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_revision.h>
#include <glib.h>
#include <stdio.h>

#include "../sdlapp_options.h"

/**
 * @brief Vrátí TRUE pokud byl emulátor spuštěn s CLI volbou @c --headless.
 *
 * Helper pro no-op brány v audio init (a případně dalších bodech, kde
 * je potřeba úplně vynechat SDL device otevření). Video subsystém musí
 * být inicializovaný i v headless režimu, protože hlavní smyčka
 * @ref sdlapp_run() používá @c SDL_PollEvent (= vyžaduje SDL events,
 * které SDL3 váže na SDL_INIT_VIDEO inicializaci).
 *
 * @return TRUE pokud @c --headless je přítomný v argv, jinak FALSE.
 *
 * @note Lookup do argv je O(n) - jen volat z init cest, ne z hot path.
 */
static gboolean sdl3_backend_is_headless(void)
{
    return sdlapp_option_present("--headless") ? TRUE : FALSE;
}

static gboolean sdl3_main_check_version(void)
{
    SDL_Log("Compiled SDL version: %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    SDL_Log("Compiled SDL rev: %s\n", SDL_REVISION);
    SDL_Log("Compiled SDL ver: %x\n", SDL_VERSION);

    SDL_GetVersion();
    SDL_GetRevision();

    if (!SDL_VERSION_ATLEAST(3, 0, 0))
    {
        SDL_Log("SDL_VERSION is less than 3.0.0");
        return false;
    };

    SDL_Log("Linked SDL ver.: %x", SDL_GetVersion());
    SDL_Log("Linked SDL rev.: %s", SDL_GetRevision());

    return true;
}

static SDL_AssertState sdl3_main_CustomAssertHandler(const SDL_AssertData *data, void *userdata)
{
    (void)userdata;
    fprintf(stderr, "Assert failed: %s, file %s, line %d\n", data->condition, data->filename, data->linenum);
    SDL_TriggerBreakpoint();

    // Můžete vrátit SDL_ASSERTION_RETRY nebo SDL_ASSERTION_BREAK podle potřeby
    return SDL_ASSERTION_ABORT; // nebo jiný stav, např. SDL_ASSERTION_IGNORE, SDL_ASSERTION_RETRY
}

static gboolean global_sdl3_backend_initialized = false;

static gboolean sdl3_main_init(void)
{
    if (global_sdl3_backend_initialized)
    {
        return true;
    };

    global_sdl3_backend_initialized = true;

    SDL_SetLogPriority(
        (SDL_LOG_CATEGORY_APPLICATION |
         SDL_LOG_CATEGORY_ERROR |
         SDL_LOG_CATEGORY_ASSERT |
         SDL_LOG_CATEGORY_SYSTEM |
         SDL_LOG_CATEGORY_AUDIO |
         SDL_LOG_CATEGORY_VIDEO |
         SDL_LOG_CATEGORY_RENDER |
         SDL_LOG_CATEGORY_INPUT |
         SDL_LOG_CATEGORY_TEST),
        SDL_LOG_PRIORITY_VERBOSE);

    if (!sdl3_main_check_version())
        return false;

    SDL_SetAssertionHandler(sdl3_main_CustomAssertHandler, NULL);

    return true;
}

gboolean sdl3_backend_video_init(void)
{
    if (!sdl3_main_init())
    {
        return false;
    };

    SDL_InitFlags was_init = SDL_InitSubSystem(0);
    SDL_InitFlags need_init = 0;

    need_init |= (was_init & SDL_INIT_VIDEO) ? 0 : SDL_INIT_VIDEO;
    need_init |= (was_init & SDL_INIT_EVENTS) ? 0 : SDL_INIT_EVENTS;

    if (need_init == 0)
    {
        return true;
    };

    gboolean result;

    if (was_init)
    {
        result = SDL_InitSubSystem(need_init);
    }
    else
    {
        result = SDL_Init(need_init);
    };

    if (!result)
    {
        g_printerr("Can't initialize SDL VIDEO: %s\n", SDL_GetError());
        return FALSE;
    };

    return TRUE;
}

void sdl3_backend_video_quit(void)
{
    if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    };
}

gboolean sdl3_backend_video_opengl_init(void)
{
    if (!sdl3_backend_video_init())
    {
        return false;
    };

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    return TRUE;
}

gboolean sdl3_backend_joy_init(void)
{
    if (!sdl3_backend_video_init())
    {
        return false;
    };

    if (SDL_WasInit(SDL_INIT_JOYSTICK))
    {
        return true;
    };

    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK))
    {
        g_printerr("Can't initialize SDL JOYSTICK: %s\n", SDL_GetError());
        return FALSE;
    };

    return TRUE;
}

void sdl3_backend_joy_quit(void)
{
    if (SDL_WasInit(SDL_INIT_JOYSTICK))
    {
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    };
}

gboolean sdl3_backend_audio_init(void)
{
    if (!sdl3_main_init())
    {
        return false;
    };

    /* Headless režim: žádný audio device se neotevírá. Vrátíme TRUE,
     * aby vyšší vrstvy (iface_audio) pokračovaly v inicializaci svých
     * interních bufferů a state machine, ale samotný SDL_OpenAudioDeviceStream
     * je gated v iface-audio/sdl3/sdl3_audio.c::iface_audio_lowlevel_init. */
    if (sdl3_backend_is_headless())
    {
        return TRUE;
    };

    SDL_InitFlags was_init = SDL_InitSubSystem(0);
    if (was_init & SDL_INIT_AUDIO)
    {
        return true;
    };

    gboolean result;

    if (was_init)
    {
        result = SDL_InitSubSystem(SDL_INIT_AUDIO);
    }
    else
    {
        result = SDL_Init(SDL_INIT_AUDIO);
    };

    if (!result)
    {
        g_printerr("Can't initialize SDL AUDIO: %s\n", SDL_GetError());
        return FALSE;
    }

    return TRUE;
}

void sdl3_backend_audio_quit(void)
{
    /* V headless režimu jsme SDL_INIT_AUDIO nikdy nealokovali (viz
     * @ref sdl3_backend_audio_init), proto není co ukončovat. */
    if (sdl3_backend_is_headless())
    {
        return;
    };

    if (SDL_WasInit(SDL_INIT_AUDIO))
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    };
}

void sdl3_backend_quit(void)
{
    if (!global_sdl3_backend_initialized)
    {
        return;
    };

    global_sdl3_backend_initialized = false;

    if (SDL_WasInit(0))
    {
        SDL_Quit();
    };
}