# Bookmarks - named address bookmarks

The **Bookmarks** window lets you save an arbitrary number of named
bookmarks to addresses (entered as a hex literal or as a symbol name).
Bookmarks persist across emulator restarts (per-arch JSON file) and
clickably redirect focus to any disasm instance (#1 main, #2-5
secondary).

## Data model

A bookmark has three properties:

- **id** - monotonic counter, persistent across sessions
- **user_input** - hex string (`0xE6CA`, `#1234`, `1234h`) or symbol
  name (`MAIN_LOOP`); resolution happens dynamically
- **comment** - optional comment

**The address is dynamic.** Storage keeps only the `user_input` as a
raw string; on UI render each frame the resolve is performed:

1. Try parsing as hex: `0x1234`, `0X1234`, `#1234`, `$1234`,
   `1234h`/`1234H`, bare hex `1234` (max 4 chars, hex digits only)
2. Fallback: lookup in the symbol DB (case-sensitive)
3. If even the symbol fails, resolve `false` and the row in the UI is
   greyed disabled

**Consequence:** a symbol bookmark follows the current symbol DB - an
import of a new `.map` file is immediately reflected in the Address
column and the label.

## File format - JSON

```json
{
  "version": 1,
  "bookmarks": [
    {"id": 1, "input": "MAIN_LOOP", "comment": "main computation loop"},
    {"id": 2, "input": "0xE6CA", "comment": ""},
    {"id": 3, "input": "$1234", "comment": "screen RAM"}
  ]
}
```

Auto-load happens at emulator startup, auto-save at shutdown, both
according to the cfg keys listed below.

Cfg section `[BOOKMARKS]`:

| Key | Type | Default |
|-----|------|---------|
| `bookmarks_file` | string | per-arch (`mz800.bookmarks`, `mz1500.bookmarks`, `mz700.bookmarks`) |
| `bookmarks_auto_save` | bool | true |
| `bookmarks_auto_load` | bool | true |

## UI window

Activation: top menu **Debugger -> Bookmarks**. Default closed. The
window position and size are remembered via `imgui.ini`.

### Layout

```
+------------------------------------------------------------+
| Bookmarks                                            [X]   |
+------------------------------------------------------------+
| Filter:[__________] [Clear]   3 bookmarks (2 filtered)     |
| Selected: 0  [Delete selected]                             |
| [Save] [Save As...] [Load...] [Merge...]                   |
+------------------------------------------------------------+
| Add                                                        |
|   Bookmark: [_______________________] [Insert PC]          |
|   Comment:  [_______________________] [Add]                |
+------------------------------------------------------------+
| [v] | [> ...] Label  | $address | Comment       | Del      |
| [ ] | [> ...] MAIN_..| $E6CA    | main loop     | [x]      |
| [ ] | [> ...] 0x1234 | $1234    |               | [x]      |
| [ ] | (--unresolvable--) ----   | invalid sym   | [x]      |
+------------------------------------------------------------+
```

### Add form

Layout: 2 rows, label + input + button:

- **Bookmark:** InputText (placeholder "address or symbol") +
  **[Insert PC]** (inserts the current `PC` as `#XXXX` into the entry)
- **Comment:** InputText + **[Add]**

Validation: an empty bookmark input -> red border + tooltip "Address
or symbol required" + Add button disabled. After a successful Add
both buffers are cleared and focus returns to the Bookmark entry.

### Table

4 columns (+ select checkbox as column 0):

| # | Column | Contents | Width |
|---|--------|----------|-------|
| 0 | Sel | per-row checkbox + 3-state header (none/some/all) | fixed |
| 1 | Label | `[> ...]` button + plain text label | flex |
| 2 | $address | dynamic hex `$XXXX` or `----` | fixed |
| 3 | Comment | clickable (= edit on click) | flex |
| 4 | Del | `[x]` button | fixed |

**Label column** contains:

- **`[> ...]` SmallButton:**
  - LMB click -> focus in the primary disasm instance (Disassembly #1)
  - RMB click -> popup "Show in Disassembly #1..#5"
  - Disabled if the address is not resolvable
  - Tooltip: "Focus in primary disassembly. Right-click for additional
    actions."
- **Plain text label** (= `user_input`, or the symbol name):
  - Hover -> underline + tooltip "Click to edit"
  - Click -> switches the row into inline edit mode
  - Greyed disabled if unresolvable

**Comment column:**

- Hover -> underline + tooltip "Click to edit"
- Click -> switches the row into inline edit mode
- Shows `(no comment)` if the comment is empty

### Inline edit

A click on Label or Comment switches the row into edit mode:

- Label column -> InputText with the current `user_input`
- Comment column -> InputText with the current comment
- Del column -> replaced by the buttons `[v]` Apply + `[x]` Cancel

**Keys:**

- **Enter** in InputText -> Apply (saves the changes)
- **Escape** -> Cancel (discards the changes)
- If the user presses Enter and Escape in the same frame, Apply has
  priority

### Filter

InputText "name, comment..." - case-insensitive substring match over:

- `user_input` (raw input)
- the resolved label (symbol name if `user_input` is hex)
- `comment`

An empty filter shows all bookmarks.

### 3-state checkbox in the Sel column header

Visualizes the selection state across visible rows (= after the filter
is applied):

| State | Symbol | Click action |
|-------|--------|--------------|
| **none** | empty square | select all visible |
| **some** | indeterminate (filled square) | select all visible |
| **all** | checkmark | deselect all visible |

### File ops

Buttons in the window header open a file dialog:

- **Save** - quick save into the default file
- **Save As...** - file dialog, save to the chosen path
- **Load...** - file dialog, replace mode (replaces the current list)
- **Merge...** - file dialog, append mode (adds bookmarks,
  deduplication by ID and identical contents)

## Click behavior of the `[> ...]` button

**LMB click**: focus in the primary disasm instance (Disassembly #1):

1. If the main debug window is closed, it opens
2. If `follow_pc=true` and the emu is running, follow_pc is
   permanently turned off
3. The disasm is pointed at the bookmark address

**RMB click** opens a popup with 5 items "Show in Disassembly #1..#5".
For a secondary window the window is opened, focus is set and (if the
emu is running) Follow PC is permanently turned off.

## Adding a bookmark from Disassembly

The right-click context menu in the Disassembled section contains the
item **"Add to bookmarks"**:

1. If a symbol exists at the address, the symbol name is used as
   `user_input`. Otherwise the hex literal `#XXXX` is used.
2. The bookmark is inserted with an empty comment.
3. The Bookmarks window is opened so the user can see the added
   bookmark and optionally fill in the comment.

The same item is available in the RMB popup of the clickable PC in the
I/O Ports History (Selected Event panel).

## Coupling to other panels

- **Disassembly** ([disassembly.md](disassembly.md)) - the target of
  LMB/RMB from the `[> ...]` button and from the "Add to bookmarks"
  item in the context menu.
- **Symbols** - the resolution of a symbol name to an address goes
  through the symbol DB; importing `.lbl`/`.map` is immediately
  reflected in the bookmarks table.
- **I/O Ports History** - the RMB popup over a clickable PC offers
  "Add to bookmarks".

## Known limitations

1. **Multi-select**: per-row checkbox + bulk delete in the header.
   There is no Ctrl/Shift range select nor Click-and-Drag for a
   range.
2. **Inline edit cancel via Escape** is global within the frame -
   Escape anywhere cancels the edit.
3. **No keyboard shortcut** for toggling the Bookmarks window - only
   the menu item.
4. **Comment max length** is a soft 256 characters (UI buffer);
   storage accepts longer texts.
