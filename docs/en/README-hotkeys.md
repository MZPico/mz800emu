# MZ-800 emulator Hot-Keys

Global keyboard shortcuts available across the entire emulator window.
Source: `src/ui-imgui/topmenu/global_shortcuts.cpp`.

## Reset and general

| Key                        | Action                                                                      |
|----------------------------|-----------------------------------------------------------------------------|
| `F12`                      | Emulated computer reset                                                     |
| `F11`                      | Joystick calibration (or Reset when emulator runs under Windows Debugger)   |

## Window and display

| Key                        | Action                                                                      |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + Ctrl + Enter`       | Toggle fullscreen / windowed mode                                           |
| `Alt + W`                  | Fix window aspect ratio by Width                                            |
| `Alt + H`                  | Fix window aspect ratio by Height                                           |

## Emulation speed

| Key                        | Action                                                                      |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + P`                  | Toggle Pause / Resume                                                       |
| `Alt + N`                  | Set Normal emulation speed (100 %)                                          |
| `Alt + M`                  | Switch between Max and (Normal or Custom) emulation speed                   |
| `Alt + Shift + M`          | Switch between Custom and (Normal or Max) emulation speed                   |
| `Alt + Up`                 | Increase Custom speed (step 1 % of Normal speed)                            |
| `Alt + Shift + Up`         | Increase Custom speed (step 10 % of Normal speed)                           |
| `Alt + Pg_Up`              | Increase Custom speed (step 100 % of Normal speed)                          |
| `Alt + Down`               | Decrease Custom speed (step 1 % of Normal speed)                            |
| `Alt + Shift + Down`       | Decrease Custom speed (step 10 % of Normal speed)                           |
| `Alt + Pg_Down`            | Decrease Custom speed (step 100 % of Normal speed)                          |

## Floppy disk (FDC)

| Key                        | Action                                                                      |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + [1..4]`             | Mount FD image into drive 1..4 (and enable WD279x)                          |
| `Alt + Shift + [1..4]`     | Eject FD image from drive 1..4                                              |

## Cassette (CMT) and virtual keyboard

| Key                        | Action                                                                      |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + C`                  | Show / Hide Virtual CMT window                                              |
| `Alt + K`                  | Show / Hide Virtual Keyboard window                                         |

## Snapshots

| Key                        | Action                                                                      |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + F6`                 | Save Snapshot (open save dialog)                                            |
| `Alt + F7`                 | Load Snapshot (open load dialog)                                            |
| `Alt + F8`                 | Quick Save                                                                  |
| `Alt + F9`                 | Quick Load                                                                  |

## Debugger

Available only in builds with `MZ800EMU_CFG_DEBUGGER_ENABLED`.

| Key                        | Action                                                                      |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + D`                  | Show / Hide MZ-800 Debugger window                                          |
| `Alt + B`                  | Show / Hide Breakpoints window                                              |
| `Alt + V`                  | Show / Hide Variables window                                                |
| `Alt + I`                  | Show / Hide I/O Ports window                                                |
