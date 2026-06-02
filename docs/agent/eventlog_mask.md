# EventLog category mask

The Event Viewer ring buffer filters events by a 64-bit mask. Bit `i`
in the mask enables category `i` (per `en_EVENTLOG_CATEGORY`).

Set via `emu_eventlog_set_mask` (atomic replace). Mask can be passed
as a JSON integer (0..2^63-1) or as a hex string (full 64-bit range,
e.g. `"0xFFFFFFFFFFFFFFFF"`, underscores allowed as visual separators).

## Category bit assignments

| Bit | Name | Description |
|-----|------|-------------|
| 0 | `CPU_INT` | CPU interrupt acknowledge / state transition |
| 1 | `CPU_PIN_EDGE` | Z80 pin edge (INT / NMI / WAIT / BUSREQ) |
| 2 | `IRQ_ACK_IM2` | IM 2 vectored IRQ acknowledge with vector read |
| 3 | `IORQ_IN` | Z80 `IN` to a port |
| 4 | `IORQ_OUT` | Z80 `OUT` to a port |
| 5 | `MMIO_R` | Memory-mapped I/O read |
| 6 | `MMIO_W` | Memory-mapped I/O write |
| 7 | `GDG_MODE` | GDG mode / DMD register change |
| 8 | `GDG_BANKING` | GDG memory banking event |
| 9 | `GDG_HWSCROLL` | GDG hardware scroll change |
| 10 | `GDG_COLORS` | GDG palette / colour register write |
| 11 | `GDG_VIDEO` | GDG video frame / scanline events |
| 12 | `PIO8255` | Intel 8255 PPI (PA / PB / PC, control word) |
| 13 | `CTC8253` | Intel 8253 CTC (counter / control) |
| 14 | `PIOZ80` | Z80 PIO (control / data / IRQ) |
| 15 | `PSG` | SN76489 / AY-3-8910 sound generator |
| 16 | `FDC` | WD279x floppy controller |
| 17 | `MEMEXT` | Memory expansion (Luftner / PEHU) |
| 18 | `BP_FIRE` | Breakpoint fire event |
| 19 | `USER_MARK` | User-injected mark (`emu_eventlog_mark` etc.) |
| 20 | `CPU_CTRL` | CPU control events (RESET, HALT, sleep, ...) |
| 21 | `GDG_WFRF` | GDG WF / RF VRAM plane control |
| 22 | `QD` | Quick Disk events |
| 23 | `RD` | RAM Disk events |
| 24 | `SYS` | Subsystem / system-level events |

Bits beyond 24 are reserved and currently ignored. Bit 63 is unreliable
when passed as a JSON integer (signed `gint64` interpretation) - use
a hex string for it.

## Mask recipes

| Goal | Mask | Note |
|------|------|------|
| All events | `"0xFFFFFFFF"` | covers bits 0..31 |
| Memory accesses only (MMIO + IORQ) | `(1<<3) \| (1<<4) \| (1<<5) \| (1<<6)` = `0x78` | reads + writes, ports + MMIO |
| IRQ subsystem | `(1<<0) \| (1<<1) \| (1<<2)` = `0x07` | CPU INT + pin edges + IM2 ack |
| Video / GDG | `(0xF << 7) \| (1<<21)` = `0x20780` | bits 7-10 + bit 21 |
| Audio | `1 << 15` = `0x8000` | PSG only |
| Breakpoint hits only | `1 << 18` = `0x40000` | useful when many BPs are active |
| User marks only | `1 << 19` = `0x80000` | filter to manual annotations |
| Nothing (logger silent but ring kept) | `0` | drains continue, ring stays empty |

## How to set

```json
// JSON-RPC tools/call argument
{"mask": 120}                              // = 0x78, memory accesses
{"mask": "0xFFFFFFFFFFFFFFFF"}             // full 64-bit
{"mask": "0xff_ff"}                        // underscore separators allowed
```

Returns `{"mask_hex": "0x...."}` with the 16-digit zero-padded value
that was actually applied (round-trip confirmation).

## Performance tip

The mask is checked **once** per event before any payload formatting,
so disabling categories you do not care about is cheap and reduces ring
churn (= more retained history of the categories you do want).

## Related

- `emu_eventlog_set_capacity` - resize the ring (destructive).
- `emu_eventlog_get_event` - read events by index.
- Source: `src/emulator/debugger/eventlog.h` for the full enum
  definition.
