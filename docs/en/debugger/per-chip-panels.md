# Per-chip detail panels (CTC / PPI / Z80 PIO / PSG)

Four separate debug windows that show the current internal state of
the emulated machine's peripheral chips. They complement the generic
**I/O Ports Overview** window - that one shows a flat mapping of ports
and their current value, while these panels give a structured view of
a specific chip (counter states, keyboard matrix, PSG latch phase,
interrupt vectors, etc.).

The panels are strictly **observational** - they only read and display,
they never modify the running emulation. You can leave them open all
the time.

| Panel | Shortcut | Available on |
|-------|----------|--------------|
| CTC 8253 State | Alt+Shift+C | MZ-700, MZ-800, MZ-1500 |
| PPI 8255 State | Alt+Shift+I | MZ-700, MZ-800, MZ-1500 |
| Z80 PIO State | Alt+Shift+Z | MZ-800, MZ-1500 (MZ-700 has no such chip) |
| PSG SN76489 State | Alt+Shift+G | MZ-800 (mono), MZ-1500 (stereo) (MZ-700 has no such chip) |

The MZ-700 does not have Z80 PIO or PSG SN76489 chips - the
corresponding menu items, keyboard shortcuts and windows are not
present at all in the MZ-700 build.


## Activation and persistence

Open a panel via any of these:

- Menu **Debugger -> CTC State** (or PPI State / Z80 PIO State / PSG State).
- Keyboard shortcut **Alt+Shift+C / I / Z / G**.
- From DBG Workplace - the **Workplace -> CTC / PPI / Z80 PIO / PSG**
  submenu lets you enable the panel to open automatically together
  with the main debugger window. Off by default.

Window position, size and the expanded/collapsed state of sections
are remembered between emulator runs. State polling only runs while
the window is open - closed windows do not load the emulator.


## Common UI elements

- Tables have **resizable columns** - drag the separator to set the
  width; the layout is saved.
- Sections inside a panel are inside collapsing headers - the
  collapsed state is remembered.
- Values are rendered with tooltips (hover with the mouse for a
  field description).
- An 8-bit hex value is shown alongside its binary representation,
  a 16-bit hex value alongside its decimal representation.
- 0/1 flags are color-coded (on = green, off = grey).
- Enum values (modes, states) are decoded into text (e.g. `Mode 3
  Square Wave`), with the raw number next to it.


## CTC 8253 State

The Intel 8253 chip is a triple counter / timer. Three independent
channels (CTC0, CTC1, CTC2), each 16-bit, six operating modes and
optionally BCD counting. The CTC drives audio (CTC0), generates an
internal clock signal and serves as an interrupt source.

### Last Control Word

A snapshot of the last Control Word that the CPU wrote to the control
port. Decoded fields:

| Field | Bits | Meaning |
|-------|------|---------|
| SC | D7-D6 | Select Counter - 0 = CTC0, 1 = CTC1, 2 = CTC2, 3 = Read-Back |
| RLF | D5-D4 | Read/Load Format - Latch / LSB only / MSB only / LSB+MSB |
| Mode | D3-D1 | Operating mode 0 through 5 |
| BCD | D0 | 0 = binary counting, 1 = BCD (4-decade) |

### Per-channel sections

Each channel (CTC0, CTC1, CTC2) has its own collapsible section.
Main fields:

| Field | Meaning |
|-------|---------|
| `out` | Current counter output level |
| `gate` | Last GATE input level |
| `mode` | Current mode (0 = Interrupt on Terminal Count, 3 = Square Wave, etc.) |
| `bcd` | Binary / BCD counting |
| `rlf` | Read/Load Format - how many bytes and in what order the CPU reads/writes |
| `state` | Internal state machine (LOAD / COUNTDOWN / WAIT_GATE1 ...) |
| `load_done` | Preset was fully written and is now active |
| `rl_byte` | Index of the current byte in the ongoing Read/Load sequence |
| `preset_value` | Preset that the counter is loaded with |
| `value` | Current counter value (live countdown) |
| `mode3_destination_value` | Mode 3 only: target value of the current half-period |
| `mode3_half_value` | Mode 3 only: half period (50 % duty square wave) |

### Tip - tracking mid-frame palette effects

If you are debugging a program that switches the palette or border
colour in the middle of the frame (classic raster trick), watch the
`value` of CTC2 in the CTC State window. As soon as it reaches zero,
an interrupt fires in which the game writes the new colours. If
`value` freezes or never moves, no interrupt is generated - usually
a wrongly written preset or the INT mask disabled in PPI Port C.

### Tip - silent audio

If channel CTC0 runs correctly (`value` ticks down from `preset_value`
to zero in Mode 3) but you hear silence, check PPI State for PC0
(audio mask) - value 0 means muted, 1 means passing through.


## PPI 8255 State

The Intel 8255 chip has three 8-bit ports (A, B, C). On
MZ-800 / MZ-700 / MZ-1500 it handles the central I/O - keyboard
column sampling, CMT data input, tape motor control, beeper mask,
VBLN signal.

### Last Control Word

The last byte written to the PPI control port. The PPI uses two
interpretations depending on bit D7:

- **Mode Set (D7 = 1)** - mode and direction configuration for each
  port. The bits set the directions of ports A, B, the upper and
  lower halves of C and the mode groups.
- **Bit Set/Reset (D7 = 0)** - one-shot set or reset of a specific
  Port C bit.

The panel shows both interpretations - the active one (depending on
D7) in normal colour, the inactive one greyed out.

### Port A - keyboard column

Port A is an output port. The CPU writes here which keyboard matrix
column it wants to sample. Bit decoding:

| Bits | Meaning |
|------|---------|
| D3-D0 | Keyboard column (0-9) |
| D4 | JOY1 enable (active LOW, MZ-800 / MZ-1500 only) |
| D5 | JOY2 enable (active LOW, MZ-800 / MZ-1500 only) |

On the MZ-700 the JOY1/JOY2 rows display a "not present on this arch"
note.

### Port B - keyboard matrix 10x8

When the CPU reads Port B, the PPI returns the bits of the keyboard
row that corresponds to the column currently set on Port A. The panel
shows the entire matrix - 10 columns x 8 bits - as a grid with three
modes:

| Mode | Shows |
|------|-------|
| **Combined** | What the CPU really gets on IN (physical keyboard AND virtual) |
| **Physical** | Physical SDL keyboard only |
| **Virtual** | Virtual keyboard only (VKBD / autotyping) |

The polarity is **active LOW** - a green `*` cell means the key is
pressed (bit 0), a grey `.` cell means released (bit 1). The column
headers 0 to 9 are the PA value, the b0 through b7 rows correspond
to the bits of Port B.

### Port C - per-bit decoding

Port C has some output bits (D0-D3) and some input bits (D4-D7). The
output bits are shown directly from the PPI (idle state). Input bits
D5-D7 are computed by the real hardware only at the moment of read
and the panel shows them greyed with a "computed on read" note - it
does not track them because reading them would have side effects
(CMT sample shift, GDG raster evaluation).

| Bit | Dir | Meaning |
|-----|-----|---------|
| D0 | OUT | CTC0 audio mask (0 = mute, 1 = pass through). Not present on MZ-700. |
| D1 | OUT | CMT data out (bit going to the tape) |
| D2 | OUT | CTC2 INT mask (0 = blocked, 1 = enabled) |
| D3 | OUT | CMT motor control (rising edge toggles the motor) |
| D4 | IN  | CMT motor state (current motor state) |
| D5 | IN  | CMT data in (computed on read) |
| D6 | IN  | Cursor blink generator (computed on read) |
| D7 | IN  | VBLN - vertical blanking flag (computed on read) |

If you need to track D5-D7 live including the side effects, open the
**I/O Ports Overview** window, which reads the port the same way the
CPU does.

### VKBD autotype state

The bottom section (collapsed by default) shows the virtual keyboard
state - remaining text to send, key-down / key-up phase, timing.
Useful for debugging VKBD macro scripts or when a program does not
react to virtual keyboard input.


## Z80 PIO State

The Z80 PIO is Zilog's dual-port programmable I/O chip, fully
integrated into the Z80 daisy-chain interrupt system. It is used
mainly for joystick and parallel interfacing.

Available **only on MZ-800 and MZ-1500**. The MZ-700 does not have
this chip; in the MZ-700 build the panel is not available (the menu
item, keyboard shortcut and window are compiled out).

### Global state - INT / IEO + ICENA event

| Field | Meaning |
|-------|---------|
| `interrupt` | Daisy-chain interrupt register (INT bit, IEO bit) |
| `interrupt_port_id` | Port currently holding INT (Port A / Port B / NONE) |
| `icena_event` | Deferred ICENA event (time when the enable is applied) |
| `icena_event_port_id` | Port the event is scheduled for |

When the INT bit is lit, the PIO is driving the bus `/INT` signal
active. The IEO bit (Interrupt Enable Out) is the output to the next
device in the daisy chain - when it is 0, it blocks interrupts from
lower-priority devices.

### Per-port (Port A, Port B)

Each port has its own collapsible section.

#### Last Control Byte

The Z80 PIO control port is a sequential state machine - the meaning
of a written byte depends on the previous operation. The panel decodes
the byte according to the currently expected type (IVW Interrupt
Vector, MCW Mode Set, ICW Interrupt Control Word, IDW Disable Word,
IOMCW IO mask, INTMCW Interrupt mask).

#### Main port fields

| Field | Meaning |
|-------|---------|
| `mode` | Output (0) / Input (1) / Bidirectional (2) / Control (3) |
| `io_mask` | Mode 3 direction mask (bit 0 = output, 1 = input) - shown greyed out outside Mode 3 |
| `ctrl_expect` | What byte the PIO expects next (Command / IOMCW / INTMCW) |
| `data_output` | Last byte the CPU wrote to the Data port |
| `masked_input` | Masked snapshot returned by IN |
| `interrupt_vector` | IM2 vector (bit 0 is always 0) |
| `icfnc` | Interrupt function - OR (any bit) / AND (all bits) |
| `iclvl` | Interrupt level - LOW (active 0) / HIGH (active 1) |
| `icmask` | Mask of bits watched for the interrupt condition |
| `icena` | Interrupt enable - Enabled / Disabled |
| `port_int` | Port INT pipeline state (None / Pending / Received / Re-Pending) |

### Tip - joystick does not respond

If the joystick does not respond, watch `data_output` or
`masked_input` of the corresponding port in the Z80 PIO State window -
the value must change as you move the stick. If it does not, the
problem is higher up the stack (SDL joystick mapping in iface), not
in the PIO emulation itself.


## PSG SN76489 State

A programmable sound generator - three tone channels plus one noise
channel. The MZ-800 has one chip (mono), the MZ-1500 has two chips
(stereo Left + Right). The MZ-800 can be configured for stereo
depending on the ROM preset.

Available **only on MZ-800 and MZ-1500**. The MZ-700 does not have
this chip; in the MZ-700 build the panel is not available (the menu
item, keyboard shortcut and window are compiled out).

### Latch state

The PSG uses a two-byte protocol. First the LATCH byte (bit 7 = 1)
selects the target channel and the kind of the following data byte
(tone / attenuation), then the DATA byte (bit 7 = 0) writes the value.

| Field | Meaning |
|-------|---------|
| Latched channel | Index of the channel (0-3) the next DATA byte will go to |
| Next DATA targets | TONE / NOISE config (blue) or ATTENUATION (orange) |

### Per-channel state

Four collapsible sections - Channel 0, 1, 2 (always TONE) and
Channel 3 (always NOISE).

#### Common fields

| Field | Meaning |
|-------|---------|
| Channel type | TONE / NOISE |
| Attenuation | 0-15. 0 = max volume (0 dB), step -2 dB, 15 = muted (channel off) |

The dB column in the UI shows for example `0 dB (max)`, `-10 dB`,
`silent (off)`.

#### TONE channel

| Field | Meaning |
|-------|---------|
| Tone divider | 10-bit value 0-1023 |
| Frequency | Computed output frequency in Hz / kHz |
| MIDI note | Nearest MIDI note plus offset in cents (e.g. `A4 +5c`) |

If the divider is less than 2, the output is DC (no oscillation) -
the panel shows "DC (no oscillation)".

#### NOISE channel (CH3 only)

| Field | Meaning |
|-------|---------|
| Noise divider type | 0, 1, 2 = fixed PSG dividers 16 / 32 / 64; 3 = driven by channel 2 tone divider |
| Noise feedback | PERIODIC (simple LFSR) / WHITE (XOR feedback, broader spectrum) |

### PSG oscilloscopes

Below the state fields of every channel there is a miniature
oscilloscope that draws the current output waveform of that channel.

Limitations worth knowing:

- The scope always draws **approximately 4 periods** of the signal
  regardless of frequency. Its purpose is to let you see that the
  channel is actually oscillating and in what shape, not for precise
  timing measurement.
- The noise channel shows a **random sample of the noise pattern** -
  it looks different on each redraw. That is the correct behaviour.
- When muted (Attenuation = 15) the output is a flat line at zero.

### Stereo (MZ-1500, optionally MZ-800)

In stereo mode the panel shows **PSG Left (chip 0)** and **PSG Right
(chip 1)** as two separate sections. Each chip has its own set of
4 channels. In mono mode only one chip is shown.

### Tip - silent channel

If a channel sounds wrong:

1. Check Attenuation - 15 = fully muted.
2. Check Tone divider - values 0 or 1 mean DC, no audible tone.
3. On Channel 3 check the type - if NOISE is selected, you hear
   noise, not a tone.
4. If the PSG looks fine, check PPI State for PC0 (CTC0 audio mask) -
   it must be 1, otherwise the entire audio path is muted.


## Relation to I/O Ports Overview

I/O Ports Overview and the per-chip panels complement each other:

| Window | Shows |
|--------|-------|
| **I/O Ports Overview** | Flat list of all ports and their last value |
| **Per-chip State panel** | Internal structure of a specific chip - sequencers, state machines, partial registers |

A typical workflow: keep I/O Ports Overview open all the time as a
general overview, and open per-chip panels as needed depending on
what you are debugging (CTC State for a raster glitch, PSG State for
an audio problem, PPI State for a keyboard or CMT problem).
