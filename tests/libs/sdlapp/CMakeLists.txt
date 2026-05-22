# tests/libs/sdlapp/CMakeLists.txt
# Standalone - linkuje s SDL3 + GLib (sdlapp framework závislosti)

mz_add_test(sdlapp
    TYPE     STANDALONE
    SOURCES  test_sdlapp.c
    LIBS     mzlib_sdlapp mz::sdl3 mz::glib mz::platform
    LABELS   "standalone;libs"
)

mz_add_test(sdlapp_options
    TYPE     STANDALONE
    SOURCES  test_sdlapp_options.c
    LIBS     mzlib_sdlapp mz::sdl3 mz::glib mz::platform
    LABELS   "standalone;libs"
)
