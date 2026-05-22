# UI testy - smoke test infrastruktury (SDL3 + ImGui + OpenGL inicializace).
# Hodnota nízká (testuje vendored knihovny), ale rychlé a užitečné při ImGui
# upgrade - detekuje rozbitý backend.

mz_add_test(ui-smoke
    TYPE     UI
    SOURCES  test_ui_smoke.cpp
    LIBS     mzlib_imgui mzlib_igfd mzlib_imwidgets
    LABELS   "ui"
)
