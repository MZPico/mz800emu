# CPU window - Z80 registers in a standalone floating window

**CPU Registers** is a standalone dockable ImGui window that displays
the live state of the Z80 CPU - main and alternate register file, index
registers, flags, control state (I, R, IFF1, IFF2, IM) and a section
with cycle/raster information. Most values are editable (click = pause
the emulator + in-place edit).

## Opening / closing

- **Menu:** Debugger -> CPU Registers
- **Keyboard shortcut:** Alt+R (toggle visibility)
- **Default visibility:** closed at emulator startup. The user opens
  it explicitly via the menu or shortcut. After the first open, the
  window position and size are persisted via `imgui.ini`.

## Layout

A standalone floating window with auto-fit sizing based on its content.
Manual resize is disabled - the window adapts to the content (the
table layout has fixed columns).

```
+----------- CPU Registers ----------+
| F: [s][z][5][h][3][P][n][c]        |
|                                    |
| > AF  : 0604    > AF' : 0000       |
| > HL  : E001    > HL' : 0000       |
| > DE  : 0005    > DE' : 0000       |
| > BC  : 0DFF    > BC' : 0000       |
| > PC  : E6C4    > IX  : CEE9       |
| > SP  : 10E4    > IY  : 0000       |
| > R   : 6F      > I   : 00         |
|   IFF1: ON        IM  : [1 v]      |
|   IFF2: ON                         |
|                                    |
| VECA: FE34    ISRA: 1234           |   (MZ-800 / MZ-1500 only)
| VECB: FE36    ISRB: 5678           |
+------------------------------------+
| v Cycles & raster                  |
| Frame: 534  Line: 0  Col: 12       |
|                                    |
| Frame cyc : 0                      |
| Total cyc : 37853340               |
| User cyc  : 37853340  [Set0]       |
+------------------------------------+
```

### Flag breakdown F

The row above the register table shows the 8 bits of register F:
**S Z 5 H 3 P N C** (bit 7..0).

- Large golden symbol = bit set (1)
- Small grey symbol = bit cleared (0)
- Hover shows a tooltip explaining the flag

In paused mode, clicking a bit toggles the corresponding bit in the F
register. In running mode, a click triggers a silent pause (without
a modal dialog) - the bit is not toggled at that moment, the user has
to click again once in the paused state.

### Register table

Table layout (4+4 columns + separator). Order:

- **Left half:** AF, HL, DE, BC, PC, SP, R, IFF1, IFF2
- **Right half:** AF', HL', DE', BC', IX, IY, I, IM

The 8-bit registers R and I are on separate rows. IFF1 / IFF2 are
displayed as `ON` / `OFF`, IM as a combo `0 / 1 / 2`.

### PIO-Z80 VEC/ISR (MZ-800 / MZ-1500 only)

Below the register table, two rows for IM 2 interrupt vectoring are
shown on MZ-800 and MZ-1500 (MZ-700 has no such section - it has no
PIO-Z80):

- **VECA** = `(I << 8) | (interrupt_vector_A & 0xFE)`
- **VECB** = `(I << 8) | (interrupt_vector_B & 0xFE)`
- **ISRA** = `MEM[VECA] | (MEM[VECA+1] << 8)`
- **ISRB** = `MEM[VECB] | (MEM[VECB+1] << 8)`

VEC* is the address of the two-byte cell in the ISR vector table (= the
location pointed to by the Z80 during an IM 2 cycle). ISR* is the
dereference of that cell (= the target address of the ISR routine).

Editing:

- **VEC*** is written atomically in two steps: the high byte = the new
  I, the low byte = the new value of the interrupt vector for the given
  PIO-Z80 port (mask `& 0xFE`, bit 0 is always 0 per specification).
  A change of I is propagated to both VEC* values (they share the
  high byte).
- **ISR*** writes 2 bytes (little-endian) to memory at address VEC*.
  If any of the affected addresses lies in a non-writable region
  (ROM, CG-ROM, VRAM I/II on MZ-800, memory-mapped ports E000-E00F,
  prohibited region, unmapped), no byte is written and a modal warning
  is opened with the address and region description. The writable
  regions are RAM, VRAM_TEXT, CGRAM and PCG banks.

## Focus button `>` (navigation, does NOT pause)

A `>` button precedes each 16-bit register pair.

- **Left click** = focus the main Disassembly at address = register
  value. If the main debug window is closed, it opens. Does not pause
  emulation - navigation works even during execution.
- **Right click** = popup with the following options:
  - Focus in Disassembly (main) / Focus in Disassembly #2..#5
  - Toggle format (cycle hex -> dec -> bin)
  - Add to Watch $<name> = $<hex>
  - Add breakpoint at <reg> ($<hex>)
  - Copy hex / Copy dec / Copy bin

For the 8-bit virtual registers I and R (= not 16-bit addresses) the
popup only offers Copy hex / dec / bin and Toggle format. Focus, Watch
and Add breakpoint are not shown (they do not make sense for an 8-bit
value).

### Add to Watch limitation

The Watch entry is a **static snapshot** at the moment of insertion -
the value is not refreshed live when the register changes. There is
currently no dedicated subsystem for true live tracking.

## Editing a register value

Click on a register value:

- **In paused mode** = opens an in-place InputText over the value.
- **In running mode** = silent auto-pause (= pause without a modal info
  dialog) + immediate opening of the InputText. No "second click
  required".

Keys in edit mode:

- **Enter** - parse the value according to the current format, validate
  the range, write the value. On invalid input, the edit mode stays
  open (no modal dialog) - the user can correct it.
- **Esc** - cancel without changes.
- **Tab** - apply + jump to the next register in order:
  AF, BC, DE, HL, IX, IY, SP, PC, AF', BC', DE', HL', R, I.
- **Click outside the InputText** - cancel.

If the emulator transitions to the running state while an edit is open
(Play / step / breakpoint resume), the edit is automatically cancelled.

### Per-register format

Cycle via the **Toggle format (cur -> next)** item in the RMB popup
over the `>` button. Formats:

- **HEX** (default): 4 characters in uppercase, no prefix (`042A`).
- **DEC**: unsigned 0..65535.
- **BIN**: 16-bit split by a space into two 8-bit groups
  (`00000100 00101010`).

The format is session-only - it does not persist; on emulator restart
it reverts to HEX. The value column width is fixed (5 monospace
glyphs), so switching formats does not cause layout jitter.

### Parser - tolerance and range

Accepted input:

- HEX prefix `0x` / `$` (optional), characters `0-9` `a-f` `A-F`
- DEC sign `+` / `-`, characters `0-9`
- BIN prefix `0b` (optional), characters `0` `1`
- Whitespace, underscore and `_` separators inside the number

Range:

- 16-bit unsigned: 0..65535
- 16-bit signed: -32768..32767 (signed DEC, converted to two's
  complement)
- 8-bit unsigned: 0..255 (R, I)

On overflow or invalid character, parsing returns false and the edit
stays open.

### Editing IFF1 / IFF2 / IM

- **IFF1 / IFF2** are shown as SmallButton `ON` / `OFF`. A click in
  paused mode toggles, in running mode it triggers a silent auto-pause
  + toggle.
- **IM** is a combo with items `0`, `1`, `2`. Changing the value
  silently pauses + writes the new mode.

## Change highlighting (golden fade)

After each refresh tick the values are compared against the previous
ones. A changed register lights up golden for 1.5 s and linearly fades
back to default. The 8-bit virtual registers (I, R) do not have this
fade.

## Cycles & raster section

A default-collapsed section below the register table. Power user
information, collapsed by default (= saves refresh round-trips to the
emu thread).

First row: raster position (`Frame: NNN  Line: NNN  Col: NNN`).

Below it a counter table:

| Column | Meaning |
|--------|---------|
| Frame cyc | T-states in the current frame |
| Total cyc | Cumulative T-states since reset |
| User cyc | Stopwatch - difference `total_cycles - origin` |

**Set0** next to User cyc sets the origin to the current `total_cycles`,
so the display drops to 0. It serves as an interactive stopwatch for
measuring the T-state difference between two points. Edit by clicking
on the User cyc value - parses a decimal and computes the new origin.

The counter is 32-bit; at the normal Z80 speed it overflows after
roughly 20 minutes.

## Refresh cycle

The panel is connected to a centralized refresh controller (interval
100 ms). On a force refresh (pause, step, RESET, action from a context
menu) the update is immediate.

The refresh runs in a single batched round-trip to the emu thread -
per-section gating ensures that a collapsed section does not send data
until it is expanded. The default-collapsed section (Cycles & raster)
does not contribute to the refresh cost until the user opens it.

## Persistence

The configuration file `imgui.ini` keeps the window position. The
visibility flag (open / closed) is session-only - the default state at
startup is closed.

The per-register format, edit history and fade timer are session-only.

## Keyboard shortcuts (summary)

| Shortcut | Action |
|----------|--------|
| Alt+R | Toggle CPU window visibility |
| Enter | Apply value edit |
| Esc | Cancel edit |
| Tab | Apply + jump to next register |

## Related documentation

- [stack-window.md](stack-window.md) - Stack Monitor window
- [breakpoints.md](breakpoints.md) - Breakpoints and conditional BPs
