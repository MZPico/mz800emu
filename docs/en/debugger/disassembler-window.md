# Disassembler - standalone range-based window with export

Independent dockable window for disassembling an arbitrary address
range with automatically generated labels (S/L/D/W convention),
optional integration with the symbol database and CDL/mhmap data,
and export to `.asm` / `.s` with three assembler dialects (pasmo,
sjasmplus, sdcc-asz80).

**Difference from the Disassembled section**
([disassembly.md](disassembly.md)): the Disassembled section in the
main debug window plus secondary windows #2-#5 are **live debug views**
with Follow PC and history of last executed instructions. The
Disassembler in contrast is a **static range view** focused on
generating a readable listing / assembler-ready source file for
reverse engineering.

## Opening the window

Three equivalent paths:

- **Keyboard shortcut**: `Alt+Shift+D` (pattern matching `Alt+D` for
  the main debug window)
- **Top menu**: Debugger → Disassembler
- **Iconbar in the debug window**: button `DASM`

The tooltip over the DASM icon in the iconbar shows "Disassembler
window (Alt+Shift+D)".

The default size on first open is 1024x480 px; subsequent openings
preserve the last size/position stored in the ImGui ini.

## Window layout

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
|                                       dialect: name  mhmap: <state>|
+------------------------------------------------------------------+
```

## Top bar (row 1)

### From / To (hex inputs)

Address range to disassemble. 16-bit hex (0000-FFFF), no prefix.
Changing any field marks the state as "dirty" - the listing is
regenerated **only on Disassemble click** (= no auto-update).

### Bank: CPU

In V1 only an indicator - export always operates on the **CPU view**
0000-FFFF (= the current banking configuration). Per-bank export is
planned for a future version.

### Disassemble button

Runs a two-pass auto-label scanner over the given range:
- **Pass 1**: map all instruction start addresses
- **Pass 2**: detect targets of jumps / calls / data references
- **Pass 3** (only if `use CDL/mhmap` is ON): detect data zones

The result appears in the table below.

### Heatmap... button (right-aligned)

Always visible (independent of the `use CDL/mhmap` state). Opens the
Memory Heatmap window - quick access to mhmap recording configuration
(OFF / WITH_WINDOW / ALWAYS) and counter visualization.

## External sources (row 2)

**Key UX rule**: both sources (sym_db and CDL/mhmap) are **off by
default** and must be explicitly enabled by the user as needed. The
reason: implicit use would cause false hits when:

- disassembling a snapshot from a different run (CDL counters from
  another session)
- ROM monitor + game symbols in sym_db (name collisions)
- raw memory dump without context for the current session

### Checkbox `use sym_db`

When ON, existing symbols in the Symbol Browser
([symbols.md](symbols.md)) **override** auto-generated labels. Symbols
from `.lbl` / `.noi` / `.map` / `.sym` files with priority order
(LBL > MAP > NOI > SJASMPLUS).

Next to the checkbox an informational text:
- `(off)` when OFF
- `(N syms in range)` when ON - number of symbols within the current
  From-To range

The `Browse...` button opens the Symbols window
([symbols.md](symbols.md)) for symbol management (load / save / edit).

### Checkbox `use CDL/mhmap`

When ON, data from the Memory Heatmap
([memory-heatmap.md](memory-heatmap.md)) is used to detect data zones
in the range. A byte is considered "data" if `exec counter == 0 &&
read counter > 0` (it was read but never executed).

Contiguous data zones are rendered as `DB ...` (per-dialect:
`DB`/`DB`/`.byte`) instead of being disassembled as code, both in the
listing and the export.

The mhmap recording state is shown in the status bar below
(right-aligned):
- yellow `mhmap: OFF (no data)` - mhmap is off, no data
- disabled `mhmap: WITH_WINDOW` / `mhmap: ALWAYS` - active mode

## Listing table

| Column | Content |
|--------|---------|
| Addr | Hex address of instruction / data zone |
| Bytes | Up to 4 opcode bytes (max Z80 instruction width) |
| Label | Auto-label or sym_db name (see Auto-labels) |
| Mnemonic | Z80 mnemonic with substituted labels |

For large ranges (up to the full 64 KB) the table uses
ImGuiListClipper for high performance (only visible rows are
rendered).

### Auto-labels

Convention: prefix + lowercase hex address (e.g. `Sc020`):

| Prefix | Type | Source |
|--------|------|--------|
| `S` | Subroutine | `CALL nn`, `CALL cc,nn`, `RST n` |
| `L` | Label | `JP nn`, `JP cc,nn`, `JR d`, `JR cc,d`, `DJNZ d` |
| `D` | Data ref | Memory references (in/out target) |
| `W` | Warn | Jump target falls in the middle of another instr |

**Conflict priority** (higher wins on the same address):
`WARN (4) > JUMP (3) > SUBROUTINE (2) > DATA (1)`.

A **WARN** label marks likely self-modifying code or an incorrect
range (= jump target does not align with instruction boundaries).

**sym_db override**: if `use sym_db` is ON and sym_db contains a name
for the target address, the auto-label is overridden. Sym_db labels
are visually distinguished by color in the listing.

### Right-click on a row - context menu

| Item | Action |
|------|--------|
| Set as PC | Set the PC register to the row's address |
| Set Breakpoint | Add an execution breakpoint at the address |
| Copy address | Copy `%04X` hex string of address to clipboard |
| Copy mnemonic | Copy full instruction text to clipboard |
| Focus to target | If the row has a statically known target (JP/JR/CALL), move the range to that target (range_from = target, range_to = target+0x100) and re-disassemble |

## Save dialog

Triggered by the `Save .asm/.s...` button in the bottom bar or by the
`Ctrl+S` shortcut (when the window is focused).

### Dialect selection

| Radio | Hex | ORG | DB/DW | Label | IX/IY | Suffix |
|-------|-----|-----|-------|-------|-------|--------|
| pasmo | `0CEh` | `ORG` | `DB`/`DW` | `Sc020:` | `(IX+d)` | `.asm` |
| sjasmplus (default) | `$CE` | `ORG` | `DB`/`DW` | `Sc020:` | `(IX+d)` | `.asm` |
| sdcc-asz80 | `0xCE` | `.org` | `.byte`/`.word` | `Sc020::` | `(d,IX)` (Motorola) | `.s` |

When the radio is changed, the extension in the Path field is
automatically updated (only if the user did not enter a custom
explicit path).

### Options checkboxes

- **include ORG directive** - emit `ORG 0XXXXh` / `ORG $XXXX` /
  `.org 0xXXXX` at the start of the file (default ON)
- **include bytes as comments** - after each instruction list the
  hex bytes as a comment (default ON, helps verifying round-trip)
- **uppercase mnemonics** - `LD A,$00` instead of `ld a,$00` (default
  OFF)

### Path field + Browse

Path to the target file. Editable manually, or via the `Browse...`
button which opens an ImGuiFileDialog file picker (filter by dialect:
`.asm,.txt,.*` for pasmo/sjasmplus, `.s,.txt,.*` for sdcc).

### Save / Cancel

`Save` writes the file, shows an info / error message in the message
bar. `Cancel` (or `Esc`) closes the dialog without writing.

Error messages:
- "Cannot open file - check path and permissions" (open fail)
- "Write failed - disk full or I/O error"
- "Invalid range (From > To)"
- "Selected dialect not yet implemented" (reserved for future
  dialects beyond F4-F6)

## Copy to clipboard

Same output as Save, but to the system clipboard. The default dialect
is the current `Save dialect` (= whatever is set in the Save dialog
state). Use this for a quick paste into an editor without saving a
file.

## Refresh button

Forces a re-disassembly of the current range (= alternative to
Disassemble when you want to re-run the scan without changing
From/To). Keyboard shortcut `F5`.

## Status bar (bottom)

Single-line status with `|` separators:

```
N instr | auto: A | sym: S | warn: W | range: $XXXX-$XXXX | dialect: name        mhmap: <state>
```

- **N instr** - count of disassembled instructions in the range
- **auto** - count of auto-generated labels (S/L/D without sym_db
  override)
- **sym** - count of labels from sym_db (only if `use sym_db` ON)
- **warn** - count of WARN labels; **red** if > 0
- **range** - current range with `$` prefix
- **dialect** - name of the currently selected save dialect
- **mhmap** (right-aligned) - mhmap recording state; only displayed
  if `use CDL/mhmap` ON. Yellow `OFF (no data)` if
  `mhmap_mode == OFF`, otherwise disabled mode label.

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| `Alt+Shift+D` | Toggle the window (open / close) |
| `Ctrl+S` | Open Save dialog (window must be focused) |
| `F5` | Refresh (re-disassemble) |
| `Esc` | Inside Save dialog: close dialog without saving |

## Persistence

The window remembers its configuration across emulator restarts.
Persistence via the `[DASM_WINDOW]` section in `mz<N>.cfg`:

| Key | Type | Default |
|-----|------|---------|
| `range_from` | uint16 | 0x0000 |
| `range_to` | uint16 | 0x00FF |
| `use_symdb` | bool | false |
| `use_cdl` | bool | false |
| `save_dialect` | int (0/1/2) | 1 (sjasmplus) |
| `save_include_org` | bool | true |
| `save_include_bytes` | bool | true |
| `save_uppercase` | bool | false |
| `save_path` | string | `disasm.asm` |

The ImGui ini stores window position and size separately.

## References

- [disassembly.md](disassembly.md) - Disassembled section in the
  main debug window (Follow PC, history, secondary windows #2-#5)
- [symbols.md](symbols.md) - Symbol Browser, formats .noi / .map /
  .sym (sjasmplus and pasmo) / .lbl
- [memory-heatmap.md](memory-heatmap.md) - Memory Heatmap (mhmap)
  R/W/X counters for CDL detection
- [breakpoints/](breakpoints/) - smart breakpoints (context menu
  "Set Breakpoint" target)

## External assembler references

- [pasmo](https://pasmo.speccy.org/) - Julián Albo, classic Z80
  assembler
- [sjasmplus](https://github.com/z00m128/sjasmplus) - active sjasm
  fork, Spectrum/MSX/Next community standard
- [sdcc / sdasz80](https://sdcc.sourceforge.net/) - SDCC toolchain
  for Z80, ASxxxx syntax (Motorola-style IX/IY)
