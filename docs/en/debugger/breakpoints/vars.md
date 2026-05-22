# Breakpoint - $vars (user variables)

User-defined scalar variables `$name` allow smart breakpoints to hold
state between hits (counter, state machine, manual flag for
overriding the condition). This document describes the storage,
persistence, action and expression API and the UI panel `Variables`.

Cross-references:
- Action `set $name op= expr` and `clear_vars`: `action-dsl.md`.
- Expression resolve `$name`: `expression-syntax.md`.
- Persist in `.bpt`: `persistence.md`.

## Purpose

`$vars` are global (per emulator session) signed 32-bit integer
variables. Typical use cases:

- **State machine** - BP1 sets `$stateA = 1`, BP2 tests
  `if $stateA == 1 then ...`.
- **Counter** - a BP with the action `set $count += 1` (= tracking
  the number of hits).
- **Manual override** - the user sets `$dbg_pause = 1` in the UI; a
  BP condition tests `$dbg_pause` (= dynamically toggleable BP).
- **Cross-BP coordination** - BP1 records context into `$last_addr`,
  BP2 uses it in its condition.

The value of a non-existent variable in an expression is `0` (= no-op
default).

## Identifier syntax

A valid identifier (after the `$` prefix):

```
^[a-zA-Z_][a-zA-Z0-9_]*$
```

- First character: a letter (a-z, A-Z) or underscore.
- Continuation: letters, digits, underscore.
- Case-sensitive (`$count` != `$Count`).
- Maximum length: 31 characters.
- **No reserved keywords** - the `$` prefix is itself distinctive in
  the action / expression language, so `$if`, `$set`, `$log` are
  valid user names.

Validation happens:
- In the storage layer - a defensive check.
- In the action parser - an early error with a description of the
  regex rule.
- In the UI Add Var form - feedback in the inline error text.

Examples:

| Name           | Valid? |
|----------------|--------|
| `count`        | yes    |
| `count1`       | yes    |
| `_internal`    | yes    |
| `myStateA`     | yes    |
| `1count`       | NO (digit at start) |
| `count-down`   | NO (dash) |
| `count.x`      | NO (dot)  |
| `if`           | yes (no keywords) |
| (empty)        | NO     |

## Value type

- Signed 32-bit integer.
- Default 0 for a non-existent name (= no "undefined" state).
- Overflow on `+=` / `-=` / `*=` behaves per the expression evaluator
  (= overflow inside signed 32-bit, see `expression-syntax.md`).

## Comment field

An optional comment (max 256 chars) - a note for the user. Displayed
in the Variables UI panel, persisted into the `.bpt` / `.vars` file.
Not used in the parser or in the expression evaluator.

## Persist value flag

A per-var boolean `persist_value`. Default `true`.

| Value   | Persist save                              | Load behavior         |
|---------|--------------------------------------------|-----------------------|
| `true`  | name + value + comment + flag              | restores the last value |
| `false` | name + comment + flag (without value)      | value reset to 0      |

Use case difference:

- `$frameCount` (= counter, changes on every hit) -
  `persist_value=false`; at emu start it makes no sense to have the
  old value loaded.
- `$gameStateConfig` (= user-set config option) -
  `persist_value=true`; keep the user choice across sessions.

Toggle from the UI: a checkbox in the Persist column of the table.
The default in the `[+ Add]` form is `true` (= safe default, value is
saved).

## Lifecycle

1. **Per-session storage** - lives from the initialization of the
   breakpoint subsystem until its shutdown.
2. **Persists in `.bpt`** - section `"vars"` in the JSON, shared with
   the BPs.
3. **Per-arch `.vars` separate file** - `mz800.vars` / `mz1500.vars`
   / `mz700.vars` in the cfg dir. Auto-load on start (cfg
   `auto_load=1`), auto-save on exit (cfg `auto_save=1`).

Initialization order:

1. Storage initialization (empty).
2. Load the vars section from `.bpt` (if it exists).
3. Load the `.vars` file per architecture (overrides vars from
   `.bpt`).

A standalone `.vars` overrides vars loaded from `.bpt` (= explicit
per-arch separation takes precedence). If the user wants only the
`.bpt` section, set `auto_load=0` in the section `[BP_VARS]`.

## API

### Action mini-DSL

```
set $name = expr
set $name += expr   (compact form, same as `set $name = $name + expr`)
set $name -= expr
set $name *= expr
set $name /= expr
set $name <<= expr
set $name >>= expr
set $name &= expr
set $name |= expr
set $name ^= expr

clear_vars
```

Rules:
- `set` on a non-existent name creates a new entry with `comment =
  NULL`, `persist_value = true`.
- `set` on an existing name changes **only the value** (= comment and
  flag stay unchanged).
- `clear_vars` zeroes the values of all existing entries; the
  entries remain in storage (= state-machine reset).

Compound operator details: `action-dsl.md`.

### Expression resolve

```
$name
```

Returns the value or 0 if `name` does not exist. No typed system -
`$x` is always a signed 32-bit integer. Detail:
`expression-syntax.md`.

### Load mode

When loading a `.vars` file you can choose a mode:

- **REPLACE** - clears the storage and loads from the file.
- **MERGE OVERWRITE** - merges; on a name conflict the file wins.
- **MERGE SKIP** - merges; on a name conflict the existing entry wins.

## File format `.vars`

A top-level JSON object with the key `"vars"` (= a subset of the
`.bpt` schema).

```json
{
  "vars": [
    {
      "name": "frame_count",
      "value": 1234,
      "comment": "frames since reset",
      "persist_value": true
    },
    {
      "name": "runtime_counter",
      "comment": "reset on each load",
      "persist_value": false
    }
  ]
}
```

Per-entry rules:

- `name` - mandatory, valid per the regex above.
- `value` - emitted only if `persist_value=true`. On load +
  `persist_value=false` the "value" key in the file is ignored
  (warning to stderr).
- `comment` - emitted only if non-NULL and non-empty.
- `persist_value` - always emitted (explicit flag).

Backward compatibility with a minimalist schema:

- Missing `comment` -> `NULL`.
- Missing `persist_value` -> `true`.

## File ops (UI panel)

| Button            | Action                                                  |
|-------------------|---------------------------------------------------------|
| `Save`            | Save to the default per-arch file (`mz800.vars` etc.)   |
| `Save As...`      | File dialog (extension `.vars`, confirm overwrite)      |
| `Load From...`    | File dialog -> REPLACE confirm                          |
| `Merge...`        | File dialog -> 3 choices (Overwrite all / Skip all / Cancel) |
| `Clear Values`    | Reset values (keep entries) - confirm                   |
| `Clear All`       | Delete all entries - confirm                            |

Confirm dialogs:
- **Clear All** - "Delete all N variables?"
- **Clear Values** - "Reset all values to 0?"
- **Delete selected** (bulk) - "Delete N selected vars?"
- **Load From** - "Replace current N vars with file content?"
- **Merge OVERWRITE / SKIP / Cancel** - 3 choices for conflicting
  names.

Per-arch default path:

| Build      | Filename       |
|------------|----------------|
| MZ-800     | `mz800.vars`   |
| MZ-1500    | `mz1500.vars`  |
| MZ-700     | `mz700.vars`   |

Cfg section `[BP_VARS]`:

| Key          | Default            | Meaning                      |
|--------------|--------------------|------------------------------|
| `vars_file`  | `mz<N>.vars`       | Default path (rel. cfg dir)  |
| `auto_save`  | `1`                | Save on emu exit             |
| `auto_load`  | `1`                | Load on emu start            |

## UI panel `Variables`

Opened from the menu Debugger -> Variables (no keyboard shortcut).
Default size 800x360, min 760x300. A window-level horizontal
scrollbar appears if the user resizes the window below min content
width (= scrolls the sticky header + the table together, a single
scroll for the whole window).

### Layout

**Sticky header** (2 rows, always visible at the top of the window):

1. `[+ Add]` toggle + Filter input + Clear + count `(visible/total)`
2. `Selected: N` + bulk actions: Delete / Set 0 / +1 / -1 / **Set...**
   | File ops: Save / Save As... / Load From... / Merge... / Clear
   Values / Clear All

**Add Var form** (collapsible block BELOW the sticky header, ABOVE
the table) - only if the user clicked `[+ Add]`:

```
[Name:    ___________________________________]  [Value: ___] [Persist]
[Comment: ___________________________________]                     [OK] [Cancel]
```

Widths are **dynamic** according to actual content - localization
tolerance, no hardcoded px. The Name + Comment text entries have
aligned starts and stretch / shrink with the window size.

### Table (7 columns)

| Column    | Edit                                | Meaning                       |
|-----------|-------------------------------------|-------------------------------|
| Sel       | click checkbox                      | Bulk selection (per-row)      |
| Name      | double-click = **rename** inline    | `$<name>`                     |
| Value     | double-click = inline IASM input    | Decimal display               |
| Hex       | double-click = inline IASM input    | `0x<HEX>` predfilled          |
| Persist   | click checkbox                      | Toggle persist_value flag     |
| Comment   | double-click = inline text input    | User comment (max 256)        |
| x         | click = delete (no confirm)         | Per-row delete button         |

**Tooltips** directly on buttons + edit cells (= no `(?)` markers).
Tooltip for Name: "Double-click to rename. Existing BPs with $oldname
references stay unchanged (no refactor)."

**Filter** (case-insensitive substring) matches against **name or
comment**.

### Tristate select-all checkbox

The header of the Sel column is a **tristate** select-all checkbox:

| State            | Visual                              | Click action                                     |
|------------------|-------------------------------------|--------------------------------------------------|
| **none** (= 0 selected) | empty square                  | Selects all visible                              |
| **all** (= all visible selected) | V-checkmark           | Deselects all visible                            |
| **some** (= subset)    | filled smaller square         | Selects all visible (= upgrades to "all")        |

The V-checkmark stroke uses the same visual as the standard ImGui
checkbox in rows.

### Rename behavior (Name column)

Double-click on `$<name>` -> InputText edit. Rules:

- Validation: regex - parse error inline.
- Duplicate check: if the new name already exists, error "Name
  already exists".
- **No refactor of BPs**: existing BPs with `$oldname` in the
  condition / action stay with the old name (= broken reference,
  lookup returns 0). The user must fix the BPs manually, or
  `set $oldname = ...` in the action creates a new entry.
- Apply: snapshot value/comment/persist + unset the old name + set
  the new name + restore comment + restore persist.
- Selection set update: if the old one was selected, the new one is
  too.

### Inline edit Hex column

Double-click on the `0x<HEX>` cell -> InputText edit prefilled with
`"0x{value}"` (unsigned 32-bit hex for correct display of
negatives). The apply path is **shared** with the Value column - the
user can type `42` or `#2A` or `%101010` in the Hex edit; everything
parses the same through the IASM parser.

### Bulk Set... popup

The `Set...` button in the sticky-header bulk-ops section -> popup
with a text input:

- IASM parser (42 / 0x2A / #2A / %101010).
- Enter or OK = apply to all selected.
- Esc or Cancel = close without changes.
- Inline error if parse fails.

### Inline edit - keys

| Key        | Action                                |
|------------|---------------------------------------|
| Enter      | Apply (parse + save)                  |
| Esc        | Cancel (no changes)                   |
| Click out  | Apply (= save by default)             |

If Value/Hex/Name parse / validation fails, the edit stays active
with red error text.

### IASM value parser

Input for the Value column and the Add Var form:

| Input        | Value | Note |
|--------------|-------|------|
| `42`         | 42    | decimal |
| `-1`         | -1    | negative decimal |
| `0x2A`       | 42    | hex C-style |
| `#2A`        | 42    | hex Sharp/IASM |
| `%101010`    | 42    | binary |
| `abc`        | error | non-numeric |
| (empty)      | 0     | (Add form; inline edit error) |

Whitespace tolerant (leading + trailing). Hex / bin do not accept a
sign (= bit interpretation, sign makes no sense).

## Validation rules summary

| Rule                              | Where checked                        |
|-----------------------------------|--------------------------------------|
| Name regex                        | parser + storage + UI Add form       |
| Name unique                       | UI Add form (storage accepts upsert) |
| Value IASM format                 | UI inline edit + Add form            |
| Value range int32                 | IASM parser (overflow = error)       |
| Comment max 256 chars             | UI InputText limit (storage tolerant) |

## Use case examples

### State machine - pair of events

BP1 (PC=0x1000): `set $stateA = 1`
BP2 (PC=0x2000): `if $stateA == 1 then ; set $stateA = 0`

That is, BP2 triggers only if BP1 just passed, and it resets the
state.

### Counter pattern

BP (PC=0x1234): `set $hitCount += 1; if $hitCount > 100 then mark "hot"`

Counter `$hitCount` (= persist_value=false for a reset on every
start), the mark action runs after crossing the threshold.

### Manual override from the UI

A permanent BP with the condition `if $dbg_pause == 1`. The user sets
`$dbg_pause = 1` in the Variables UI panel to activate it, `0` to
deactivate. No emu restart, no editing of the BP definition.

### Cross-BP context

BP1 (PC=0x1000): `set $last_a = a` (= snapshot of register A at hit time)
BP2 (PC=0x2000): `if a != $last_a then log "A changed: %d -> %d", $last_a, a`

## Limits

- **No nested structures** - scalar signed 32-bit integer only. For
  arrays / structs use the Memory Browser or `mem[addr]` in an
  expression.
- **No typed system** - int32 for everything. A bool is represented
  as 0/1.
- **No expression in the default value** - a newly created var (via
  the Add form without Value) has the value 0. A custom default is
  set in an action BP with an expression.
- **No scope** - everything is global per session.
- **No race protection vs the emulator thread** - the UI sees the
  storage as-is, the BP action executor mutates the storage from the
  emulator thread. Worst case: one frame displays a stale value (=
  acceptable for a debug UI).

## Related documents

- `expression-syntax.md` - condition expression grammar including
  `$vars`
- `action-dsl.md` - action DSL with `set` and `clear_vars`
- `persistence.md` - JSON schema for the `vars` section
