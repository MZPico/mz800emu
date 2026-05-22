# trace-suite - user documentation

A set of 5 independent sequential logging subsystems of the mz800new
emulator for in-depth analysis of executed code, I/O traffic,
interrupts, HW state changes and user markers.

## 5 subsystems

| Subsystem | What it logs                                       | Bytes/event |
|-----------|----------------------------------------------------|-------------|
| cputrack  | Every executed Z80 instruction (PC, bytes, WAIT)   | 12          |
| iorqlog   | Every IORQ IN/OUT (port, value, source addr)       | 24          |
| intlog    | INT pin changes, IM/IFF state, EI                  | 24          |
| hwlog     | Changes of HW chip state (GDG, PSG, CTC, PIO, MEMEXT, QD/FDC/RD) | 24 |
| marklog   | User markers from the `mark "name"` BP action      | 24          |

Each subsystem is independent - you can enable just one, any
combination, or all of them.

## Activation

### CLI options

```bash
./mz800emu.exe --cputrack-mode=always --cputrack-dir=./logs/run1
./mz800emu.exe --iorqlog-mode=window --intlog-mode=always
```

Per-subsystem options (each subsystem has identical ones):
- `--<sys>-mode=off|window|always`
- `--<sys>-dir=<path>` (default: `./trace-suite/`)
- `--<sys>-name=<basename>` (default: same as sys, e.g. `cputrack`)
- `--<sys>-chunk-mb=<N>` (default: 64)
- `--<sys>-max-total-mb=<N>` (default: 0 = unlimited)
- `--<sys>-save-on-exit=on|off` (default: on)

`<sys>` = `cputrack` / `iorqlog` / `intlog` / `hwlog` / `marklog`.

Marklog also has:
- `--marklog-stdout=on|off` (default: `on`) - back-compat printout
  `[BP-MARK] <name>` to stdout when the STMT_MARK BP action fires.
  Independent of `--marklog-mode` (= you can have stdout without the
  binary log, or the binary log without stdout, or both / neither).

Hwlog also has:
- `--hwlog-hs-decimation=<N>` (default: `0` = HS/HBLN edges OFF;
  `N > 0` emit every Nth HS/HBLN edge. Full rate is ~31000 events/sec
  which destroys real-time performance - reasonable values are
  `100`-`1000` for troubleshooting or `1` for a full trace of a short
  section.)

#### Shorthand `--all-traces-*`

```bash
./mz800emu.exe --all-traces-mode=always --all-traces-dir=./logs/run-1/
./mz800emu.exe --all-traces-mode=always --hwlog-mode=off   # everything except hwlog
```

`--all-traces-mode <off|window|always>` applies the value to all 5
subsystems at once; `--all-traces-dir <path>` does the same for the
output directory. A per-subsystem option (`--cputrack-mode` etc.) has
precedence - the shorthand fills in only those subsystems where the
user did not pass a per-subsys option.

### GUI menu

Debugger Settings -> Trace Suite -> [CPU Track | IORQ Log | Interrupt Log | HW Log | Marker Log]

Per-subsystem: radio (Off / Only With Debug Window / Always) + Save on Exit toggle.

#### Status indicator in the window title

If at least one subsystem is actively recording, a marker
`[trace: <flags>]` is appended after the standard text of the SDL main
window title, where `<flags>` is a space-separated list of letters:

| Letter | Subsystem |
|--------|-----------|
| `C`    | cputrack  |
| `I`    | iorqlog   |
| `N`    | intlog    |
| `H`    | hwlog     |
| `M`    | marklog   |

If a subsystem hit the `max_total_mb` limit and recording was stopped
(truncated), a suffix `!` appears after the letter (e.g. `H!`). If no
subsystem is active, the bracket does not appear in the title at all
(= in the off state the title is unchanged).

Examples:

```
MZ-800 - normal speed: 100.00 %, FB-FPS: 50.00
MZ-800 - normal speed: 100.00 %, FB-FPS: 50.00 [trace: C I N H]
MZ-800 - normal speed: 100.00 %, FB-FPS: 50.00 [trace: C I]
MZ-800 - normal speed: 100.00 %, FB-FPS: 50.00 [trace: C I N H!]
```

Updated periodically via an SDL timer (~1x/s).

### INI config

Per-subsystem section in `mz800emu.ini` / `mz1500emu.ini`:

```ini
[TRACE_CPUTRACK]
mode=OFF
dir=trace-suite
name=cputrack
chunk_mb=64
max_total_mb=0
save_on_exit=1

[TRACE_IORQLOG]
... (same structure)

[TRACE_INTLOG]
...

[TRACE_HWLOG]
...

[TRACE_MARKLOG]
mode=OFF
dir=trace-suite
name=marklog
chunk_mb=64
max_total_mb=0
save_on_exit=1
stdout_enabled=1                # back-compat printout [BP-MARK] to stdout
```

## Mode semantics

| Mode        | Activation                                          |
|-------------|------------------------------------------------------|
| OFF         | Subsystem off, no overhead even in the hot path     |
| WITH_WINDOW | Active only while the debug window (Alt+D) is open  |
| ALWAYS      | Active continuously, regardless of the debug window |

## Output structure

Per-recording directory `<dir>/`:

```
trace-suite/
    cputrack.json                  # meta + chunks list
    cputrack_initial_ram.bin       # 64 KB CPU RAM dump at start
    cputrack_initial_vram.bin      # 32 KB VRAM
    cputrack_initial_exvram.bin    # 32 KB EXVRAM (mz800)
    cputrack_initial_pcg.bin       # 24 KB PCG (mz1500)
    cputrack_initial_memext.bin    # 512 KB Memext (if connected)
    cputrack.000.bin               # chunk 0 (per-instr events)
    cputrack.001.bin               # chunk 1
    ...
    iorqlog.json
    iorqlog.000.bin
    ...
    intlog.json
    intlog.000.bin
    ...
    hwlog.json
    hwlog.000.bin
    ...
    marklog.json                   # meta + chunks list + markers registry
    marklog.000.bin                # chunk 0 (per-marker events 24 B)
    ...
```

## meta.json format

Each subsystem generates its own meta.json:

```json
{
  "subsys": "cputrack",
  "platform": "MZ-800",
  "pxclk_freq": 17734475,
  "cpu_divider": 5,
  "pxclk_per_screen": 97344,
  "truncated": false,
  "truncated_reason": "",
  "chunks": [
    { "index": 0, "file": "cputrack.000.bin",
      "bytes": 67108864,
      "start_pxclk": 12345,
      "start_cpuclk": 2469,
      "start_screens": 0 },
    ...
  ],
  "subsys_header": {
    ...per-subsystem specific fields...
  }
}
```

`chunks[i].start_*` enables O(1) seek to a chunk + linear scan inside
the chunk for timestamp reconstruction (cputrack: by summing
insn_T_states + wait_clk; iorqlog/intlog/hwlog: explicit timestamp in
every event).

## Per-subsystem event format

### cputrack (12 B)

| Offset | Size | Field                     |
|--------|------|---------------------------|
| 0      | 2 B  | regPC (uint16 LE)         |
| 2      | 1 B  | insn_length (1-4)         |
| 3      | 4 B  | insn_bytes (only the first insn_length valid) |
| 7      | 4 B  | wait_clk (uint32 LE)      |
| 11     | 1 B  | reserved                  |

`wait_clk == 0` = a normal instruction, `0xFFFFFFFF` = saturated
(typ. a long HALT loop >20 min at 3.5 MHz).

HALT/self-loop collapse: repeated execution of the same insn (HALT,
JP $, JR $-2) is collapsed into a single event with wait_clk = the
summed time.

### iorqlog (24 B)

| Offset | Size | Field                                  |
|--------|------|----------------------------------------|
| 0      | 8 B  | pxclk_total                            |
| 8      | 4 B  | screens_total                          |
| 12     | 4 B  | pxclk_in_screen                        |
| 16     | 1 B  | event_type (IORQ/MREQ/IORQ_UNCONNECTED) |
| 17     | 1 B  | direction (IN=0/OUT=1)                 |
| 18     | 2 B  | source_addr (regBC for IORQ, regPC for MREQ) |
| 20     | 2 B  | port_or_addr                           |
| 22     | 1 B  | value                                  |
| 23     | 1 B  | pulse_duration_pxclk                   |

Emitted event types:
- `IORQ` - regular IN/OUT (every port access via the Z80 IORQ cycle)
- `MREQ_MAPPED` - memory access into 0xE000-0xE008 (PIO8255 / CTC8253 /
  GDG status). For MZ-1500 unconditionally; for MZ-800 only in MZ-700
  mode with the upper ROM mapped in
- `IORQ_UNCONNECTED` - "ghost bus" IN to an unmapped port (the value
  returned comes from the bus latch)

### intlog (24 B)

| Offset | Size | Field                                  |
|--------|------|----------------------------------------|
| 0      | 8 B  | pxclk_total                            |
| 8      | 4 B  | screens_total                          |
| 12     | 4 B  | pxclk_in_screen                        |
| 16     | 1 B  | event_class (PIN_EDGE/CPU_INT_STATE/PIO_STATE/IRQ_ACK_IM2) |
| 17     | 1 B  | source_chip                            |
| 18     | 1 B  | source_pin (0-15 for PIOZ80@P[A,B][0..7]) |
| 19     | 1 B  | edge (RISING=1/FALLING=2)              |
| 20     | 4 B  | state_bitmask (IM/IFF1/IFF2/EI/RETI or (vector_table_addr \| isr_addr<<16) for IRQ_ACK_IM2) |

Covered event classes:
- `PIN_EDGE` - rising/falling on the CPU INT pin, source_chip = CTC2 /
  PIOZ80 / PIOZ80_PA0..PA7 / PIOZ80_PB0..PB7 / FDC. Per-pin PIOZ80
  granularity is active.
- `CPU_INT_STATE` - EI events with the current IM/IFF1/IFF2; RETI events
  (bit `INTLOG_STATE_BIT_RETI`).
- `PIO_STATE` - changes in the PIOZ80 internal state (READY / ARMED /
  IM2_JUMP / RETI_APPLIED bits).
- `IRQ_ACK_IM2` - 32-bit encoded payload: low 16 bits = vector_table_addr
  (entry in the IM 2 table), high 16 bits = isr_addr (= ISR address that
  the Z80 will load into PC). Source_chip distinguishes the vector
  source: `PIOZ80_PORT_A` / `_PORT_B` (vector from the PIOZ80 daisy
  chain), `CTC2` / `FDC` (INT pin pulled, vector = 0, dispatch onto
  (I:00)), `VECTOR_BUS_LATCH` (other / not resolvable). Key for
  debugging ISR dispatch.

### hwlog (24 B)

See the separate `docs/cz/debugger/formats/HW-log_format.md`.

Covered per-chip register write hooks for:
- **GDG** (mz800 + mz1500) - MODE / BANKING / HWSCROLL / COLORS / WFRF
- **GDG_VIDEO** sync events (VBLN / VS edges; HS / HBLN with decimation)
- **PSG** (SN76489 register write) - audio engine RE
- **CTC8253** (control + counter writes)
- **PIO8255** (port A/B/C + control writes)
- **MEMEXT** (bank switch - LUFTNER + PEHU)
- **QD / FDC / RD** (register write events on disk operations)
- **PIOZ80** (Mode/Vector/INT_CTRL/Mask/IO_Select/Data writes, Data
  reads, Bus input change, M2 IRQ ack, RETI applied)

Plus an initial state binary snapshot in `<dir>/<name>_initial_state.bin`
(TLV per chip - chip_id + length + payload, EOF marker 0xFF). Each
payload is a field-by-field little-endian serialization independent of
compiler ABI - layout see `docs/cz/debugger/formats/HW-log_INITIAL_STATE_format.md`.

The header contains per-chip dividers:
`psgclk_divider_cpuclk`, `tempo_divider_screen_rows`,
`ctc0_clk1m1_divider_pxclk`, `pio8255_cursor_divider_screens`.

### marklog (24 B)

See the separate `docs/cz/debugger/formats/MARK-log_format.md`.

Logs user markers triggered from the BP action `mark "name"`. The
marker name is pre-registered during BP parse and is represented in the
binary by a stable `uint16 marker_id`. The id -> name mapping is dumped
into `meta.json` `subsys_header.markers`. Marklog additionally has an
optional stdout printout (`[BP-MARK] <name>`) controlled by the
separate `stdout_enabled` flag.

## Performance impact

- OFF states: zero impact (a single if test, the branch predictor
  learns "always false")
- ON states: emulation slows down significantly (synchronous disk
  write on chunk swap, recording path through slow callbacks).
  **Acceptable - this is a debugging mode.** The console message
  `[trace-suite] <subsys>: chunk N swap to disk (XX B)` notifies of a
  flush.

## Limits

- chunk-mb (default 64 MB): RAM buffer per subsystem. Higher = less
  frequent flush, higher peak RAM
- max-total-mb (default 0 = unlimited): hard limit on the total
  recording size. When reached, the subsystem stops, meta.json gets
  `"truncated": true, "truncated_reason": "max_total_mb"` and a console
  message `[trace-suite] <subsys>: max-total-mb=N reached, recording
  stopped`.

## External parsing

External tooling (Python, C, ...) can parse meta.json + the binary
chunks according to the documented formats. No vendor-locked formats.

Re-simulation: hackers / aicoders / RE tooling can **compute** memory
accesses by deterministic re-simulation from (initial RAM dump in the
cputrack header + the sequence of opcodes + IN values in iorqlog). No
HW component on MZ-800/700/1500 modifies RAM autonomously without a
Z80 instruction.
