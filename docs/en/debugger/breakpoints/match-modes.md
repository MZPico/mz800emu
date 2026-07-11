# Breakpoint - Match modes

The match mode decides how a BP evaluates a match of address / port /
bank / SP against the current emulator state. Without an explicit
setting the default mode is `SINGLE` (= match on the exact value).

This document describes the semantics of all three modes and their
interaction with other aspects of the BP (Zone awareness, Condition
expression).

## Overview

For address fields (PC_EXEC, MEM_R, MEM_W, IORQ_R, IORQ_W,
MMEXT_BANK):

| Mode | Semantics | Typical use case |
|------|-----------|------------------|
| `SINGLE` | `x == ref` | regular BP on a specific address |
| `RANGE`  | `ref <= x <= end` | watch an entire code block / memory buffer / port group |
| `MASK`   | `(x & mask) == (ref & mask)` | dispatch table debugging, register group probe |

For SP_THRESHOLD there is a separate mode:

| Mode | Semantics | Typical use case |
|------|-----------|------------------|
| `SINGLE` | trigger on a descending crossing of a single threshold | stack overflow detect |
| `WINDOW` | trigger if SP leaves `[lower..upper]` | stack corruption / cross-task switch detect |

## SINGLE mode

Default. Match only on an exact value match.

```
Match Mode: Single
Address:    0x1234
```

Triggers if `value == 0x1234`.

## RANGE mode

Matches if the value lies in the inclusive range `[start..end]`.

```
Match Mode: Range
Address:    0x1000
End Addr:   0x10FF
```

Triggers for addresses `0x1000`, `0x1001`, ..., `0x10FF` (256
addresses).

### Examples

1. **Memory buffer watch** - track writes into a buffer:
   ```
   Type:       MEM_W
   Address:    0xE000
   End Addr:   0xE0FF
   Match Mode: Range
   ```

2. **I/O port group** - track writes to the group of PSG ports:
   ```
   Type:       IORQ_W
   Port:       0xF0
   End Port:   0xFF
   Match Mode: Range
   ```

3. **PEHU overlay range** - debug a group of overlay banks:
   ```
   Type:       PC_EXEC
   Zone:       MMEXT_BANK
   Bank ID:    08
   End Bank:   0F
   Match Mode: Range  (bank Match Mode)
   ```

### Validation

End must be `>= start`, otherwise the UI deactivates the OK button +
shows a warning. At runtime there is a defensive swap for SP WINDOW
(= safety against a manually edited `.bpt` JSON), but for an address
RANGE `addr_end < addr` is interpreted as `addr_end == addr` (=
effectively SINGLE).

## MASK mode

Matches if `(value & mask) == (ref & mask)`. Useful for groups of
addresses / ports sharing a bit pattern.

```
Match Mode: Mask
Address:    0x0042
Mask:       0x00FF
```

Triggers for addresses with the low byte `0x42`: `0x0042`, `0x0142`,
`0x0242`, ... `0xFF42`.

### Examples

1. **Low byte watch** - whenever PC hits an address ending in `0x42`:
   ```
   Type:       PC_EXEC
   Address:    0x0042
   Mask:       0x00FF
   Match Mode: Mask
   ```

2. **I/O port group** - track ports `0xCC..0xCF` (e.g. the WD279x FDC
   register group):
   ```
   Type:       IORQ_W
   Port:       0xCC
   Mask:       0xFC
   Match Mode: Mask
   ```

3. **PEHU bank group** - track banks 8..11 (share the upper 5 bits):
   ```
   Type:       PC_EXEC (or MEM_R/W)
   Zone:       MMEXT_BANK
   Bank ID:    08
   Mask:       18
   Match Mode: Mask  (bank Match Mode)
   ```

### Edge cases

- `Mask = 0` matches anything (= equivalent to a missing condition).
  The UI shows a warning but accepts. If you want "always fire", use
  the GLOBAL type with `expr = 1` instead.
- `Mask = 0xFFFF` (or `0xFF` for a bank) is equivalent to SINGLE.

## SP WINDOW mode

For an `SP_THRESHOLD` BP. The trigger fires if SP leaves the allowed
range `[lower..upper]`. Edge-triggered - to avoid spam for a
pre-existing outside SP at emu startup (= it must first be inside,
then transition out).

```
Type:        SP_THRESHOLD
Lower bound: 0xFE00
Upper bound: 0xFFFE
Mode:        Window
```

Triggers if SP transitions from a value inside `[0xFE00..0xFFFE]` to
outside (e.g. SP dropped to `0xFD00` or rose to `0xFFFF`).

### Edge semantics

On the hooked instruction we compare `(was_inside, is_outside)`:
- `was_inside && is_outside` = trigger
- `was_inside && is_inside` = no-op (level)
- `!was_inside && is_outside` = sustained outside, no trigger
- `!was_inside && is_inside` = SP came back to the window, no trigger

That is, the trigger requires a **transition**.

## Interaction with Zone awareness

A match mode on **address** is applied **in addition to** the Zone
filter. Example:

```
Type:       PC_EXEC
Zone:       MMEXT_BANK
Bank ID:    9
Address:    0x2000
End Addr:   0x3FFF
Match Mode: Range  (addr Match Mode)
```

The BP fires only if:
1. Memext PEHU is connected
2. The currently mapped overlay bank == 9
3. PC is in `[0x2000..0x3FFF]`

The Bank Match Mode (separate from the addr Match Mode) can cover a
group of banks (RANGE/MASK on bank).

## Address as (MMEXT_BANK MEM_R / MEM_W)

For a MEM_R / MEM_W breakpoint in the `MMEXT_BANK` zone there is an
additional per-BP `Address as` choice that decides how the `Address`
field is interpreted:

| Address as | Address field | When the BP fires |
|------------|---------------|-------------------|
| `CPU view` (default) | Z80 address `0x0000-0xFFFF` | on a read/write to that CPU address while the bank is currently mapped into the CPU window (= the previous behavior) |
| `Bank offset` | offset `0x0000-0x1FFF` within the PEHU bank | on a read/write to the pair `(bank_id, offset)` regardless of which CPU window the bank is currently mapped into |

- The choice is available **only for the `MMEXT_BANK` zone and only for
  the PEHU memext** (8 KB bank, offset `0x0000-0x1FFF`).
- In the Edit BP dialog, when `Zone = MMEXT_BANK`, a dropdown
  `Address as: CPU view | Bank offset` appears. When `Bank offset` is
  selected the `Address:` row is renamed to `Offset:`.
- **The Match Mode (SINGLE / RANGE / MASK) works in both modes.** In
  `Bank offset` mode the Match Mode is applied to the offset
  (`0x0000-0x1FFF`) instead of the CPU address.

Difference: `Bank offset` tracks the physical bank across remapping - it
catches a write to `(bank_id, offset)` in whichever CPU window the bank
is currently mapped into (even if that window changes between writes).
`CPU view` in contrast is a banking-aware view of one specific CPU
address. A write only reaches the bank while it is mapped somewhere - an
unmapped bank is not hit by a CPU write in either mode.

## Interaction with the Condition expression

The match mode is evaluated **BEFORE** the condition expression. If
the match mode does not pass, the condition is never evaluated (=
optimization, hot path skip).

The sequence per BP hit:
1. Effective enabled check
2. Zone awareness filter
3. **Match mode check**
4. Skip count
5. Condition expression
6. Hits counter
7. Hit count
8. Action execute

## Persistence

Match modes are serialized as strings in the `.bpt` JSON file:

```json
{
  "addr_match_mode": "RANGE",
  "addr_mask": "0xFFFF",
  "port_match_mode": "SINGLE",
  "port_end": "0x00",
  "port_mask": "0xFFFF",
  "port_mode": "8BIT",
  "bank_match_mode": "SINGLE",
  "bank_id_end": 0,
  "bank_id_mask": "0xFF",
  "sp_mode": "SINGLE",
  "sp_upper": "0x0000"
}
```

`.bpt` files without the match-mode keys are loaded with the default
`SINGLE`.

## PC_EXEC MASK

PC_EXEC with MASK match mode is fully supported. SINGLE and RANGE
PC_EXEC use a per-PC bytemap (O(1) per-instruction lookup), while
MASK PC_EXEC is evaluated by iterating over the list of non-SINGLE
BPs (= a sparse mask cannot be enumerated into a bytemap). The hot
loop has a guard that tests whether any non-SINGLE PC_EXEC BP is
registered - if not, the overhead is zero.

## IORQ Port Mode

The Z80 has two I/O addressing patterns. IORQ_R / IORQ_W therefore
has a per-BP `port_mode` choice:

| Port Mode | Z80 instruction | Address bus | Match width |
|-----------|-----------------|-------------|-------------|
| `8BIT` (default) | `IN A,(n)` / `OUT (n),A` | n on A0..A7, A8..A15 = "don't care" | low byte |
| `16BIT` | `IN r,(C)` / `OUT (C),r` | BC on A0..A15 (B = high) | full 16-bit |

**Default `8BIT`** = every IORQ with a matching low byte of the port
fires (= most Z80 software).

**`16BIT`** distinguishes e.g. `0x42CE` from `0x88CE` - useful for
hardware with extended port addressing via the B register (e.g.
unicard / internal peripherals with a 16-bit decoder).

The Match Mode (`SINGLE` / `RANGE` / `MASK`) is applied **inside the
chosen port_mode**:

- `8BIT` + RANGE / MASK = compares only the low byte.
- `16BIT` + RANGE / MASK = compares the full 16-bit BC.

**Persistence:** `port_mode` as the string `"8BIT"` / `"16BIT"`. Files
without this key load with the default `8BIT`. The full `port` value
is preserved in the file independently of the mode (= a switch 16BIT
-> 8BIT does not lose the upper byte in persistence; the runtime
just matches the low byte).

**JSON example:**

```json
{
  "type": "IORQ_W",
  "port": "0x42CE",
  "port_end": "0x0000",
  "port_match_mode": "SINGLE",
  "port_mask": "0xFFFF",
  "port_mode": "16BIT"
}
```

## Performance

Per-instruction overhead for non-SINGLE modes is marginal:
- SINGLE: 1 compare
- RANGE: 2 compares
- MASK: 2 AND + 1 compare

## Related documents

- `expression-syntax.md` - condition expression
- `memory-map.md` - banking + zone awareness
