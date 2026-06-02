# Memory Browser - Edit tools

Summary of Memory Browser editing tools: edit mode, undo/redo, Pattern
fill, Annotations, Freeze bytes and visualization of recently edited
bytes. All work through the hex view and toolbar of the Memory Browser
window.


## 1. Edit mode

Memory Browser has two basic states - **browsing** (default) and
**editing**. In edit mode the hex view accepts keyboard input and
changes are written immediately through the `write_bytes` backend to
memory.

### Turning on / off

| Path | What happens |
|---|---|
| **Key F2** | Toggle Edit mode for the active Memory Browser instance |
| **Button Edit: OFF / ON** in toolbar row 1 | Toggle (= equivalent to F2) |
| **Key Esc** | Disables Edit mode |

The **Edit** button has a **red background** when enabled as a visual
warning that keyboard input now alters memory contents.

For **read-only regions** (ROM, FLASH) editing is force-disabled - the
Edit button is greyed out and F2 does not respond. The same applies
for **disconnected regions** (= region currently unmapped in the
banking scheme).

### Two editing sub-modes

In Edit mode there are two sub-modes depending on which column of the
hex view is active:

- **HEX sub-mode** - editing by nibbles in the hex column. Each byte
  is edited as a pair of hex digits; after the second digit the cursor
  moves to the next byte and the write is committed.
- **ASCII sub-mode** - raw 7-bit ASCII input in the ASCII column
  (0x20-0x7E), each key press = 1 byte written at the current cursor
  position. For special characters (Czech diacritics, Kanji, MZ CG-ROM
  glyphs) see the Char Inserter in [search](search.md).

Switching between sub-modes: the **Tab** key.


## 2. Recently edited badge (yellow underline)

Each successful byte edit adds a record into the "recently edited"
storage **per Memory Browser instance**. Edited bytes are shown in the
hex view with a **yellow underline** under the hex digits and the
corresponding ASCII glyph.

### Storage parameters

- Max **1024 records per instance** (`MB_EDITED_MAX_PER_INSTANCE`),
  FIFO - the oldest is evicted on overflow
- Each record also holds `original_byte` = the byte value **before
  the first edit in the current session**

### Auto-unmark after returning to original

If a subsequent edit (typing, fill, undo, redo, Char Inserter) returns
the byte to its stored `original_byte` value, the record is removed
automatically and the underline disappears.

For multi-level undo this works correctly: a sequence A -> B -> C ->
undo to B -> undo to A returns the byte to A (= original), the badge
disappears.

### Manual / automatic clear

- **Clear edited button** in toolbar row 1 - manual clear for the
  active instance
- **Region switch** - on switching to another region the badges are
  cleared automatically (= new session)
- **mzarch switch / shutdown** - clear across all instances

### Visual style

A thin yellow underline (`RGB 255, 220, 70`) under the hex byte and
the ASCII glyph. Deliberately thin so it does not obscure hex digit
readability.


## 3. Per-byte undo / redo

Every memory change through Memory Browser pushes a pre-write snapshot
onto the **undo stack**. Stacks are per region (key = `region_kind +
sub_id`).

### Keys

| Key | Action |
|---|---|
| **Ctrl+Z** | Undo - restore the previous state (per active region) |
| **Ctrl+Y** | Redo - reapply the just-undone change |

The same actions are available in the right-click context menu of the
hex view:
- **"Undo (X levels)"** - X = number of available levels for the region
- **"Redo (Y levels)"** - Y = number of available redo levels

In the hex view status row, after an operation "Undo: applied" or
"Undo: nothing to undo" is shown.

### Stack limits

- **Max 10 levels per region** (`MB_UNDO_MAX_LEVELS`) - ring buffer,
  the oldest record falls off on overflow
- **Max 1 MB per snapshot** (`MB_UNDO_MAX_BYTES`) - for fill operations
  larger than 1 MB no undo record is created and the status reports
  "(undo unavailable - too large)"

### Which operations push to undo

- HEX typing (per byte = 1 push)
- ASCII typing (per byte = 1 push)
- Pattern fill (= 1 push for the whole range as a single snapshot)
- Char Inserter direct write

Standard browser/editor behavior: **a new edit after a series of
undos discards the redo stack**.


## 4. Pattern fill

A modal dialog for bulk filling a byte range using one of 6 generators.

### Opening

Right-click in hex view -> menu item **"Fill at cursor..."**.

### Fill Memory dialog

```
+-- Fill Memory ----------------------------+
| Region: User RAM                          |
| Offset: 0x1000   Length: 256              |
| Mode:                                     |
|   ( ) Zero (0x00)                         |
|   ( ) Fill (0xFF)                         |
|   ( ) Ramp (0x00 + i)                     |
|   ( ) Random (seed = ___)                 |
|   ( ) Pattern (string AA BB CC)           |
|   ( ) Increment (start = 0x__)            |
| Pattern: [_________________________]      |
|                                           |
|         [Apply]   [Cancel]                |
+-------------------------------------------+
```

### 6 fill modes

| Mode | Meaning |
|---|---|
| **Zero** | Fills with all 0x00 |
| **Fill 0xFF** | Fills with all 0xFF |
| **Ramp** | i-th byte = `i mod 256` (0, 1, 2, ..., 255, 0, 1, ...) |
| **Random** | Pseudo-random (seeded xorshift32; seed = 0 means "derive from time") |
| **Pattern** | Repeated hex pattern from user string (max 64 bytes per pattern, `MB_FILL_PATTERN_MAX_BYTES`) |
| **Increment** | i-th byte = `(start + i) mod 256` |

### Pattern parser

The **Pattern** mode accepts a hex string in the form `"AA BB CC dd"` -
2 hex digits per byte, separators `space` / comma / none (= pairs
like `"AABBCC"`). Whitespace is ignored. On a syntax error (odd number
of hex digits, invalid char) the dialog returns an error.

### Apply workflow

On **Apply** click:

1. Backend reads the current range contents into a tmp buffer
   (pre-fill snapshot)
2. The snapshot is pushed onto the undo stack for the active region
3. Backend calls `write_bytes` with the generated data
4. The range is marked "recently edited", **originals = pre-fill bytes**
   (so auto-unmark works correctly after undo)

In the status row: `"Fill: %u bytes written, undo level available"`.


## 5. Annotations with color tag

Per-byte text comment + RGBA color tag. Storage in an in-memory array
(max **1024 records**, `MB_ANNOT_MAX_ENTRIES`). Persistence into the
file `membrowser_annotations.txt` in the working directory - loaded at
startup, saved at shutdown.

### Add / edit

Right-click in hex view -> menu item:
- **"Add annotation at cursor..."** - if there is no annotation at
  the position
- **"Edit annotation at cursor..."** - if one already exists

### Dialog

```
+-- Annotation @ 0xC100 ---------------------+
| Color: [#] (color picker)                  |
| Text:                                      |
| [________________________________________] |
| [________________________________________] |
|                                            |
|         [Save]  [Delete]  [Cancel]         |
+--------------------------------------------+
```

### Text limits

- Max **256 chars** UTF-8 (`MB_ANNOT_TEXT_MAX`) per annotation
- For longer notes use CDL Notes or external documentation

### Color tag

RGBA picker with several preset swatches. Value `0` (= fully
transparent) means "neutral default" - no color highlighting.

### Hover tooltip

Hovering with the mouse over a byte with an annotation in HEX or ASCII
column shows an ImGui tooltip with a color swatch (12x12), address
and annotation text.

### Indicator in status row

If the byte under the cursor has an annotation, the bottom bar shows
a color swatch + annotation text.

### Persistence format

The file `membrowser_annotations.txt`, 1 annotation per line, fields
separated by tab:

```
kind\tsub_id\taddr\tcolor_rgba8\ttext\n
```

Escape sequences in text:
- `\\n` -> newline
- `\\t` -> tab
- `\\\\` -> literal backslash

On a syntax error the line is silently skipped; the valid rest of the
file is loaded.


## 6. Freeze bytes (cheat lock)

Per (region, offset) record + stored **value**. Every frame the
emulator overwrites these bytes back to the stored value (= "lock byte"
against overwrite by the running program).

### Limits

- **Max 256 simultaneously frozen bytes**
- If no byte is frozen, freeze has no runtime overhead

### Freeze byte

Right-click in hex view -> **"Freeze byte at cursor (= 0xXX)"** where
`XX` is the current byte value that will be frozen.

The **Frozen bytes** layer in the Layers panel (ON) draws **dark
purple BG** on frozen bytes as a visual indicator.

### Unfreeze

Right-click on a frozen byte -> **"Unfreeze byte at cursor"**.

### Banking-aware apply

The region is resolved dynamically on every apply. If a region becomes
**disconnected** mid-session (e.g. Memext/Ramdisk unplugged), the
entry is skipped for that frame (= soft tolerance). After reconnecting
the region, freeze works again.

### Persistence

Freeze records are runtime only - they are lost after emulator restart.

### Example usage: cheat HP lock

1. Find the HP byte in game RAM (search or manual scroll)
2. Right-click -> **"Freeze byte at cursor (= 0x64)"** (= 100 HP)
3. The game continues taking damage internally, but the visible HP
   stays at 100
4. Enable the visual badge via Layers panel -> **Frozen** ON


## 7. Save / Load BIN

The **Load BIN** and **Save BIN** buttons in toolbar row 1.

### Save BIN

Dialog:

```
+-- Save region as BIN -----+
| From: 0x0000              |
| Size: 65536               |
| [x] Whole region          |
| [ ] Offset from cursor    |
|                           |
| File: [_______________]   |
|        [Save]  [Cancel]   |
+---------------------------+
```

Disabled if the region is empty or disconnected.

### Load BIN

Loads a binary file and writes it to the region from offset 0 (or
from the cursor position, depending on the choice). Disabled if:
- Edit mode is off
- Region is read-only (ROM, FLASH)


## 8. Char Inserter for special characters

For inserting characters that cannot be typed directly from the
keyboard (Czech diacritics in Raw encoding, Kanji, MZ CG-ROM glyphs),
use the **Char Inserter** window - available via the "Insert
character..." item in the hex view context menu or via Menu Debugger.

Detailed description: [search](search.md).


## 9. ASCII edit dispatch (V1.5+)

In Edit mode + ASCII sub-mode the hex view accepts raw printable 7-bit
ASCII (0x20..0x7E) directly from the keyboard. For Czech diacritics
and extended characters the input is silent skip - for those characters
use the Char Inserter.


## 10. Edit tools key summary

| Key | Action |
|---------|------|
| **F2** | Toggle Edit mode |
| **Tab** | Switch HEX <-> ASCII sub-mode |
| **Esc** | Disable Edit mode |
| **Ctrl+Z** | Undo (per region) |
| **Ctrl+Y** | Redo (per region) |


## 11. Related

- [hex-view](hex-view.md) - core hex viewer, edit dispatch, cursor
- [search](search.md) - search engine and Char Inserter
- [layers-regions](layers-regions.md) - Frozen layer and region switching
- [diff-pcg](diff-pcg.md) - Memory Diff and PCG editor
