# Smart variables ($name)

User-defined signed 32-bit integer variables, addressable by `$name`.
Storage is shared across all breakpoints and persists per-session.

For the authoritative document with all corner cases see
`docs/en/debugger/breakpoints/vars.md`.

## Identifier syntax

```
^[a-zA-Z_][a-zA-Z0-9_]*$
```

Max 31 characters. Case-sensitive (`$count` != `$Count`). No reserved
names - `$if`, `$set`, `$log` are valid because `$` itself is
distinctive.

## Read (expression side)

In any BP condition / Watch expression / action argument:

```text
$counter
$counter > 100
$last_addr == HL
```

A non-existent name reads as `0`. The eval never raises an error.

## Write (Action DSL)

Variables are written only via the BP Action DSL. The compound forms
auto-create the variable on first write.

```text
$counter = 0                      ; create or reset
$counter += 1                     ; increment (alias for $counter = $counter + 1)
$counter -= 1
$mask &= 0x0F                     ; bit clear
$hits |= 1 << A                   ; bit set
$last_pc = PC

clear_vars                        ; zero all values (entries kept)
```

Note: `$name op= expr` is **bare** (= no `set` keyword). The `set`
command is reserved for **Z80 register writes** (e.g. `set PC 0x8000`).
See `emulator://docs/action_dsl` for the full command list.

Supported compound operators: `=`, `+=`, `-=`, `*=`, `/=`, `<<=`, `>>=`,
`&=`, `|=`, `^=`. The RHS is a full expression (see
`emulator://docs/bp_dsl`).

## Lifecycle

1. Storage initialised empty at MCP / debugger init.
2. `.bpt` file (full breakpoint config) loads `"vars"` section.
3. Per-arch `.vars` file (e.g. `mz800.vars`) loads next; **it
   overrides** entries from `.bpt` for the same name.
4. On emu exit, auto-save writes to `.vars` (when `[BP_VARS]
   auto_save=1`).

## Per-variable persistence flag

- `persist_value=true` (default): name + value + comment saved.
- `persist_value=false`: name + comment saved; value resets to 0 on
  next load (typical for counters that should not carry state across
  emu sessions).

## Read live state

Use the live resource `emulator://vars` to enumerate current variables
with values. The list reflects everything created via Action DSL,
loaded from `.bpt` / `.vars`, or added in the UI Variables panel.

## Use cases

### Counter

BP at `PC == 0x1234` with action `$hits += 1`. Then a separate BP
fires on `$hits > 100`. Set `persist_value=false` if `$hits` should
reset between sessions.

### State machine across BPs

BP1 (`PC == 0x1000`): action `$stateA = 1`
BP2 (`PC == 0x2000`): condition `$stateA == 1`, action `$stateA = 0`

BP2 fires only after BP1 has been hit.

### Manual override from the UI

Permanent BP with condition `$dbg_pause == 1`. Toggle `$dbg_pause` in
the Variables UI panel to activate / deactivate the BP without editing
its definition.

### Cross-BP context snapshot

BP1: `$last_a = A`
BP2: condition `A != $last_a` (= fires on A change between two PCs).

## Limits

- Scalar `int32_t` only. No arrays / structs / strings. For aggregate
  data use memory reads (`[addr]`, `{addr}`) in expressions.
- No scope - global per emulator session.
- No race protection vs the emu thread; one frame of stale display in
  the UI is acceptable.

## Related

- `emulator://docs/bp_dsl` - expression language including `$name` read.
- `emulator://docs/action_dsl` - full BP Action DSL grammar (11
  commands, `$name` writes context).
- `emulator://vars` - live snapshot of all variables.
- `docs/en/debugger/breakpoints/action-dsl.md` - authoritative
  reference with all corner cases.
