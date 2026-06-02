
# Planned features

- debugger Step variants (Scanline step, until ROM exit, until SP unwind, ...)
- VRAM viewer, especially for MZ-800 with the ability to switch display modes and modify palettes
- MZ-800: CG-RAM editor
- MZ-1500: PCG viewer/editor
- LUA scripting
- Smart memory search
- MCP server
- dynamic IORQ bus, plug-and-play
- ability to attach external modules/libraries to IORQ
- Reference search (where exactly is this read/written)
- Reverse trace (who wrote the value at address X?)
- Back step
- Cheat search
- complete overscreen menu
- TapeMZ support and online conversion from wav to mzf
- gdb support
- full CENTRONICS plotter support (D100 printer simulation)
- full Sharp MZ-1P16 plotter/printer simulation

## Emulation

- Re-measure on real HW: `VIDEO_H_BACK_PORCH_TICKS`, `VIDEO_H_FRONT_PORCH_TICKS` (currently 104+39=143, but the exact split is not verified)
- Consider emulating partially populated VRAM on MZ-800
- MZ-1500 gdg: verify absence of VRAM latch

## Localization

- Go through all calls to `baseui_show_error_message()` and `baseui_show_message()` in the emulator and ensure that both format strings and messages pass through the `_()` macro. So far found unlocalized: `src/emulator/mzarch/bootstrap.c` (2x `baseui_show_error_message`). The test infrastructure (`test-i18n-coverage`) does not catch these cases because it only checks files in `src/ui-imgui/` and `src/ui/`.
- A large part of the UI texts in `src/ui-imgui/` still does not have localization macros (`_()` / `_L()`). It is necessary to systematically go through all windows and dialogs and add translations for all visible strings (labels, hints, messages, button captions).

## ImGui - Keeping this here permanently as a reminder

- For all localizable active elements it is necessary to ensure uniqueness of the name by adding a suffix `##unique_name`
- It is necessary to go through all ImGui routines in `ui-imgui` and make sure that, in error states, `return` is not called before the ImGui element (especially Window) is closed


# Bugs awaiting a fix

- **topmenu**: When the user uses arrow keys for navigation, they are picked up as keyboard input to the emulation (consider whether this behavior can be suppressed)
- **MZ-800 emulation bug**: For a very long time there has been a feeling that when the Flappy demo is playing, the music tempo speeds up during HW scroll. That would suggest that the GDG has some additional undocumented state causing CPU WAIT. This needs to be verified by measurement on real HW.
