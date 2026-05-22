# CPU Tracking Log - export file format

## What is cputrack

**cputrack** is the first of four trace-suite subsystems (together with
`iorqlog`, `intlog`, `hwlog`). It logs **one record per completed Z80
instruction** during emulator run. Used for reverse engineering, hot
path analysis, identification of self-modifying code and external
re-simulation of memory state.

cputrack is controlled from the menu `Debugger Settings -> Trace Suite
-> CPU Track`. It has three modes:

- `Off` (default) - no recording, no overhead.
- `Only With Debug Window` - active only when the debug window is open.
- `Always` - always active.

The options `Save on Exit`, `Set directory...`, `Chunk MB` and `Max
Total MB` in the same menu control finalization behavior, target
directory and size limits. All options are persistent in the emulator
INI configuration (section `[TRACE_CPUTRACK]`, keys `mode`, `dir`,
`name`, `chunk_mb`, `max_total_mb`, `save_on_exit`).

Recording runs in the shared slow path with CDL/cpuhist (callback
swap); the emulator hot path without active diagnostics stays unchanged
relative to the vanilla version.

## Export directory structure

```
<dir>/
    <name>.json                    # meta + chunks anchor + initial regs
    <name>_initial_ram.bin         # 64 KB CPU RAM dump
    <name>_initial_vram.bin        # 32 KB VRAM (MZ-800 only)
    <name>_initial_exvram.bin      # 32 KB EXVRAM (MZ-800 only)
    <name>_initial_cgram.bin       # 4 KB CG-RAM (MZ-1500 only)
    <name>_initial_pcg.bin         # 24 KB PCG (MZ-1500 only)
    <name>_initial_memext.bin      # 512 KB Memext (if connected)
    <name>_initial_rdN.bin         # per-Ramdisk (if active)
    <name>.000.bin                 # chunk 0 - per-instr events
    <name>.001.bin                 # chunk 1
    ...
```

`<name>.json` is the text metadata file (see below). `<name>_initial_*.bin`
are snapshots of physical memories at the moment recording starts
(= header). `<name>.NNN.bin` are flat sequences of per-instruction
events.

## Per-event record (12 B fixed)

| Offset | Size     | Field                                          |
|--------|----------|------------------------------------------------|
| 0      | 2 B      | regPC (uint16 LE)                              |
| 2      | 1 B      | insn_length (1-4)                              |
| 3      | 4 B      | insn_bytes (only the first insn_length bytes are valid) |
| 7      | 4 B      | wait_clk (uint32 LE)                           |
| 11     | 1 B      | reserved (alignment to 12 B)                   |

Endianness of the write is host byte order; on all supported emulator
platforms (Windows x86_64, Linux x86_64) this is little-endian.

### Field regPC

Address of the instruction at the moment of its execution (= PC before
executing the instruction, not PC after it). For `JP 1234h`, `regPC`
is the address of the `JP` opcode, not the jump target.

### Field insn_length

Number of valid bytes in `insn_bytes`. Z80 instructions have length 1-4 B
(prefix `CB`, `DD`, `ED`, `FD` + opcode + optional offset/imm bytes).

### Field insn_bytes

Up to 4 bytes of opcode + operands. Bytes beyond `insn_length` are
undefined (likely 0, but the parser must not rely on it).

From `regPC` + `insn_bytes[0..insn_length-1]` one can fully reconstruct:
- what the Z80 executed (= disassemble)
- the standard length in T-states (Z80 ISA table)
- which registers the instruction reads / writes
- which memory operations it does (from the register state that an
  external re-simulator tracks itself)

### Field wait_clk

Number of **extra WAIT T-states** beyond the standard length of the
instruction.

| Value | Meaning |
|-------|---------|
| `0` | No WAIT (common case for most instructions in the usual emulator state). |
| `1..0xFFFFFFFE` | WAIT lasted N CPU T-states. |
| `0xFFFFFFFF` | **Saturated sentinel** - WAIT was longer than ~20 minutes at 3.5 MHz, the value overflowed uint32. The external parser knows "long nothing, cannot determine exactly how long". A real Sharp would by then have lost DRAM anyway (refresh); the emu can hold the pause indefinitely. |

#### Source of WAIT T-states

The `wait_clk` value comes from two hot-path places where the emulator
inserts WAIT cycles:

1. **MREQ to VRAM in MZ-700 mode** - WAIT due to synchronization with
   HBLN (= so that the CPU does not write to VRAM during video read).
   Size depends on current raster position.
2. **OUT to PSG** - WAIT due to synchronization of CPU CLK with PSGCLK
   (PSG is 1.108 MHz, CPU 3.546 MHz; write must hit the PSG cycle
   boundary).

The WAIT T-state accumulator is a saturating uint32. If HALT/self-loop
collapse is active, accumulation continues across iterations (= the
sum of all WAIT + all standard T-states from the first iteration).

`insn_length`/`insn_bytes` in the event corresponds to the **standard
instruction** (= without WAIT). The standard length in T-states is
derivable from the opcode via Z80 ISA (see below).

The length of the instruction itself in T-states is **not stored** -
it is deterministically derivable from the opcode according to the
Z80 ISA table. For conditional jumps (taken/not-taken differ in
T-states), external post-processing computes it from the flag state
(from re-simulation).

### HALT and self-loop collapse

HALT, `JP $`, `JR $-2`, `CALL` to itself (= any instruction that leaves
PC at the same address and repeats) is collapsed into **a single event**
with `wait_clk` = total time spent in the loop (until exit via IRQ
accept / state change).

Consequence: the tight-loop "wait for IRQ" idiom in MZ-800 monitor /
games (`HALT` or `JR $-2` in a polling loop) does not generate tens of
thousands of events per second, but one event with `wait_clk` equivalent
to the waiting time. This is a side-effect of cputrack design - **without
collapse, the log would be unusable on real software**.

An external re-simulator detects a collapsed HALT trivially: opcode =
`0x76` (HALT) + non-zero `wait_clk`. It detects a collapsed self-loop:
the instruction changes PC to PC, and `wait_clk` > 0.

## meta.json

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
    {
      "index": 0,
      "file": "cputrack.000.bin",
      "bytes": 67108864,
      "start_pxclk": 0,
      "start_cpuclk": 0,
      "start_screens": 0
    },
    {
      "index": 1,
      "file": "cputrack.001.bin",
      "bytes": 67108864,
      "start_pxclk": 39728432,
      "start_cpuclk": 7945686,
      "start_screens": 408,
      "start_pxclk_in_screen": 21344
    }
  ],
  "subsys_header": {
    "start_pxclk": 0,
    "start_pxclk_in_screen": 0,
    "start_screens": 0,
    "start_cpuclk": 0,
    "event_record_size": 12,
    "initial_state": {
      "regs": {
        "AF": "0xFFFF", "BC": "0x0000", "DE": "0x0000", "HL": "0x0000",
        "IX": "0x0000", "IY": "0x0000", "SP": "0x0000", "PC": "0x0000",
        "AF_alt": "0x0000", "BC_alt": "0x0000",
        "DE_alt": "0x0000", "HL_alt": "0x0000",
        "I": "0x00", "R": "0x00",
        "IM": 0, "IFF1": 0, "IFF2": 0
      },
      "memory_dumps": [
        { "region": "ram",      "file": "cputrack_initial_ram.bin",      "bytes": 65536  },
        { "region": "vram",     "file": "cputrack_initial_vram.bin",     "bytes": 32768  },
        { "region": "exvram",   "file": "cputrack_initial_exvram.bin",   "bytes": 32768  },
        { "region": "memext",   "file": "cputrack_initial_memext.bin",   "bytes": 524288 }
      ]
    }
  }
}
```

### Key fields

- `subsys` - always `"cputrack"`.
- `platform` - `"MZ-800"`, `"MZ-700"` or `"MZ-1500"`.
- `pxclk_freq` - pixel clock frequency (Hz). MZ-800/700/1500 standard:
  17734475 (= 17.7 MHz). `cpu_freq = pxclk_freq / cpu_divider` can be
  derived.
- `cpu_divider` - pxCLK divider for CPU CLK. MZ-800: `5` (CPU 3.546895 MHz).
- `pxclk_per_screen` - number of pxCLK per one screen (raster cycle).
  Usable for raster-aligned analysis.
- `chunks[]` - list of chunks in order. Each chunk is an independent
  binary file.
- `truncated` - `true` if recording reached the `max_total_mb` limit.
- `truncated_reason` - explanation (`"max_total_mb"` or empty).

### chunks[i] anchor

Each chunk has an anchor with `start_pxclk`, `start_cpuclk`,
`start_screens` at the moment of chunk start. This enables **O(1) seek
to a chunk** + linear scan inside it to reconstruct the timestamp of
any specific instruction:

```
cumul_cpuclk(N) = chunk.start_cpuclk + sum_{i=0..N}( insn_T_states[i] + wait_clk[i] )
cumul_pxclk(N)  = chunk.start_pxclk + (cumul_cpuclk(N) - chunk.start_cpuclk) * cpu_divider
screens_at(N)   = floor(cumul_pxclk(N) / pxclk_per_screen)
pxclk_in_screen_at(N) = cumul_pxclk(N) mod pxclk_per_screen
```

`insn_T_states[i]` is derived from opcode according to the Z80 ISA
table.

### subsys_header.initial_state

`regs` contains a complete snapshot of Z80 CPU registers at the moment
recording starts (before the first logged event). Values are hex strings
(uint16 as `"0xABCD"`, uint8 as `"0xCD"`).

`memory_dumps[]` references binary files with initial dumps of physical
memories. Each entry has `region` (= symbolic name), `file` (= relative
path against the meta file directory), `bytes` (= size).

The initial state + event sequence is a **hermetically closed dataset**
- an external re-simulator deterministically derives the memory state
at any moment from it (= no HW on MZ-800/700/1500 changes RAM
autonomously; all data appears via IORQ, which we log separately in
`iorqlog`).

## Initial memory dumps

Individual regions may differ per platform. The header contains only
those that exist / are connected for the given architecture.

### MZ-800

| File | Size | Description |
|------|------|-------------|
| `<name>_initial_ram.bin` | 64 KB | Main DRAM |
| `<name>_initial_vram.bin` | 32 KB | VRAM 4 banks (linear) |
| `<name>_initial_exvram.bin` | 32 KB | EXVRAM (linear) |
| `<name>_initial_memext.bin` | 512 KB | Memext RAM (only if connected) |
| `<name>_initial_rd0.bin` | variable | RAM-disk 0 (only if active) |
| `<name>_initial_rd1.bin` | variable | RAM-disk 1 (if active) |

ROM is **not stored** - it is deterministically known from compile-time
embedded ROM image (= version in `meta.json` via `platform`).

### MZ-1500

| File | Size | Description |
|------|------|-------------|
| `<name>_initial_ram.bin` | 64 KB | Main DRAM |
| `<name>_initial_cgram.bin` | 4 KB | CG-RAM |
| `<name>_initial_pcg.bin` | 24 KB | 3x 8 KB PCG bank (R/G/B) |
| `<name>_initial_memext.bin` | 512 KB | Memext (if connected) |

## Usage

### From the UI

`Debugger Settings -> Trace Suite -> CPU Track`:

1. Select recording mode (Off / Only With Debug Window / Always).
2. Optionally enable `Save on Exit` (default on).
3. Set the target directory and basename, optionally `chunk_mb` /
   `max_total_mb`.
4. Run the emulated program. Recording runs in the background.

### Loading data in Python (example)

```python
import json, struct
from pathlib import Path

def load_cputrack(meta_path):
    meta_path = Path(meta_path)
    meta = json.loads(meta_path.read_text())
    base_dir = meta_path.parent

    events = []
    for chunk in meta["chunks"]:
        data = (base_dir / chunk["file"]).read_bytes()
        n_events = chunk["bytes"] // 12
        for i in range(n_events):
            o = i * 12
            pc          = struct.unpack_from("<H", data, o + 0)[0]
            insn_length = data[o + 2]
            insn_bytes  = data[o + 3 : o + 3 + insn_length]
            wait_clk    = struct.unpack_from("<I", data, o + 7)[0]
            events.append((pc, insn_length, bytes(insn_bytes), wait_clk))
    return meta, events

meta, events = load_cputrack("./trace-suite/cputrack.json")
# Example: how many HALT instructions there were
halt_count = sum(1 for (_, l, b, _) in events if l == 1 and b[0] == 0x76)
```
