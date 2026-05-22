# Klávesové zkratky emulátoru MZ-800

Globální klávesové zkratky dostupné napříč celým oknem emulátoru.
Zdroj: `src/ui-imgui/topmenu/global_shortcuts.cpp`.

## Reset a obecné

| Klávesa                    | Akce                                                                        |
|----------------------------|-----------------------------------------------------------------------------|
| `F12`                      | Reset emulovaného počítače                                                  |
| `F11`                      | Kalibrace joysticku (nebo Reset, pokud emulátor běží pod Windows Debugger)  |

## Okno a zobrazení

| Klávesa                    | Akce                                                                        |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + Ctrl + Enter`       | Přepnutí mezi fullscreen a okenním režimem                                  |
| `Alt + W`                  | Opravit poměr stran okna podle šířky                                        |
| `Alt + H`                  | Opravit poměr stran okna podle výšky                                        |

## Rychlost emulace

| Klávesa                    | Akce                                                                        |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + P`                  | Přepnutí Pauza / Spuštěno                                                   |
| `Alt + N`                  | Nastavit normální rychlost emulace (100 %)                                  |
| `Alt + M`                  | Přepínat mezi Max a (Normal nebo Custom) rychlostí emulace                  |
| `Alt + Shift + M`          | Přepínat mezi Custom a (Normal nebo Max) rychlostí emulace                  |
| `Alt + Up`                 | Zvýšit Custom rychlost (krok 1 % normální rychlosti)                        |
| `Alt + Shift + Up`         | Zvýšit Custom rychlost (krok 10 % normální rychlosti)                       |
| `Alt + Pg_Up`              | Zvýšit Custom rychlost (krok 100 % normální rychlosti)                      |
| `Alt + Down`               | Snížit Custom rychlost (krok 1 % normální rychlosti)                        |
| `Alt + Shift + Down`       | Snížit Custom rychlost (krok 10 % normální rychlosti)                       |
| `Alt + Pg_Down`            | Snížit Custom rychlost (krok 100 % normální rychlosti)                      |

## Disketové mechaniky (FDC)

| Klávesa                    | Akce                                                                        |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + [1..4]`             | Připojit FD obraz do mechaniky 1..4 (a aktivovat WD279x)                    |
| `Alt + Shift + [1..4]`     | Odpojit FD obraz z mechaniky 1..4                                           |

## Magnetofon (CMT) a virtuální klávesnice

| Klávesa                    | Akce                                                                        |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + C`                  | Zobrazit / Skrýt okno virtuálního magnetofonu                               |
| `Alt + K`                  | Zobrazit / Skrýt okno virtuální klávesnice                                  |

## Snapshoty

| Klávesa                    | Akce                                                                        |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + F6`                 | Uložit Snapshot (otevře dialog uložení)                                     |
| `Alt + F7`                 | Načíst Snapshot (otevře dialog načtení)                                     |
| `Alt + F8`                 | Rychlé uložení                                                              |
| `Alt + F9`                 | Rychlé načtení                                                              |

## Debugger

Dostupné pouze v buildech s `MZ800EMU_CFG_DEBUGGER_ENABLED`.

| Klávesa                    | Akce                                                                        |
|----------------------------|-----------------------------------------------------------------------------|
| `Alt + D`                  | Zobrazit / Skrýt okno MZ-800 Debuggeru                                      |
| `Alt + B`                  | Zobrazit / Skrýt okno Breakpoints                                           |
| `Alt + V`                  | Zobrazit / Skrýt okno Variables                                             |
| `Alt + I`                  | Zobrazit / Skrýt okno I/O Ports                                             |
