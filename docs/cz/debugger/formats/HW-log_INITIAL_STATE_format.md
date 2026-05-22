# HW Log - initial state format

Soubor `<dir>/<name>_initial_state.bin` zachycuje stav HW chipů v okamžiku
startu hwlog recordingu. Externí parser podle této baseline + sumace všech
hwlog eventů (24 B `<dir>/<name>.NNN.bin` chunků) rekonstruuje stav v
libovolném okamžiku.

## TLV obálka

```
[0]    chip_id        (1 B)
[1..4] payload_length (4 B uint32 LE)
[5...] payload        (per-chip field-by-field LE encoded - viz níže)
```

EOF marker: `chip_id = 0xFF, length = 0`.

Pořadí TLV v souboru: PIO8255, CTC8253, PSG, [MEMEXT pokud connected],
GDG_MODE, PIOZ80, EOF.

**Endianness:** všechny vícebajtové hodnoty jsou little-endian. Parsování
je nezávislé na compiler ABI / alignment / padding.

## Per-chip payload layouty

### HWLOG_CHIP_PIO8255 (chip_id = 0x10)

Velikost payloadu: **50 B** (pevná).

| Offset | Velikost | Pole                              |
|--------|----------|-----------------------------------|
| 0      | 4 B u32  | signal_PA                         |
| 4      | 4 B u32  | signal_PA_keybord_column          |
| 8      | 4 B u32  | signal_PA_joy1_enabled            |
| 12     | 4 B u32  | signal_PA_joy2_enabled            |
| 16     | 4 B u32  | signal_PC                         |
| 20     | 4 B u32  | signal_pc00 (blokovani audio)     |
| 24     | 4 B u32  | signal_pc01 (data CMT out)        |
| 28     | 4 B u32  | signal_pc02 (CTC2 INT mask)       |
| 32     | 4 B u32  | signal_pc03 (motor CMT)           |
| 36     | 4 B u32  | signal_pc04 (motor stav)          |
| 40     | 10 B     | keyboard_matrix[10]               |

Vynecháno: vkbd_autotype state (UI driver, neuložené).

### HWLOG_CHIP_CTC8253 (chip_id = 0x11)

Velikost payloadu: **102 B** (= 3× 34 B counter).

Per counter (i = 0..2), offset = i × 34:

| Offset | Velikost | Pole                              |
|--------|----------|-----------------------------------|
| +0     | 1 B      | mode (en_CTC_MODE)                |
| +1     | 1 B      | bcd                               |
| +2     | 1 B      | rlf (en_CTC_RLF)                  |
| +3     | 1 B      | state (en_CTC_STATE)              |
| +4     | 1 B      | out                               |
| +5     | 1 B      | gate                              |
| +6     | 1 B      | load_done                         |
| +7     | 1 B      | rl_byte (LSB/MSB pořadí counter)  |
| +8     | 1 B      | latch_op                          |
| +9     | 1 B      | padding                           |
| +10    | 4 B u32  | read_latch                        |
| +14    | 4 B u32  | preset_value                      |
| +18    | 4 B u32  | preset_latch                      |
| +22    | 4 B u32  | value (current count)             |
| +26    | 4 B u32  | mode3_destination_value           |
| +30    | 4 B u32  | mode3_half_value                  |

Vynecháno: output_cb (function pointer), clk1m1_event (scheduling).

### HWLOG_CHIP_PSG (chip_id = 0x20)

Velikost payloadu: **2 + 2× (8 + 4× 23) = 2 + 2× 100 = 202 B**.

Globální:

| Offset | Velikost | Pole                              |
|--------|----------|-----------------------------------|
| 0      | 1 B      | stereo (0 = mono, 1 = stereo)     |
| 1      | 1 B      | psg_count (= PSG_MAX_COUNT = 2)   |

Per PSG (p = 0..1), offset = 2 + p × 100:

| Offset | Velikost | Pole                              |
|--------|----------|-----------------------------------|
| +0     | 4 B u32  | latch_cs                          |
| +4     | 4 B u32  | latch_attn                        |
| +8     | 92 B     | channel[0..3] (4× 23 B)           |

Per channel (ch = 0..3), offset uvnitř PSG = 8 + ch × 23:

| Offset | Velikost | Pole                              |
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

V mono módu jsou hodnoty psg[1] platný kopie psg[0] interní logiky;
parser by se měl orientovat podle stereo flagu zda psg[1] zahrnout
v rendering pipeline.

### HWLOG_CHIP_MEMEXT (chip_id = 0x40)

Přítomný **jen pokud je Memext připojen** v okamžiku startu hwlog
recordingu. Pokud Memext není připojen, TLV chybí.

Velikost payloadu: **68 B**.

| Offset | Velikost | Pole                              |
|--------|----------|-----------------------------------|
| 0      | 1 B      | connection (en_MEMEXT_CONNECTION) |
| 1      | 1 B      | type (en_MEMEXT_TYPE: 0=LUFTNER,1=PEHU) |
| 2      | 2 B u16  | addr_mask                         |
| 4      | 64 B     | map[16] (16× u32 = 64 B)          |

Vynecháno: RAM (512 KB), FLASH, WOM (= patří do cputrack initial dump).

### HWLOG_CHIP_GDG_MODE (chip_id = 0x01)

GDG_MODE jako reprezentant GDG clusteru. Velikost variabilní podle
architektury - rozliš podle `arch_marker`.

Společné pole (oba MZ-800 i MZ-1500):

| Offset | Velikost | Pole                              |
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

MZ-800 specifická pole (offset 24, jen pokud arch_marker = 0x80):

| Offset | Velikost | Pole                              |
|--------|----------|-----------------------------------|
| 24     | 1 B      | regPALGRP                         |
| 25     | 1 B      | regPAL0                           |
| 26     | 1 B      | regPAL1                           |
| 27     | 1 B      | regPAL2                           |
| 28     | 1 B      | regPAL3                           |

Celková velikost MZ-800: **29 B**.

MZ-1500 specifická pole (offset 24, jen pokud arch_marker = 0x15):

| Offset | Velikost | Pole                              |
|--------|----------|-----------------------------------|
| 24     | 32 B     | mode1500_color[8] (8× u32)        |

Celková velikost MZ-1500: **56 B**.

Vynecháno: emuevent state, screen_need_update_from, last_updated_border_pixel,
tempo_divider (architektura konstanta), ctc0clk (compile-time conditional).

### HWLOG_CHIP_PIOZ80 (chip_id = 0x12)

Per port + globální. Velikost payloadu: **2× 13 + 2 = 28 B**.

Per port (p = 0..1), offset = p × 13:

| Offset | Velikost | Pole                              |
|--------|----------|-----------------------------------|
| +0     | 1 B      | mode (en_PIOZ80_PORT_MODE)        |
| +1     | 1 B      | io_mask (1 = vstup, 0 = výstup)   |
| +2     | 1 B      | iclvl (0 = LOW, 1 = HIGH)         |
| +3     | 1 B      | icmask (0 = monitorován, 1 = maskován) |
| +4     | 1 B      | icena (0 = DISABLED, 1 = ENABLED) |
| +5     | 1 B      | icfnc (0 = OR, 1 = AND)           |
| +6     | 1 B      | interrupt_vector                  |
| +7     | 1 B      | port_int (en_PIOZ80_PORT_INT)     |
| +8     | 1 B      | masked_input                      |
| +9     | 1 B      | data_output                       |
| +10    | 1 B      | ctrl_expect (state machine)       |
| +11    | 1 B      | last_intfnc_result                |
| +12    | 1 B      | bus_input (raw piny - viz pozn. 1) |

Globální (offset 26):

| Offset | Velikost | Pole                              |
|--------|----------|-----------------------------------|
| 26     | 1 B      | g_pioz80.interrupt                |
| 27     | 1 B i8   | g_pioz80.interrupt_port_id (-1 = NONE) |

**Pozn. 1 - bus_input:** Pole zachycuje aktuální snímek raw vstupních pinů:
- PA: bity 0,1 = 1 (LPT pull-up), bit 4 = !CTC0_OUT0, bit 5 = VBLN
- PB: 0xFF (drženo na zemi přes invertor)

**Pozn. 2 - input_latch:** Mode 1 handshake latch se v MZ-800 nepoužívá -
pole se neuvádí.

## Parser implementace

Pseudocode pro čtení initial state:

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

Per chip parser dispatchuje podle chip_id na konkrétní field-by-field
deserializer. Layout je závazně určený tabulkami výše.
