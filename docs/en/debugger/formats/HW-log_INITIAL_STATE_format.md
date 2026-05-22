# HW Log - initial state format

The file `<dir>/<name>_initial_state.bin` captures the state of HW chips
at the moment hwlog recording starts. An external parser, using this
baseline + summation of all hwlog events (24 B `<dir>/<name>.NNN.bin`
chunks), reconstructs the state at any given moment.

## TLV envelope

```
[0]    chip_id        (1 B)
[1..4] payload_length (4 B uint32 LE)
[5...] payload        (per-chip field-by-field LE encoded - see below)
```

EOF marker: `chip_id = 0xFF, length = 0`.

Order of TLVs in the file: PIO8255, CTC8253, PSG, [MEMEXT if connected],
GDG_MODE, PIOZ80, EOF.

**Endianness:** all multi-byte values are little-endian. Parsing is
independent of compiler ABI / alignment / padding.

## Per-chip payload layouts

### HWLOG_CHIP_PIO8255 (chip_id = 0x10)

Payload size: **50 B** (fixed).

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| 0      | 4 B u32  | signal_PA                         |
| 4      | 4 B u32  | signal_PA_keybord_column          |
| 8      | 4 B u32  | signal_PA_joy1_enabled            |
| 12     | 4 B u32  | signal_PA_joy2_enabled            |
| 16     | 4 B u32  | signal_PC                         |
| 20     | 4 B u32  | signal_pc00 (audio blocking)      |
| 24     | 4 B u32  | signal_pc01 (CMT data out)        |
| 28     | 4 B u32  | signal_pc02 (CTC2 INT mask)       |
| 32     | 4 B u32  | signal_pc03 (CMT motor)           |
| 36     | 4 B u32  | signal_pc04 (motor state)         |
| 40     | 10 B     | keyboard_matrix[10]               |

Omitted: vkbd_autotype state (UI driver, not stored).

### HWLOG_CHIP_CTC8253 (chip_id = 0x11)

Payload size: **102 B** (= 3x 34 B counter).

Per counter (i = 0..2), offset = i * 34:

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| +0     | 1 B      | mode (en_CTC_MODE)                |
| +1     | 1 B      | bcd                               |
| +2     | 1 B      | rlf (en_CTC_RLF)                  |
| +3     | 1 B      | state (en_CTC_STATE)              |
| +4     | 1 B      | out                               |
| +5     | 1 B      | gate                              |
| +6     | 1 B      | load_done                         |
| +7     | 1 B      | rl_byte (LSB/MSB counter order)   |
| +8     | 1 B      | latch_op                          |
| +9     | 1 B      | padding                           |
| +10    | 4 B u32  | read_latch                        |
| +14    | 4 B u32  | preset_value                      |
| +18    | 4 B u32  | preset_latch                      |
| +22    | 4 B u32  | value (current count)             |
| +26    | 4 B u32  | mode3_destination_value           |
| +30    | 4 B u32  | mode3_half_value                  |

Omitted: output_cb (function pointer), clk1m1_event (scheduling).

### HWLOG_CHIP_PSG (chip_id = 0x20)

Payload size: **2 + 2x (8 + 4x 23) = 2 + 2x 100 = 202 B**.

Global:

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| 0      | 1 B      | stereo (0 = mono, 1 = stereo)     |
| 1      | 1 B      | psg_count (= PSG_MAX_COUNT = 2)   |

Per PSG (p = 0..1), offset = 2 + p * 100:

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| +0     | 4 B u32  | latch_cs                          |
| +4     | 4 B u32  | latch_attn                        |
| +8     | 92 B     | channel[0..3] (4x 23 B)           |

Per channel (ch = 0..3), offset inside PSG = 8 + ch * 23:

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| +0     | 1 B      | type (en_PSG_CHTYPE: 0=TONE,1=NOISE) |
| +1     | 1 B      | attn (en_ATTENUATOR 0..15)        |
| +2     | 4 B u32  | timer                             |
| +6     | 4 B u32  | tone.divider                      |
| +10    | 4 B u32  | tone.latch_divider                |
| +14    | 1 B      | noise.div_type                    |
| +15    | 1 B      | noise.type                        |
| +16    | 1 B      | noise.last_noise_type             |
| +17    | 2 B u16  | noise.shiftregister               |
| +19    | 4 B u32  | output_signal                     |

In mono mode, psg[1] values are a valid copy of psg[0] internal logic;
the parser should consult the stereo flag to decide whether to include
psg[1] in the rendering pipeline.

### HWLOG_CHIP_MEMEXT (chip_id = 0x40)

Present **only if Memext is connected** at the moment hwlog recording
starts. If Memext is not connected, the TLV is absent.

Payload size: **68 B**.

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| 0      | 1 B      | connection (en_MEMEXT_CONNECTION) |
| 1      | 1 B      | type (en_MEMEXT_TYPE: 0=LUFTNER,1=PEHU) |
| 2      | 2 B u16  | addr_mask                         |
| 4      | 64 B     | map[16] (16x u32 = 64 B)          |

Omitted: RAM (512 KB), FLASH, WOM (= belong in cputrack initial dump).

### HWLOG_CHIP_GDG_MODE (chip_id = 0x01)

GDG_MODE as the representative of the GDG cluster. Variable size
depending on architecture - distinguish by `arch_marker`.

Common fields (both MZ-800 and MZ-1500):

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| 0      | 4 B u32  | total_elapsed.screens             |
| 4      | 4 B u32  | total_elapsed.ticks               |
| 8      | 4 B u32  | beam_row                          |
| 12     | 1 B      | sts_vsync                         |
| 13     | 1 B      | sts_hsync                         |
| 14     | 1 B      | hbln                              |
| 15     | 1 B      | vbln                              |
| 16     | 1 B      | regDMD                            |
| 17     | 1 B      | regBOR                            |
| 18     | 1 B      | regct53g7                         |
| 19     | 4 B u32  | tempo                             |
| 23     | 1 B      | arch_marker (0x80=MZ-800, 0x15=MZ-1500) |

MZ-800 specific fields (offset 24, only if arch_marker = 0x80):

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| 24     | 1 B      | regPALGRP                         |
| 25     | 1 B      | regPAL0                           |
| 26     | 1 B      | regPAL1                           |
| 27     | 1 B      | regPAL2                           |
| 28     | 1 B      | regPAL3                           |

Total MZ-800 size: **29 B**.

MZ-1500 specific fields (offset 24, only if arch_marker = 0x15):

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| 24     | 32 B     | mode1500_color[8] (8x u32)        |

Total MZ-1500 size: **56 B**.

Omitted: emuevent state, screen_need_update_from, last_updated_border_pixel,
tempo_divider (architecture constant), ctc0clk (compile-time conditional).

### HWLOG_CHIP_PIOZ80 (chip_id = 0x12)

Per port + global. Payload size: **2x 13 + 2 = 28 B**.

Per port (p = 0..1), offset = p * 13:

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| +0     | 1 B      | mode (en_PIOZ80_PORT_MODE)        |
| +1     | 1 B      | io_mask (1 = input, 0 = output)   |
| +2     | 1 B      | iclvl (0 = LOW, 1 = HIGH)         |
| +3     | 1 B      | icmask (0 = monitored, 1 = masked) |
| +4     | 1 B      | icena (0 = DISABLED, 1 = ENABLED) |
| +5     | 1 B      | icfnc (0 = OR, 1 = AND)           |
| +6     | 1 B      | interrupt_vector                  |
| +7     | 1 B      | port_int (en_PIOZ80_PORT_INT)     |
| +8     | 1 B      | masked_input                      |
| +9     | 1 B      | data_output                       |
| +10    | 1 B      | ctrl_expect (state machine)       |
| +11    | 1 B      | last_intfnc_result                |
| +12    | 1 B      | bus_input (raw pins - see note 1) |

Global (offset 26):

| Offset | Size     | Field                             |
|--------|----------|-----------------------------------|
| 26     | 1 B      | g_pioz80.interrupt                |
| 27     | 1 B i8   | g_pioz80.interrupt_port_id (-1 = NONE) |

**Note 1 - bus_input:** This field captures the current snapshot of raw
input pins:
- PA: bits 0,1 = 1 (LPT pull-up), bit 4 = !CTC0_OUT0, bit 5 = VBLN
- PB: 0xFF (held to ground through inverter)

**Note 2 - input_latch:** Mode 1 handshake latch is not used in MZ-800 -
the field is not included.

## Parser implementation

Pseudocode for reading initial state:

```python
def parse_initial_state ( data ):
    chips = {}
    off = 0
    while off + 5 <= len ( data ):
        chip_id = data [ off ]
        length = int.from_bytes ( data [ off + 1 : off + 5 ], 'little' )
        if chip_id == 0xFF and length == 0:
            break  # EOF
        payload = data [ off + 5 : off + 5 + length ]
        chips [ chip_id ] = parse_chip ( chip_id, payload )
        off += 5 + length
    return chips
```

The per-chip parser dispatches by chip_id to a specific field-by-field
deserializer. Layout is authoritatively defined by the tables above.
