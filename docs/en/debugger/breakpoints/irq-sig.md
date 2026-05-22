# Breakpoint - IRQ signal source filter

The `IRQ_SIG` breakpoint triggers on the **raise edge** of a
peripheral INT line (= pre-dispatch). It filters per source: PIOZ80
PORT_A/B, CTC2, FDC, Other.

This BP type complements `IRQ` with a second perspective:

| Type | When it fires | Filter |
|------|---------------|--------|
| `IRQ` | CPU acknowledged INT (= dispatched ISR) | IM mode + RST opcode + IM 2 vector/ISR |
| `IRQ_SIG` | Peripheral X raised the INT line | Source (PIOZ80 A/B, CTC2, FDC, Other) |

`IRQ` fires from the ISR side (= "I started to service an interrupt");
`IRQ_SIG` sees the peripheral side (= "peripheral X wants my
attention; the CPU has not acknowledged it yet").

## When the enforce is called

The hook is called **after** the interrupt bus mask is updated and
**before** the Z80 dispatch. The hook holds a snapshot of the previous
bus mask and detects a **raise edge** (bits 0 -> 1).

A sustained level (= peripheral holds the INT line across several
calls) does NOT fire repeatedly - only the edge of the transition. A
pure 1 -> 0 edge (= peripheral releases INT) also does not fire (=
the filter monitors the raise edge).

```
prev_irq:  0b 00000000
curr_irq:  0b 00000010   (PIOZ80 just raised)
edge:      0b 00000010   <- fire for a BP with PIOZ80 source
```

If several bits raise at the same time (= one frame), all BPs with a
matching source get a fire (= multi-source detect).

## Source bits

| Bit | Meaning |
|-----|---------|
| 0   | Z80 PIO port A IRQ |
| 1   | Z80 PIO port B IRQ |
| 2   | CTC channel 2 (timer/counter) |
| 3   | WD279x floppy controller (FDC) |
| 4   | undetectable / bus latch (OTHER) |

### PIOZ80 sub-detection

In the interrupt bus mask the Z80 PIO is a single bit for the entire
PIO chip; A vs B is distinguished at runtime via the current port
that holds the INT pin in the daisy chain. On a PIOZ80 raise the
enforce reads this state and maps it to port A or B.

Race-safe fallback: if the current port is not yet set, the hook
reports the source as `OTHER`.

### Other (bus latch)

The `OTHER` bit catches:
- Future MZ peripherals with their own INT bit (= as the ecosystem
  grows).
- Bus latch / undefined sources.

In the current code no bit outside PIOZ80 / CTC2 / FDC exists, but
the filter preserves backward compatibility for future extensions.

## Multi-source semantics (OR)

A BP with multiple source bits fires if **any** selected source
raises:

```
source_mask = CTC2 | FDC

prev_irq:  0b 00000000
curr_irq:  0b 00000100   (CTC2 raised)
=> fire (CTC2 selected, raised)

curr_irq:  0b 00001000   (FDC raised on the next call)
=> fire (FDC selected, raised)

curr_irq:  0b 00000010   (PIOZ80 raised, not in the mask)
=> no fire
```

If **AND** semantics is needed ("fire only if both A and B at the
same time"), use 2 separate `IRQ_SIG` BPs with a condition expression
- the debugger has no built-in AND match.

## Default and validation

- Default for a new BP: source mask = 0 (= invalid, no source).
- The UI **requires at least 1 source bit** (= validation blocks OK).
- The setter saves the source mask 0 (= defensive UI check), but the
  enforce ignores it (= no fire).

## Persistence

The `.bpt` JSON stores the source mask as an **array of stable string
names**:

```json
{
  "type": "IRQ_SIG",
  "irq_sig_sources": ["PIOZ80_A", "CTC2"]
}
```

Stable names:
- `"PIOZ80_A"` = Z80 PIO port A
- `"PIOZ80_B"` = Z80 PIO port B
- `"CTC2"` = CTC channel 2
- `"FDC"` = WD279x FDC
- `"OTHER"` = bus latch / undetectable

The format allows adding new sources in the future without breaking
persistence (= an unknown name is ignored on load + a warning to
stderr; known ones OR-load into the mask).

A missing `irq_sig_sources` key = mask 0 (= invalid; UI validation
catches it).

## Context for condition / action

The hook fills the expression context:
- `Address` = newly raised bits (= edge bus mask)
- `Value` = matched source bits from the mask
- no `IsRead/Write/Port/Exec` flags

Example condition:
```
Value & 0x04   ; fires only if CTC2 was among the raised
```

(Same effect as a single-source BP, but allows more granular triggers
in a multi-source BP.)

## Use cases

1. **Trace peripheral activity without ISR overhead.** `IRQ_SIG`
   fires even at a time when CPU has `IFF1=0` or dispatches slowly.
   `IRQ` (post-dispatch) would be delayed/non-firing.

2. **Detect race conditions between raise and INTACK.** `IRQ_SIG`
   fires pre-dispatch, `IRQ` fires post-dispatch. Several
   instructions can run in between (= window in which the peripheral
   holds the line and the CPU finishes its previous instruction).

3. **Single source isolation.** `IRQ_SIG` with only the `FDC` source
   triggers **only** on FDC raise, not on other sources. `IRQ`
   post-dispatch cannot do this (you would have to use the IM 2
   vector filter, but that requires IM 2 + knowledge of the exact
   vector slot of the peripheral).

4. **Latch trace.** A BP with mask `OTHER` fires on an undefined
   source - useful for debugging a new peripheral (= "holds the INT
   bit, but I do not know the source").

## Performance

The hook is guard-aware: before being called it is tested whether at
least one `IRQ_SIG` BP is registered. Without a registered BP = zero
overhead on the hot path (= 1 array lookup).

With a registered BP the enforce hook checks the edge (`prev != curr`)
BEFORE iterating the BP list (= zero cost on a stable bus mask).

## Related documents

- `irq-filter.md` - post-dispatch IRQ filter
- `types.md` - catalogue of all BP types
- `expression-syntax.md` - condition grammar
