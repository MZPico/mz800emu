# Symbols - address <-> name mapping

The symbol database assigns user-defined names to specific addresses in
emulator memory and exposes them in the debugger UI (disassembler,
breakpoint targets, memory browser). This document describes storage,
source priority, supported file formats and the `Symbols` UI panel.

## Purpose

Typical use cases:

- **Disassembler labels** - `CALL 0xD8A2` is shown as `CALL wboot` if a
  symbol is registered for that address.
- **Breakpoint targets** - a smart BP displays the name instead of an
  address in the BP list (= "BP at print_char (0x4042)").
- **Memory browser tooltips** - hovering an address in a memory dump
  reveals the symbol name and comment.
- **User labels (LBL)** - the user adds their own names and comments for
  functions / constants reused across the session.
- **Import from assembler** - load symbol exports in `.noi`/`.map`/`.sym`
  format to follow execution of a source-level project (sdldz80,
  sjasmplus, ...).

## Source priority

A symbol may have multiple sources. Lookup by address picks the one with
the highest priority:

| Priority | Source | Description | File |
|----------|--------|-------------|------|
| 3 (max)  | LBL    | User write-back (= manually added labels + comments) | `.lbl` |
| 2        | MAP    | sdldz80 linker map (incl. module name as a hint) | `.map` |
| 1        | NOI    | SDCC NoICE export | `.noi` |
| 0 (min)  | SYM    | sjasmplus symbol export | `.sym` |

The higher priority "wins" on lookup by address. Lookup by name is
case-sensitive and returns a single record with that name (= the name is
unique within the storage).

**Auto-promote to LBL:**
- Setting a comment for a symbol overrides its source to LBL (=
  write-back preserves the user comment into the `.lbl` file).
- Manually adding a user label for an existing name overwrites the record
  to LBL priority.

## Per-bank filtering

A symbol has a `bank_id` field. On lookup by address:

- `bank_id == 0` = "CPU view", returns only records with `bank_id == 0`
  (= global symbols visible regardless of MMEXT bank).
- `bank_id != 0` = returns the record with the matching bank_id, falls
  back to `bank_id == 0` if no explicit bank-specific symbol exists.

Most imported symbols (`.noi`/`.map`/`.sym`) have `bank_id = 0`; the
MMEXT bank scope is used mainly for user-defined LBL (= manually
distinguished per banking config).

## File formats

### `.noi` (SDCC NoICE export)

Per line:
```
DEF <name> 0x<hex>
```

Example:
```
DEF wboot 0xD8A2
DEF print_char 0x4042
```

Comments are not supported.

### `.map` (sdldz80 linker map)

The parser reads `Global Defined In Module` sections. The module name is
extracted as an optional comment hint (= "from module utils.rel"). The
format is the standard sdldz80 output.

### `.sym` (sjasmplus symbol export)

Per line:
```
<NAME> EQU <value>
```

The value may be:
- `EQU $ABCD` (= sjasmplus hex prefix `$`)
- `EQU 0xABCD` (C-style hex)
- `EQU 12345` (decimal)

### `.lbl` (user write-back)

Line-based format:

```
# comment (skip)
<addr_hex>  <name>  <bank>  ; per-symbol comment
```

Example:
```
4042  print_char  0  ; print one character to display
D8A2  wboot       0  ; warm boot entry from BIOS
```

Per-symbol comment follows the `;` character.

Saving `.lbl` serializes **ONLY** LBL records (not import-driven NOI /
MAP / SYM). The user write-back persists user labels + comments between
sessions without duplicating import sources.

### Auto-detect

On load, the parser is picked by suffix:
- `.noi` -> NOI parser
- `.map` -> MAP parser
- `.sym` -> SYM parser
- `.lbl` -> LBL parser

Unknown suffixes are not loaded.

## `Symbols` UI panel

Opened from menu Debugger -> Symbols. Default 820x420, min 760x300. A
window-level horizontal scrollbar appears if the user shrinks below the
minimum content.

### Layout

**Sticky header (2 rows, always at the top of the window):**

1. `[+ Add]` toggle + Search:`[___]` + `[Clear]` + count `(visible/total symbols)`
2. `Selected: N` + bulk: `[Delete]` | file ops: `[Load From...]` `[Save .lbl As...]` `[Clear All]`

**Add Label collapsible block** (below the sticky header, only when
`[+ Add]` is active):

```
Addr (hex):[____]  Name:[____________]
Comment:[_______________________________]  [OK] [Cancel]
```

On a validation error an inline error is shown below the block (red
text).

### Table (7 columns)

| Column  | Edit | Meaning |
|---------|------|---------|
| Sel     | click checkbox | Bulk selection (per-row) |
| Name    | double-click = inline rename | Identifier |
| Addr    | double-click = inline relocate | `0x%04X` |
| Bank    | (read-only) | `bank_id` decimal |
| Source  | (read-only) | `LBL` / `MAP` / `NOI` / `SYM` |
| Comment | double-click = inline edit | User comment, auto-promote LBL |
| x       | click = delete (no confirm) | Per-row delete |

**Tooltips** are shown directly on element hover.

**Filter** (case-insensitive substring) matches on **name** or
**comment** or **addr-hex string** (e.g. `"40"` finds `0x40FF`).

### Tristate select-all checkbox

The header of the Sel column is a **tristate** select-all checkbox:

| State | Visual | Click action |
|-------|--------|--------------|
| **none** (0 selected) | empty square | Select all visible |
| **all** (all visible) | V-checkmark | Deselect all visible |
| **some** (subset) | smaller filled square | Select all visible |

### Inline edit Name (rename)

Double-click on a Name cell -> InputText edit. Rules:

- Validation: letters/digits/`_`/`.`/`'`, no leading digit, max 63.
- Duplicate check: the new name must not collide with an existing one.
- For **non-LBL source** (NOI/MAP/SYM): the old symbol stays (= rename
  cannot delete an import-driven symbol). The new LBL alias points to the
  same addr; the user may delete the old one manually or restart the emu
  (= NOI data is not persisted without an explicit re-import).
- Apply: Enter / click outside. Esc = cancel.

### Inline edit Addr (relocate)

Double-click on an Addr cell -> InputText edit (hex characters only).
Rules:

- Parse hex ("4042" / "0x4042" / "#4042").
- Overwrite the symbol with the new addr (same name -> LBL promote).
- Apply: Enter / click outside. Esc = cancel.

### Inline edit Comment

Double-click on a Comment cell -> InputText. On Enter / click outside the
comment is set and the symbol is **auto-promoted** to LBL source (= the
next save_lbl will serialize it).

### Bulk Delete

`[Delete]` in the sticky header bulk section -> confirm dialog `"Delete N
selected symbols?"`.

Bulk Delete succeeds only for **LBL** source symbols. Non-LBL symbols
from imports remain.

### File ops

| Button | Action |
|--------|--------|
| `Load From...` | File dialog for `.noi/.map/.sym/.lbl` (auto-detect by suffix) |
| `Save .lbl As...` | File dialog with `.lbl` filter + confirm overwrite |
| `Clear All` | Confirm dialog `"Wipe all N symbols (LBL + imported)?"` |

A status line below the sticky header shows the result of the last
operation (`Loaded N symbols from path` / `Saved N LBL symbols to path`
/ error).

### Inline edit keyboard shortcuts

| Key | Action |
|-----|--------|
| Enter | Apply (= parse + save) |
| Esc | Cancel (= no changes) |
| Click outside | Apply |

If Name / Addr parsing fails, the edit stays active with red error text.

## Persistence

The symbol database has auto-load and auto-save of the default `.lbl`
file, symmetric to the breakpoints lifecycle.

### Cfg section `[SYMBOLS]`

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `lbl_file` | TEXT | per-arch `mz800.lbl` / `mz1500.lbl` / `mz700.lbl` (relative to home cfg dir) | Path to the default `.lbl` file for auto-load and auto-save |
| `auto_load` | BOOL | 1 | At startup, try to load the default `.lbl` (silently tolerant to a missing file = first start) |
| `auto_save` | BOOL | 1 | On exit, save LBL records into the default `.lbl` |

### UI Persist... popup

In the toolbar (next to Load/Save/Clear All) there is a **Persist...**
button that opens a popup with:

- **Auto Load on start** toggle (modifies the `auto_load` cfg)
- **Auto Save on exit** toggle (modifies the `auto_save` cfg)
- Display of the current default file path
- **Browse...** to override the default path
- **Load now** / **Save now** for a manual trigger independent of the
  auto-* flags

The default behavior is fully automatic two-way persist. The user can
disable it via the Persist... popup or directly by editing the INI.

### What is persisted

The `.lbl` file holds **only LBL records** (= user-driven labels +
comments). Import-driven (NOI/MAP/SYM) symbols must be reloaded from
their original source files.

## Use case examples

### Importing SDCC build artifacts + user labels

```
1. Get sdcc compilation output: my_program.noi + my_program.map
2. In the Symbols panel [Load From...] -> my_program.noi (= NOI source)
3. [Load From...] -> my_program.map (= MAP overwrites NOI per priority)
4. The disassembler now shows MAP-priority symbols.
5. The user double-clicks Comment on "main_loop" -> enters "tick handler,
   called every 50Hz" -> auto-promote to LBL.
6. [Save .lbl As...] -> my_program.lbl (= user comments only, no MAP/NOI).
```

### Custom labels for ROM routines

```
1. [+ Add] toggle.
2. Addr: 0xD8A2, Name: wboot, Comment: BIOS warm boot
3. [OK] -> LBL record.
4. The disassembler shows "CALL wboot" instead of "CALL 0xD8A2".
5. On emu restart - the default `.lbl` is auto-loaded and labels are
   restored.
```

### Reorganizing LBL after refactoring

```
1. The memory map has changed - move a function from 0x4042 to 0x4500.
2. Double-click Addr on "print_char" -> change to 0x4500.
3. The symbol now maps 0x4500 -> print_char (= overwrite with the same
   name to a new addr).
```

## Limitations

- **No multi-bank labels in imports** - `.noi`/`.map`/`.sym` parsers
  ignore bank info; user-driven `.lbl` may have an explicit `bank_id`.
- **No sub-symbol structures** - only a flat name -> addr mapping.
- **Renaming a non-LBL source cannot delete the original** (= the
  import-driven symbol remains).
- **No diff/merge between loaded files** - a second `[Load From...]`
  simply overwrites by priority. For a merge workflow an explicit
  `[Clear All]` is required first.
