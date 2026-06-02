# Disassembler - samostatné range-based okno s exportem

Samostatné dokovatelné okno pro disassembly libovolného adresového
rozsahu s automaticky generovanými labely (S/L/D/W konvence),
volitelnou integrací symbol databáze a CDL/mhmap dat, a exportem
do `.asm` / `.s` se třemi assembler dialekty (pasmo, sjasmplus,
sdcc-asz80).

**Rozdíl proti sekci Disassembled** ([disassembly.md](disassembly.md)):
sekce Disassembled v hlavním debug okně + sekundární okna #2-#5 jsou
**live debug pohledy** s Follow PC a historií posledních instrukcí.
Disassembler je naproti tomu **statický range pohled** s důrazem na
generování čitelného listingu / assembler-ready zdrojového souboru
pro reverse engineering.

## Otevření okna

Tři ekvivalentní cesty:

- **Klávesová zkratka**: `Alt+Shift+D` (pattern shoda s `Alt+D` pro
  hlavní debug okno)
- **Top menu**: Debugger → Disassembler
- **Iconbar v debug okně**: tlačítko `DASM`

Tooltip nad ikonou DASM v iconbaru zobrazí "Disassembler window
(Alt+Shift+D)".

Default velikost při prvním otevření je 1024x480 px; další otevření
zachovávají poslední velikost/pozici uloženou v ImGui ini.

## Layout okna

```
+------------------------------------------------------------------+
| Disassembler                                                 [X] |
+------------------------------------------------------------------+
| [From: XXXX] [To: XXXX]  Bank: CPU  [Disassemble]    [Heatmap...]|
| External sources: [_] use sym_db (N syms) [Browse...]            |
|                   [_] use CDL/mhmap                              |
+------------------------------------------------------------------+
| Addr  | Bytes        | Label    | Mnemonic                       |
| C000  | F3 31 00 D0  | start:   | di                             |
| C001  |              |          | ld sp,$D000                    |
| C004  | CD 20 C0     |          | call Sc020                     |
| ...                                                              |
| C020  | 3E 00        | Sc020:   | ld a,$00                       |
+------------------------------------------------------------------+
| [Save .asm/.s...] [Copy to clipboard] [Refresh]                  |
| N instr | auto: A | sym: S | warn: W | range: $XXXX-$XXXX        |
|                                       dialect: name  mhmap: <stav>|
+------------------------------------------------------------------+
```

## Top bar (řádek 1)

### From / To (hex inputy)

Rozsah adres k disassembly. 16-bit hex (0000-FFFF), bez prefixu.
Změna libovolného pole označí stav jako "dirty" - listing se přegeneruje
**až po stisku Disassemble** (= žádný auto-update).

### Bank: CPU

V V1 jen indikátor - export pracuje **vždy s CPU view** 0000-FFFF
(= aktuální banking konfigurace). Per-bank export přijde v budoucí
verzi.

### Disassemble button

Spustí dvouprůchodový auto-label scanner přes zadaný rozsah:
- **Pass 1**: zmapuj všechny start adresy instrukcí
- **Pass 2**: detekuj cíle skoků / volání / data referencí
- **Pass 3** (jen pokud `use CDL/mhmap` ON): detekuj data zóny

Výsledek se zobrazí v tabulce dole.

### Heatmap... button (vpravo)

Vždy viditelný (nezávisle na stavu `use CDL/mhmap`). Otevírá
Memory Heatmap okno - rychlý přístup ke konfiguraci mhmap nahrávání
(OFF / WITH_WINDOW / ALWAYS) a vizualizaci counterů.

## External sources (řádek 2)

**Klíčové UX pravidlo**: oba zdroje (sym_db i CDL/mhmap) jsou
**defaultně OFF** a uživatel je explicitně zapne podle situace.
Důvod: implicitní použití by způsobovalo false hits při:

- disassembly snapshotu z jiného běhu (CDL counters z jiné session)
- ROM monitor + herní symboly v sym_db (kolize jmen)
- raw memory dump bez kontextu k aktuální session

### Checkbox `use sym_db`

Pokud ON, existující symboly v Symbol Browser ([symbols.md](symbols.md))
**přepíší** auto-generated labely. Symbol z `.lbl` / `.noi` / `.map` /
`.sym` souboru s priority pořadím (LBL > MAP > NOI > SJASMPLUS).

Vedle checkboxu informativní text:
- `(off)` když OFF
- `(N syms in range)` když ON - počet symbolů v rámci aktuálního
  range From-To

Tlačítko `Browse...` otevře okno Symbols ([symbols.md](symbols.md))
pro správu symbolů (load / save / edit).

### Checkbox `use CDL/mhmap`

Pokud ON, data z Memory Heatmap ([memory-heatmap.md](memory-heatmap.md))
slouží k detekci data zón v rozsahu. Bajt považujeme za "data" pokud
`exec counter == 0 && read counter > 0` (byl čten, ale nikdy
neexekutován).

Souvislé data zóny se v listingu i v exportu renderují jako `DB ...`
(per-dialect: `DB`/`DB`/`.byte`) místo disassembly jako kód.

Stav mhmap nahrávání se zobrazuje v status baru dole (vpravo):
- žluté `mhmap: OFF (no data)` - mhmap je vypnutý, žádná data
- disabled `mhmap: WITH_WINDOW` / `mhmap: ALWAYS` - aktivní mód

## Listing tabulka

| Sloupec | Obsah |
|---------|-------|
| Addr | Hex adresa instrukce / data zóny |
| Bytes | Až 4 bajty opcode (max šířka Z80 instrukce) |
| Label | Auto-label nebo sym_db jméno (viz Auto-labely) |
| Mnemonic | Z80 mnemonika s substituovanými labely |

Pro velké rozsahy (až celých 64 KB) tabulka používá ImGuiListClipper
pro vysoký výkon (jen viditelné řádky se renderují).

### Auto-labely

Konvence prefix + lowercase hex adresa (např. `Sc020`):

| Prefix | Typ | Zdroj |
|--------|-----|-------|
| `S` | Subroutine | `CALL nn`, `CALL cc,nn`, `RST n` |
| `L` | Label | `JP nn`, `JP cc,nn`, `JR d`, `JR cc,d`, `DJNZ d` |
| `D` | Data ref | Paměťové reference (in/out cíl) |
| `W` | Warn | Cíl skoku padá doprostřed jiné instrukce |

**Priorita konfliktů** (vyšší vyhraje při kolizi na adrese):
`WARN (4) > JUMP (3) > SUBROUTINE (2) > DATA (1)`.

**WARN** label označuje pravděpodobně self-modifying code nebo
špatně určený rozsah (= cíl skoku neodpovídá hranicím instrukcí).

**sym_db override**: pokud je `use sym_db` ON a sym_db obsahuje
jméno pro cílovou adresu, auto-label se přepíše. Sym_db labely jsou
v listingu vizuálně odlišené barvou.

### Pravý klik na řádek - kontext menu

| Položka | Akce |
|---------|------|
| Set as PC | Nastav PC registr na adresu řádku |
| Set Breakpoint | Přidej execution breakpoint na adresu |
| Copy address | Zkopíruj `%04X` hex string adresy do clipboardu |
| Copy mnemonic | Zkopíruj plný text instrukce do clipboardu |
| Focus to target | Pokud má řádek staticky známý cíl (JP/JR/CALL), přesuň range na target (range_from = target, range_to = target+0x100) a re-disassembluj |

## Save dialog

Vyvolán tlačítkem `Save .asm/.s...` v dolní liště nebo zkratkou
`Ctrl+S` (když je okno fokusováno).

### Volba dialektu

| Radio | Hex | ORG | DB/DW | Label | IX/IY | Suffix |
|-------|-----|-----|-------|-------|-------|--------|
| pasmo | `0CEh` | `ORG` | `DB`/`DW` | `Sc020:` | `(IX+d)` | `.asm` |
| sjasmplus (default) | `$CE` | `ORG` | `DB`/`DW` | `Sc020:` | `(IX+d)` | `.asm` |
| sdcc-asz80 | `0xCE` | `.org` | `.byte`/`.word` | `Sc020::` | `(d,IX)` (Motorola) | `.s` |

Při změně radio se automaticky aktualizuje přípona v poli Path
(pouze pokud user nezadal vlastní explicitní cestu).

### Options checkboxy

- **include ORG directive** - emituj `ORG 0XXXXh` / `ORG $XXXX` /
  `.org 0xXXXX` na začátku souboru (default ON)
- **include bytes as comments** - za každou instrukcí v komentáři
  výpis hex bajtů (default ON, pomáhá verifikaci round-trip)
- **uppercase mnemonics** - `LD A,$00` místo `ld a,$00` (default OFF)

### Path field + Browse

Cesta k cílovému souboru. Pole je editovatelné ručně, nebo přes
tlačítko `Browse...` které otevře ImGuiFileDialog s file picker
(filter dle dialektu: `.asm,.txt,.*` pro pasmo/sjasmplus, `.s,.txt,.*`
pro sdcc).

### Save / Cancel

`Save` zapíše soubor, ukáže info / error hlášku přes message bar.
`Cancel` (nebo `Esc`) zavře dialog bez zápisu.

Chybové hlášky:
- "Cannot open file - check path and permissions" (open fail)
- "Write failed - disk full or I/O error"
- "Invalid range (From > To)"
- "Selected dialect not yet implemented" (rezervováno pro budoucí
  dialekty mimo F4-F6)

## Copy to clipboard

Stejný výstup jako Save, ale do systémového clipboardu. Default
dialekt je current `Save dialect` (= co je nastaveno v Save dialog
state). Použij pokud chceš rychle paste do editoru bez ukládání
souboru.

## Refresh button

Vynutí re-disassembly aktuálního rozsahu (= alternative k Disassemble,
když chceš znovu spustit scan bez změny From/To). Klávesová zkratka
`F5`.

## Status bar (dole)

Jednořádkový status se separátory `|`:

```
N instr | auto: A | sym: S | warn: W | range: $XXXX-$XXXX | dialect: name        mhmap: <stav>
```

- **N instr** - počet disassemblovaných instrukcí v rozsahu
- **auto** - počet auto-generated labelů (S/L/D bez sym_db override)
- **sym** - počet labelů z sym_db (jen pokud `use sym_db` ON)
- **warn** - počet WARN labelů; **červeně** pokud > 0
- **range** - aktuální rozsah s `$` prefixem
- **dialect** - jméno aktuálně zvoleného save dialektu
- **mhmap** (vpravo zarovnaný) - stav mhmap nahrávání; zobrazí se
  jen pokud `use CDL/mhmap` ON. Žlutě `OFF (no data)` pokud
  `mhmap_mode == OFF`, jinak disabled mode label.

## Klávesové zkratky

| Zkratka | Akce |
|---------|------|
| `Alt+Shift+D` | Toggle okna (otevřít / zavřít) |
| `Ctrl+S` | Otevři Save dialog (okno musí být focused) |
| `F5` | Refresh (re-disassemble) |
| `Esc` | V Save dialog: zavři dialog bez uložení |

## Persistence

Okno si pamatuje konfiguraci napříč restarty emulátoru. Persistence
přes sekci `[DASM_WINDOW]` v `mz<N>.cfg`:

| Klíč | Typ | Default |
|------|-----|---------|
| `range_from` | uint16 | 0x0000 |
| `range_to` | uint16 | 0x00FF |
| `use_symdb` | bool | false |
| `use_cdl` | bool | false |
| `save_dialect` | int (0/1/2) | 1 (sjasmplus) |
| `save_include_org` | bool | true |
| `save_include_bytes` | bool | true |
| `save_uppercase` | bool | false |
| `save_path` | string | `disasm.asm` |

ImGui ini ukládá zvlášť pozici a velikost okna.

## Reference

- [disassembly.md](disassembly.md) - sekce Disassembled v hlavním
  debug okně (Follow PC, history, sekundární okna #2-#5)
- [symbols.md](symbols.md) - Symbol Browser, formáty .noi / .map /
  .sym (sjasmplus i pasmo) / .lbl
- [memory-heatmap.md](memory-heatmap.md) - Memory Heatmap (mhmap)
  počítadla R/W/X pro CDL detekci
- [breakpoints/](breakpoints/) - smart breakpoints (context menu
  "Set Breakpoint" cílem)

## Externí reference assemblerů

- [pasmo](https://pasmo.speccy.org/) - Julián Albo, classic Z80
  assembler
- [sjasmplus](https://github.com/z00m128/sjasmplus) - aktivní fork
  sjasm, Spectrum/MSX/Next community standard
- [sdcc / sdasz80](https://sdcc.sourceforge.net/) - SDCC toolchain
  pro Z80, syntaxe ASxxxx (Motorola-style IX/IY)
