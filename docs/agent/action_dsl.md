# BP Action DSL

A breakpoint's action is a sequence of commands that runs when the BP
fires (= condition was true and hit / skip counts have elapsed). The
action grammar is referred to as the "Action mini-DSL".

For the authoritative document with all corner cases see
`docs/en/debugger/breakpoints/action-dsl.md`.

## Stop vs continue semantics

No explicit `stop` command exists. The decision to halt the emulator
is driven solely by whether the action string is empty:

- **action == NULL or whitespace-only** -> emulator stops (classic BP).
- **action with any non-empty content** -> after evaluating all
  commands the emulator continues (implicit continue).

Consequence: a "log and then stop" BP is **not expressible directly**
in one BP. Workaround: increment `$hits` and add a separate Stop BP
on `$hits >= N`, or use Stop mode without a Custom script.

## Lexical rules

- Commands separated by `;` or newline.
- `#` starts a line comment (ignored inside `"..."`).
- Whitespace outside strings is irrelevant.
- Identifiers are case-insensitive (consistent with expression
  evaluator).
- Expression-type arguments use the BP DSL grammar - see
  `emulator://docs/bp_dsl`.
- Parser is strict: unknown command, wrong arity or an unterminated
  string literal = error. A BP with an unparsable action shows an
  inline error in the UI and at runtime behaves as empty action
  (= stop fallback).

## 11 commands (cheat sheet)

| Command | One-liner | Example |
|---------|-----------|---------|
| `log "fmt"[, expr...]` | Printf-style message to stdout, max 8 args | `log "PC=%X A=%X", PC, A` |
| `continue` | No-op marker (default is already continue) | `continue` |
| `disable_self` | Disable the BP that just fired (one-shot pattern) | `disable_self` |
| `enable <name>` | Activate another BP by name (case-sensitive) | `enable trace_isr` |
| `disable <name>` | Deactivate another BP by name | `disable trace_isr` |
| `poke <addr> <value>` | Write 1 byte into Z80 memory map | `poke 0x8000 0x42` |
| `set <reg> <value>` | Write into Z80 register | `set HL 0x8000` |
| `mark "name"` | Trace-marker (marklog binary + optional stdout) | `mark "ISR enter"` |
| `$name = <expr>` (+ compound ops) | Assign to user variable | `$hits += 1` |
| `clear_vars` | Reset all `$name` values to 0 (entries preserved) | `clear_vars` |
| `if <expr> then <stmt> [else <stmt>]` | Single-line conditional | `if $hits >= 5 then disable_self` |

## `log` format specifiers

The first argument is a double-quoted format string with escape
sequences `\n`, `\t`, `\"`, `\\`. Remaining args are BP DSL
expressions evaluated at fire time.

| Spec | Meaning |
|------|---------|
| `%X` | hex uppercase |
| `%x` | hex lowercase |
| `%d` | signed decimal |
| `%u` | unsigned decimal (32-bit) |
| `%b` | binary (no leading zeros) |
| `%c` | character (low byte as ASCII) |
| `%s` | arg = expression (address); looks up name in sym_db, prints symbol or `<no-symbol-at-0xADDR>` |
| `%%` | literal `%` |

**Width / padding (`%04X`, `%8d`) is NOT supported** - the parser
reads a single character spec. If you need zero-padded hex emit it
via multiple `log` calls or fixed-width construction externally.

Max 8 expression arguments per `log` call. Output goes to stdout as
`[BP-LOG] <formatted>\n`.

```
log "PC=%X A=%X HL=%X", PC, A, HL
log "frame=%d cycle=%d", Frame, Cycle
log "JP target=%X (%s)", HL, HL          # %s looks up symbol at HL
log "ret to %X (%s)", {SP}, {SP}         # %s with 16-bit memory deref
```

## Register writes (`set <reg> <value>`)

`<reg>` is a Z80 register identifier (case-insensitive). `<value>`
is a BP DSL expression. Outside of BP enforcement (= Test Eval, no
CPU context) the command prints a stderr warning and is a no-op.

Supported register identifiers:

- 8-bit: `A`, `F`, `B`, `C`, `D`, `E`, `H`, `L`
- 16-bit: `AF`, `BC`, `DE`, `HL`, `IX`, `IY`, `SP`, `PC`
- 8-bit halves of index registers: `IXH`, `IXL`, `IYH`, `IYL`
- shadow pairs (16-bit): `AF'`, `BC'`, `DE'`, `HL'` (apostrophe is
  part of the name)
- special: `I`, `R`, `IM` (low 2 bits), `IFF1` (low bit),
  `IFF2` (low bit)

```
set A 0x42
set PC 0x100      # forced jump
set IFF1 1        # enable interrupts
set BC' 0x1234    # write to shadow BC'
set IXH 0xFF      # high byte of IX, low byte preserved
```

## Memory writes (`poke`)

`poke <addr> <value>` writes 1 byte through the Z80 memory map.
Both arguments are expressions separated by **whitespace** (not
comma). The split finds the first top-level whitespace outside
`(...)`, `[...]`, `{...}` or strings.

```
poke 0x8000 0x42
poke HL A
poke 0xE000 + Y A & 0x0F
```

`value` is masked to low byte (`& 0xFF`), `addr` to 16 bits. Writes
into ROM-mapped regions or unmapped pages follow the current banking
behavior (= typically no-op or bank-aliased write, see
`emulator://docs/memory_layout` for safe RAM ranges per platform).

## User variables (`$name`)

`$name = <expr>` or compound `$name <op>= <expr>`. The full
identifier rules, persistence, lifecycle and persist_value flag are
described in `emulator://docs/smart_vars`. Operators in the action
DSL: `=`, `+=`, `-=`, `*=`, `/=`, `<<=`, `>>=`, `&=`, `|=`, `^=`.
Non-existent `$name` reads as 0 on the LHS of a compound op.

`clear_vars` zeroes all existing `$name` values; entries (name,
comment, persist_value flag) stay in storage.

```
$hits += 1
$mask = 0xFF
$last_pc = PC
$accumulator |= 1 << bit_pos
clear_vars
```

## Single-line conditional

```
if <expr> then <stmt> [else <stmt>]
```

`else` is optional. No multi-statement block, no nested
`if/then/if/then`. For multi-statement bodies use several `if`s with
the same condition, or set a `$flag` first:

```
if PC >= 0x1000 && PC < 0x2000 then log "in code seg PC=%X", PC
if $hits >= 5 then disable_self
if A == 0 then log "zero" else log "nonzero=%X", A
if $state == 1 then enable trace_bp else disable trace_bp
```

The keywords `then` and `else` must be whole words at top-level depth
(not inside `()` / `[]` / `{}` / a string).

## Markers (`mark "name"`)

Trace-suite marker with optional stdout printout. `name` is a
double-quoted string literal (same escapes as `log`).

Two independent branches run when the BP fires:

1. **Marklog binary record** - if `[TRACE_MARKLOG] mode` is
   `WITH_WINDOW` or `ALWAYS` (and the writer is active), a 24 B
   record is written into `<dir>/<name>.NNN.bin` with a pre-resolved
   `marker_id`. ID -> name mapping is in `meta.json`.
2. **Stdout printout** `[BP-MARK] <name>` - if
   `[TRACE_MARKLOG].stdout_enabled` is `1` (default).

Limits: max 63 chars per marker name, max 65535 unique markers per
session. Format details: `docs/cz/debugger/formats/MARK-log_format.md`.

## Cross-BP enable / disable

`enable <name>` and `disable <name>` look up another BP by its `name`
field (case-sensitive linear scan). The argument is **not quoted** -
the name is the rest of the line after the keyword + whitespace.

If the target BP does not exist, the command prints a stderr warning
and is a no-op. `disable_self` outside of BP enforcement (Test Eval
or manual trigger) also warns + no-ops.

## Full examples

### Trace without stopping

```
log "PC=%X HL=%X A=%X", PC, HL, A
```

### Hit counter with one-shot disable

```
$hits += 1
log "hit #%d at PC=%X", $hits, PC
if $hits >= 5 then disable_self
```

### Cross-BP state machine

BP `isr_enter`:
```
enable isr_trace
mark "ISR enter"
```

BP `isr_exit`:
```
disable isr_trace
mark "ISR exit"
```

BP `isr_trace` (initially disabled):
```
log "ISR step PC=%X", PC
```

### Forced jump after N hits

```
$count += 1
if $count >= 100 then set PC 0x8000
if $count >= 100 then $count = 0
```

## Common gotchas

- `log` format width / padding (`%04X`, `%8d`) is **not supported**.
- `log` max 8 expression arguments.
- `set <reg>` and `poke` use **whitespace** between args, not comma
  (in contrast to `log` which uses comma between fmt and exprs).
- `enable` / `disable` use the BP's `name` field, not its numeric
  ID. Names are case-sensitive.
- A single `=` in a condition expression is a parse error; in the
  action DSL it is the only valid form for `$name = expr`.
- `disable_self` only works inside real BP enforcement; in UI Test
  Eval it warns + no-ops.
- Division / modulo by zero in `$var` compound RHS or in argument
  expressions returns 0 (tolerant evaluator).
- `set PC <expr>` forces a jump on the **next** instruction step;
  the current instruction has already been fetched.
- `poke` honours current banking - to write into RAM mirrored
  underneath ROM you may need to flip the bank first (see
  `emulator://docs/memory_layout` and ports `0xE0..0xE6`).

## Error states

### Parse-time

Lexer / parser errors (bad keyword, unterminated string, bad `set` /
`poke` syntax, missing `then`, ...) appear as an inline error below
the action textbox in the Edit BP dialog. The BP runtime behaves as
empty action (= Stop fallback) until you fix the error.

### Runtime

Most runtime errors are tolerant with a stderr warning:

- `enable` / `disable` with non-existent target -> warning, no-op.
- `set` with unknown register -> warning, no-op.
- `set` without a CPU context -> warning, no-op.
- `disable_self` outside enforcement (= Test Eval) -> warning, no-op.
- `poke` outside addressable RAM -> typically no-op or bank-aliased
  write, depending on the memory subsystem.

## Related

- `emulator://docs/bp_dsl` - expression syntax used in every action
  argument (condition exprs, `log` args, `set <reg> <expr>`, etc.).
- `emulator://docs/smart_vars` - full `$name` user-variable model
  (storage, persistence, `.bpt` + `.vars` files, persist_value flag).
- `emulator://docs/memory_layout` - safe RAM ranges per platform,
  banking ports for `poke` planning.
- `docs/en/debugger/breakpoints/action-dsl.md` - authoritative
  reference with all corner cases.
- `docs/en/debugger/breakpoints/hw-events.md` - per-BP-type
  `Address` / `Value` semantics relevant for `log` arguments.
