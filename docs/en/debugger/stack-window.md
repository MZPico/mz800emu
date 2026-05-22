# Stack Monitor window - hex dump around SP + stack regions

**Stack Monitor** is a standalone dockable ImGui window that shows the
raw contents of stack memory around the current Z80 SP, augmented with
a register of named stack regions with low-water mark tracking, SP
history and a heuristic return-address decoder.

**It is NOT a callstack** (= reconstruction of CALL/RET history); it is
a view of what the Z80 SW actually uses as a stack in memory.

The standalone **Stack History** window shows the SP history sparkline
at a larger scale (= detailed history of SP over time).

## Opening / closing

- **Menu:** Debugger -> Stack Monitor
- **Keyboard shortcut:** Alt+S (toggle visibility)
- **Default visibility:** closed at emulator startup. The user opens it
  explicitly via the menu or shortcut. The window position and size are
  persisted via `imgui.ini`.

The related standalone Stack History window:

- **Menu:** Debugger -> Stack History
- **Keyboard shortcut:** Alt+Shift+H
- Active only when the `SP history` checkbox in the main Stack Monitor
  window is enabled. It shares state (selected sample, Show events
  toggle) with the main window.

## Layout

```
+--- Stack Monitor ----------------------------------------+
| SP: 10D2  Depth: 30 B  Region: [system v] [Reset W]      |
| [Set BP from SP-256]  [+ Add region from current SP]     |
| [x] SP history   [x] Lock SP center   [stack creep]      |
+-------------------+--------------------------------------+
| Addr  Byte  Word  Decode      | Regions                  |
| 10F0  ..    ..    --          | Name             Trend   |
| 10EE  CD 4A CD4A  [ret CALL]  | system    [sparkline]    |
| 10EC  80 40 4080  --          | Base Limit SP%  Min Act  |
| ...                           | 10F0 1000  18%  10C4 RX  |
| 10D2  AB CD ABCD  > --        |                          |
| 10D0  .. ..       = --        | game        ...          |
| ...                           |                          |
+-------------------+--------------------------------------+
| v SP history                                             |
| [sparkline plot full width                              ]|
| Samples 1234  First 10F0  Last 10C8  Slope -1.23e-5      |
+----------------------------------------------------------+
```

Hex dump table columns:

| Column | Meaning |
|--------|---------|
| Addr | Row address, 4 hex characters |
| Byte | Raw bytes (2 in word mode, 1 in byte mode) |
| Word | LE word (= byte + next), displayed as a return-addr candidate. Disabled (`--`) in byte mode |
| Decode | Heuristic decode of the return-address candidate (CALL/RST). Clickable. |
| M | Marker `>` on the current SP row, `=` on the selected region's watermark. `>` takes precedence if SP == watermark. |

Sticky header:

| Field | Meaning |
|-------|---------|
| SP | Current Z80 SP value (with `h` suffix for readability) |
| Depth | `base - sp_now` if SP is in the selected region, otherwise `--` |
| Region dropdown | Combo with the list of all defined regions + `(none)` |
| Reset W | Clears the watermark + push/pop counters of the selected region. Disabled when no region is selected. |
| Set BP from SP-256 | Quick action - creates an execution BP of type SP_THRESHOLD with threshold = current_sp - 256 |
| + Add region from current SP | Opens a modal for defining a new region |
| SP history | Toggle recording of SP history into a ring buffer |
| Lock SP center | Toggles the hex dump layout between the default 32/8 split (SP in the lower third) and a 20/20 split (SP in the middle) |
| [stack creep] | Orange warning text - visible only when the detector finds a slowly decreasing SP (memory leak on the stack) |

### Regions side panel

A per-region "card" with two mini-grids:

```
+-----------------------------------+
| Name              | Trend         |
+-----------------------------------+
| <region_name>     | <sparkline>   |
+-----------------------------------+
| Base | Limit | SP% | Min | Act    |
+-----------------------------------+
| XXXX | XXXX  | NN% | XXX | [R][X] |
+-----------------------------------+
```

| Field | Meaning |
|-------|---------|
| Name | Region name. Click on the row = select the region (sync with the dropdown) |
| Trend | Mini sparkline filtered to samples where SP lay inside the region. Tooltip shows the count of filtered samples. |
| Base | Region top (highest address) |
| Limit | Region bottom (lowest allowed address) |
| SP% | `(base - sp_now) * 100 / (base - limit)` if SP is in the region, otherwise `--` |
| Min | Watermark = lowest recorded SP |
| Act | `R` (reset watermark + counters), `X` (delete region) |

### Splitters

The window layout contains two splitters:

- **Vertical splitter** between the hex table and the Regions panel
  (cursor `ResizeEW` on hover). Dragging changes the width ratio.
- **Horizontal splitter** between the top section (hex + regions) and
  the bottom section (SP history sparkline). Cursor `ResizeNS`.
  Dragging changes the height of both sections.

Splitter positions are session-only (they reset at restart).

## Stack regions

Named records `<base, limit>` with runtime statistics (watermark,
push/pop counters). Max 8 regions. They serve to delineate areas of
memory that the program uses as stack(s) - CP/M BIOS typically has 3
stacks (STCK0/STCK1/STCK3 + application), various sub-systems may have
their own stacks, etc.

### Watermark and counters

For each region the following is tracked:

- **watermark** - the lowest SP ever reached in the region (= how deep
  the stack went inside the region)
- **push_count** - count of PUSH-like events (SP decreased by 2).
  Includes PUSH, CALL, RST and INT acknowledge (= implicit PUSH PC).
- **pop_count** - count of POP-like events (SP increased by 2): POP,
  RET.
- SP changes that are not ±2 (`LD SP,nn`, `EX (SP),HL`, `INC SP`,
  `DEC SP`) do not update the counters, but the watermark is always
  updated.

The default OFF state (no region defined) = zero overhead in the
emulator's hot path. The active state = one SP change check + lookup
into at most 8 slots.

### `=` watermark marker

In the hex dump table, on the row corresponding to the watermark of the
**selected** region, an `=` character is shown in column M. If SP ==
watermark, the `>` marker (= SP) takes precedence.

### Reset semantics

- **Emu reset** - the region definitions are preserved, the watermark
  + counters of all regions are zeroed (= "start over from the
  beginning").
- **Reset W button** (header) - clears the watermark + counters of the
  selected region only.
- **R button** (Act column in the side panel) - the same for the given
  region.
- **X button** (side panel) - removes the region entirely.

## Add region modal

The **+ Add region from current SP** button opens a dialog:

- **Name** - text input, default `region_N` where N = first free index.
  Validation: non-empty, max 31 characters, characters `[a-zA-Z0-9_]+`.
- **Base (hex)** - 4 hex characters, default = current SP.
- **Limit (hex)** - 4 hex characters, default = base - 256.

UI validation gives quick feedback (name, hex format, `base > limit`).
On successful confirmation, the new region is auto-selected in the
dropdown and the side panel. On failure, the dialog stays open with an
error message.

## Set BP from SP-256

A quick action in the sticky header for a quick safety net "if the
stack drops more than 256 B below the current value, stop":

1. `threshold = current_sp - 256` (a 16-bit wrap at SP < 256 is kept
   as-is; the user sees the value and can edit it in the BP edit
   panel)
2. Creates an execution breakpoint of type **SP_THRESHOLD** with the
   threshold set and SINGLE mode (default semantics "fire when SP <
   threshold")

The user can switch to WINDOW mode in the BP edit panel, change values,
etc. The button is disabled if the cache has not been populated yet
(cold start or emu offline).

## Decode column (return-address heuristic)

For each row of the hex dump table:

1. Take the word `W = mem[addr] | (mem[addr+1] << 8)` (LE).
2. Test the opcode at address `W-3`:
   - `0xCD` = `CALL nn` (3 bytes); target = `mem[W-2] | (mem[W-1] << 8)`
   - `0xC4 / 0xCC / 0xD4 / 0xDC / 0xE4 / 0xEC / 0xF4 / 0xFC` =
     `CALL cc,nn` (3 bytes, conditional)
3. If none of the above: test the opcode at address `W-1`:
   - `0xC7 / 0xCF / 0xD7 / 0xDF / 0xE7 / 0xEF / 0xF7 / 0xFF` =
     `RST n` (1 byte); target = `opcode & 0x38`
4. Otherwise NONE - the column shows `--`.

Decode formats:

- `[ret CALL XXXX]` - unconditional CALL
- `[ret CALL cc XXXX]` - conditional CALL (the specific condition is
  not stated)
- `[ret RST XX]` - RST n
- `--` - NONE or byte-mode (odd SP)

### Clickable Decode

- **LMB click** = focus primary Disassembly at the target address. Same
  as the `>` button in the CPU panel - **without pausing**. If the main
  debug window is closed, it opens.
- **RMB click** = popup with options:
  - Focus in Disassembly (main) / Focus in Disassembly #2..#5
  - Copy target hex
- **Hover** = tooltip with the opcode value, the target address and the
  note "Possible return address (heuristic). Click: focus primary
  Disassembly. Right-click: choose slot or copy."

Rows without decode info (`--` or byte-mode) remain unclickable.

### Heuristic limitations

- **False positives** - any data on the stack that happens to have a
  CALL/RST opcode 3 / 1 bytes before the target produces a matching
  decode. Without a callstack feature it is impossible to 100%
  distinguish a "true return" from coincidence. The decode is therefore
  marked as a heuristic in the tooltip.
- **3-byte CALL ambiguity** - `CALL nn` (0xCD) vs `CALL cc,nn`
  (C4..FC) are both 3 bytes with the same target offset. The UI shows
  the type (CALL vs CALL cc) but does not show the specific condition
  (NZ/Z/NC/...).
- **IM2 vector dispatch** - in IM 2 mode the CPU pushes the return
  address without an explicit CALL instruction in memory. Such slots
  remain NONE.
- **Byte-mode (odd SP)** - the word candidate is not defined, the
  decode column is always `--`.

## SP history sparkline

A ring buffer of 4096 `{cycles, sp}` samples. A sample is recorded **on
every SP change**. Recording is enabled via the `SP history` checkbox
in the sticky header.

- Default OFF = zero overhead.
- When enabled, the flag persists into `.ini`, so it is preserved
  across restarts.

### Plot

A sparkline below the hex table (default expanded). It plots:

- The SP curve over time (X = sample index, Y = SP value)
- Optional vertical push / pop / other event markers (toggle
  **Show events** checkbox)
- A hover crosshair with a tooltip (idx, SP, cycle, delta from the
  previous sample)
- LMB click on a sample = persistent yellow crosshair for the selected
  sample; clicking elsewhere or the Clear button removes it

Info text below the plot:

- `Samples N` (first / last SP, first / last cycle)
- `Slope X.XXe-Y` - linear regression of SP vs cycles over the last 256
  samples
- `Selected: idx=N SP=XXXX [Clear]` - visible only when a selection is
  active

### Stack creep detection

If the `slope` from the linear regression drops below the threshold and
the buffer contains at least 256 samples, an orange `[stack creep]`
text lights up in the sticky header with a tooltip showing the current
slope value - it indicates a slowly decreasing SP (asymmetric
PUSH/POP, stack leak).

### Emu reset

On emulator reset, the ring buffer is emptied and the sparkline returns
to the empty state. The recording flag is preserved.

### Stack History window

A standalone floating window (Alt+Shift+H or Debugger -> Stack History)
shows the same sparkline at the size of the whole window (the plot
resizes with the window in both X and Y). Default size 800x300 px.

- Shared state with the main section - selected sample / Show events /
  recording flag is propagated between windows.
- When recording is off, the window shows the hint "(SP history is
  disabled, enable in Stack Monitor)" instead of the plot.
- Window visibility persists in `.ini`.

## Lock SP center

Toggle in the sticky header (checkbox `Lock SP center`):

- **Default OFF** - the hex dump table has an asymmetric layout: 32
  rows above SP and 8 below (SP in the lower third). Suitable for
  observing the already-used stack.
- **ON** - symmetric 20/20 split (SP in the middle). Suitable when you
  want to see deep pushes and the space below SP roughly equally.

The value persists into `.ini`.

## Color highlighting

### Per-row background by region

A hex dump table row that falls into one of the stack regions has a
blue background:

- **Active region** (= selected in the dropdown): saturated tint
- **Other regions**: subdued (roughly half alpha)
- **Outside regions**: default RowBg (no highlight)

If the dropdown shows `(none)`, all regions are shown subdued - the
user still sees where they lie, even without an active selection.

### Word fade highlight

When a Word value changes (= memory write at that address), the cell
briefly turns golden and linearly fades over 1.5 s back to default.
Works **only in word mode** and **only for real memory writes** -
auto-scroll and SP move do not trigger the fade (the row would show a
different address, so the value "changed" only visually).

Multiple writes to the same cell during the fade effect = the fade
restarts.

## CDL "S" classification in Memory Heatmap

The Memory Heatmap (standalone window) has, in addition to categories
`R/W/X`, a category `S` (Stack write). The classification is
region-based:

- If at least one stack region is defined and a write targets the range
  `<region.limit..region.base>`, the heatmap records the event as **S**
  instead of W.
- `S` and `W` are mutually exclusive for a single write event.

In the Memory Heatmap window:

- Filter bar - 5th radio button `[S]` (S-only monochrome mode)
- Checkbox `Show S` - toggles visibility of the S category in RGB
  blending and threshold testing
- Color popup - ColorEdit3 for the S counter (default cyan)
- Per-cell hover tooltip + Selected cell sidebar: `S = N (X.XX%)`
- Region statistics: `Total S` + `Max S`

The default OFF state (no region defined) = the `S` counter stays 0
across all of memory, the behavior is identical to the pre-S version.

## Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| Default lines above SP (asymmetric) | 32 | Rows above SP - default layout |
| Default lines below SP (asymmetric) | 8 | Rows below SP - default layout |
| Total lines | 40 | Total number of rows in the hex table |
| Stack regions max | 8 | Maximum number of defined regions |
| Region name max | 31 | Max name length (without '\0') |
| SP history ring | 4096 | Ring buffer sample count |
| Word fade duration | 1.5 s | Length of the golden fade on memory write |
| Refresh interval | 100 ms | Window update frequency |

## Persistence

Configuration file `cfgmain.ini`:

### Section `[STACK_REGIONS]`

| Key | Type | Description |
|-----|------|-------------|
| `count` | unsigned | Count of defined regions (0..8) |
| `region_<i>_name` | text | Name of region i (i=0..7) |
| `region_<i>_base` | unsigned | Base address of region i |
| `region_<i>_limit` | unsigned | Limit address of region i |

The watermark, push/pop counters and SP history ring buffer are NOT
persisted (runtime state, reset at emu startup).

### Section `[STACK_HISTORY]`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `enabled` | bool | 0 | SP history recording flag |

### Section `[STACK_PANEL]`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `lock_sp_center` | bool | 0 | Lock SP center toggle |
| `show_regions_window` | bool | 0 | Visibility of the standalone Regions window |
| `show_history_window` | bool | 0 | Visibility of the standalone Stack History window |

### Memory heatmap - section `[DEBUGGER]` (S classification)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `mhwindow_color_s_rgba` | unsigned | cyan | RGBA color of the S category |
| `mhwindow_show_s_category` | bool | 1 | Toggle Show S in the heatmap UI |
| `mhwindow_color_mode` | keyword | RGB | Extended with the value `S` |

### Lifecycle

- **Load** at emulator startup: regions are populated with full
  validation (fail-soft for invalid records - the value is silently
  ignored, the others continue).
- **Save** at emulator shutdown. No autosave on ADD/REMOVE/RESET of a
  region - this is an acceptable loss risk between exit and the real
  disk write (regions are not a user document).

### Backward compatibility

An old INI file without the `[STACK_*]` sections: the emu starts with
default values (count=0, enabled=0, lock_sp_center=0). The sections
are created on the next exit. No crash, no error message.

## Keyboard shortcuts (summary)

| Shortcut | Action |
|----------|--------|
| Alt+S | Toggle Stack Monitor window visibility |
| Alt+Shift+H | Toggle visibility of the standalone Stack History window |

## Test workflow

Recommended program types for testing:

- **A short MZ-700 text program** (default SP 0x10F0) - quick smoke
  test: add a region (base = 0x10F0, limit = 0x1000), watch the
  watermark drop during execution.
- **IM 2 ISR demo** - visible PUSH/POP pattern in push_count/pop_count,
  ISR frame in the hex dump around SP during IM 2 dispatch.
- **Program with mid-frame palette and heavy ISR** - deep stack,
  verify a deep watermark and the SP% bar in the side panel.
- **CP/M application** - multi-stack scenario (BIOS 3-stack design:
  STCK0/STCK1/STCK3 vs application stack).

Verifying functionality:

1. Open the debugger (Alt+D), open Stack Monitor (Alt+S)
2. The header shows the current `SP: XXXXh`, Region: (none),
   Depth: `--`
3. Click **+ Add region from current SP** -> modal -> OK
4. The region appears in the dropdown and in the side panel, the
   dropdown auto-selects
5. Header: Depth updates to (base - sp_now)
6. Step Into / Step Over - the values update, the `>` marker moves
7. After a few PUSHes: the watermark in the side panel drops below
   base, the `=` marker appears in the hex dump on the watermark row
8. Reset W: the watermark returns to base, the `=` marker disappears
   (or is overlaid by `>` if SP == base)
9. Set BP from SP-256 - in the BP overlay (Alt+B) the new BPT of type
   SP_THRESHOLD with value (current_sp - 0x100) is visible
10. X in the side panel - the region disappears, the dropdown resets
    to `(none)`
11. Emulator reset - the regions remain defined, the watermarks are
    zeroed

## Related documentation

- [cpu-window.md](cpu-window.md) - CPU Registers window
- [breakpoints.md](breakpoints.md) - Breakpoints including SP_THRESHOLD
