# Watch expressions

A watch row is a persistent observation in the debugger Watch panel.
Each row has a `mode`, a target (address or expression), and a `type`
that controls how bytes are decoded for display.

Tool: `emu_watch_add` (create), `emu_watch_remove`, `emu_watch_list`,
`emu_watch_eval` (read current value or evaluate an ad-hoc expression).

## Three modes

| Mode | Target | Read behavior |
|------|--------|---------------|
| `address` | literal Z80 address | Read N bytes starting at `addr`, decode per `type`. |
| `expr_scalar` | `expr` -> int32 | `expr` evaluated; `type` controls display format only (no extra memory read). |
| `expr_deref` | `expr` -> uint16 addr | `expr` evaluated, low 16 bits used as address; then N bytes read and decoded per `type`. |

In `expr_*` modes the expression uses the same language as BP
conditions - see `emulator://docs/bp_dsl`. The expression is
re-evaluated on every Watch refresh.

## Type tags

Each tag determines value width and display formatting.

| Tag | Width | Display |
|-----|-------|---------|
| `u8` (default) | 1 byte | unsigned decimal + hex |
| `i8` | 1 byte | signed decimal |
| `u16le` / `i16le` | 2 bytes LE | unsigned / signed |
| `u16be` / `i16be` | 2 bytes BE | unsigned / signed |
| `u32le` / `i32le` | 4 bytes LE | unsigned / signed |
| `u32be` / `i32be` | 4 bytes BE | unsigned / signed |
| `bit` | 1 byte | bit-extracted (= per-bit display) |
| `ascii` | N bytes | standard ASCII string |
| `mzascii` | N bytes | Sharp MZ ASCII (see `emulator://docs/sharp_display_code`) |
| `bytes` | N bytes | raw hex dump |

For `ascii` / `mzascii` / `bytes` the length N is configurable per row
(=  length parameter in the underlying watch_add dispatch; the MCP tool
uses a sensible default and may be extended later).

## Examples

### Plain address watch

```json
{"mode": "address", "addr": 0xE000, "type": "u8", "name": "PIO_PA"}
```

Reads one byte at `0xE000` and displays it as `u8`.

### 16-bit LE word watch

```json
{"mode": "address", "addr": 0x1100, "type": "u16le", "name": "ip_word"}
```

### Scalar expression - count of bits set in A

```json
{"mode": "expr_scalar",
 "expr": "bit(A,0)+bit(A,1)+bit(A,2)+bit(A,3)+bit(A,4)+bit(A,5)+bit(A,6)+bit(A,7)",
 "type": "u8",
 "name": "popcount_A"}
```

### Indirect read - byte at HL

```json
{"mode": "expr_deref", "expr": "HL", "type": "u8", "name": "[HL]"}
```

### Indirect read - 4-byte LE long at SP+2

```json
{"mode": "expr_deref", "expr": "SP+2", "type": "u32le", "name": "ret_word"}
```

### Sharp MZ ASCII string at the top-left of text VRAM

```json
{"mode": "address", "addr": 0xD000, "type": "mzascii", "name": "screen_row0"}
```

Equivalent to reading the first 40 bytes of MZ-700 text VRAM and
interpreting them as Sharp MZ ASCII characters - useful when the
emulator is in a 700-compat layout. See
`emulator://docs/sharp_display_code` for the encoding tables.

## Ad-hoc evaluation (no row)

`emu_watch_eval` accepts a raw `expr` argument and returns the result
without creating a row. Useful for one-shot checks:

```json
{"expr": "(SP < 0xFFE0) && (Reason == int_ack)"}
```

## Live state

`emulator://watch` returns the full watch list with current values
formatted per row type. Per-row snapshot is in
`emulator://watch/snapshot/{name}`.

## Related

- `emulator://docs/bp_dsl` - expression language used by `expr_*` modes.
- `emulator://docs/sharp_display_code` - decoding `mzascii` type.
- `emulator://watch` - live watch list resource.
