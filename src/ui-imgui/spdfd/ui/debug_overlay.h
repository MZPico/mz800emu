#pragma once

class Formation;
class Cannon;

// ImGui debug overlay — toggle klávesou F1.
namespace DebugOverlay {

    void toggle();
    bool visible();

    // Vykreslí debug informace
    void render(float fps, const Formation& formation, const Cannon& cannon,
                int bullet_count);

} // namespace DebugOverlay
