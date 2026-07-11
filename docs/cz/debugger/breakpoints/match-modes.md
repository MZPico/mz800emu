# Breakpoint - Match modes

Match mode rozhoduje, jak BP vyhodnocuje shodu adresy / portu / banku /
SP proti aktuálnímu stavu emulátoru. Bez explicitního nastavení je
výchozí mód `SINGLE` (= shoda na přesnou hodnotu).

Tento dokument popisuje sémantiku všech tří modů a jejich interakci
s ostatními aspekty BP (Zone awareness, Condition expression).

## Přehled

Pro adresové fieldy (PC_EXEC, MEM_R, MEM_W, IORQ_R, IORQ_W, MMEXT_BANK):

| Mode | Sémantika | Typický use case |
|------|-----------|------------------|
| `SINGLE` | `x == ref` | běžný BP na konkrétní adrese |
| `RANGE`  | `ref <= x <= end` | watch celého kódového bloku / paměťového bufferu / portové skupiny |
| `MASK`   | `(x & mask) == (ref & mask)` | dispatch table debugging, register group probe |

Pro SP_THRESHOLD je oddělený režim:

| Mode | Sémantika | Typický use case |
|------|-----------|------------------|
| `SINGLE` | trigger při sestupném crossingu jednoho prahu | stack overflow detect |
| `WINDOW` | trigger pokud SP opustí `[lower..upper]` | stack corruption / cross-task switch detect |

## SINGLE mode

Default. Match jen na přesnou shodu hodnoty.

```
Match Mode: Single
Address:    0x1234
```

Trigger pokud `value == 0x1234`.

## RANGE mode

Match pokud hodnota leží v inkluzivním rozsahu `[start..end]`.

```
Match Mode: Range
Address:    0x1000
End Addr:   0x10FF
```

Trigger pro adresy `0x1000`, `0x1001`, ..., `0x10FF` (256 adres).

### Příklady

1. **Memory buffer watch** - sledovat zápisy do bufferu:
   ```
   Type:       MEM_W
   Address:    0xE000
   End Addr:   0xE0FF
   Match Mode: Range
   ```

2. **I/O port group** - sledovat zápisy do skupiny PSG portů:
   ```
   Type:       IORQ_W
   Port:       0xF0
   End Port:   0xFF
   Match Mode: Range
   ```

3. **PEHU overlay range** - debugovat skupinu overlay banks:
   ```
   Type:       PC_EXEC
   Zone:       MMEXT_BANK
   Bank ID:    08
   End Bank:   0F
   Match Mode: Range  (bank Match Mode)
   ```

### Validace

End musí být `>= start`, jinak UI deaktivuje OK button + zobrazí
warning. V runtime je defenzivní swap pro SP WINDOW (= bezpečnost při
ručně editovaném `.bpt` JSON), ale pro adresové RANGE je `addr_end <
addr` interpretováno jako `addr_end == addr` (= efektivně SINGLE).

## MASK mode

Match pokud `(value & mask) == (ref & mask)`. Užitečné pro skupiny
adres / portů sdílejících bitový vzor.

```
Match Mode: Mask
Address:    0x0042
Mask:       0x00FF
```

Trigger pro adresy s low byte `0x42`: `0x0042`, `0x0142`, `0x0242`,
... `0xFF42`.

### Příklady

1. **Low byte watch** - kdykoliv PC trefí adresu končící `0x42`:
   ```
   Type:       PC_EXEC
   Address:    0x0042
   Mask:       0x00FF
   Match Mode: Mask
   ```

2. **I/O port group** - sledovat porty `0xCC..0xCF` (např. WD279x FDC
   registr group):
   ```
   Type:       IORQ_W
   Port:       0xCC
   Mask:       0xFC
   Match Mode: Mask
   ```

3. **PEHU bank group** - sledovat banky 8..11 (sdílí horní 5 bitů):
   ```
   Type:       PC_EXEC (nebo MEM_R/W)
   Zone:       MMEXT_BANK
   Bank ID:    08
   Mask:       18
   Match Mode: Mask  (bank Match Mode)
   ```

### Edge cases

- `Mask = 0` matchuje cokoliv (= ekvivalent missing condition). UI
  zobrazí warning, ale akceptuje. Pokud chcete "vždy fire", použijte
  raději GLOBAL typ s `expr = 1`.
- `Mask = 0xFFFF` (resp. `0xFF` pro bank) je ekvivalent SINGLE.

## SP WINDOW mode

Pro `SP_THRESHOLD` BP. Trigger fires pokud SP opustí povolený rozsah
`[lower..upper]`. Edge-triggered - aby se nezabralo spam pro
pre-existující outside SP na startu emu (= musí nejdřív být inside,
pak přejít out).

```
Type:        SP_THRESHOLD
Lower bound: 0xFE00
Upper bound: 0xFFFE
Mode:        Window
```

Trigger fires pokud SP přejde z hodnoty uvnitř `[0xFE00..0xFFFE]` ven
(např. SP klesl na `0xFD00` nebo stoupl na `0xFFFF`).

### Sémantika edge

Při hooked instrukci porovnáme `(was_inside, is_outside)`:
- `was_inside && is_outside` = trigger
- `was_inside && is_inside` = no-op (level)
- `!was_inside && is_outside` = sustained outside, bez triggeru
- `!was_inside && is_inside` = SP se vrátil do okna, bez triggeru

Tj. trigger vyžaduje **přechod**.

## Interakce s Zone awareness

Match mode na **adrese** se aplikuje **navíc** k Zone filtru. Příklad:

```
Type:       PC_EXEC
Zone:       MMEXT_BANK
Bank ID:    9
Address:    0x2000
End Addr:   0x3FFF
Match Mode: Range  (addr Match Mode)
```

BP fire jen pokud:
1. Memext PEHU je connected
2. Aktuálně mapovaný overlay bank == 9
3. PC je v `[0x2000..0x3FFF]`

Bank Match Mode (oddělený od addr Match Mode) může pokrýt skupinu
banks (RANGE/MASK na bank).

## Address as (MMEXT_BANK MEM_R / MEM_W)

Pro MEM_R / MEM_W breakpoint v zóně `MMEXT_BANK` je navíc per-BP volba
`Address as`, která určuje, jak se interpretuje pole `Address`:

| Address as | Pole Address | Kdy BP fire |
|------------|--------------|-------------|
| `CPU view` (default) | Z80 adresa `0x0000-0xFFFF` | při čtení/zápisu na tuto CPU adresu, když je banka právě namapovaná do CPU okna (= dosavadní chování) |
| `Bank offset` | offset `0x0000-0x1FFF` v rámci PEHU banky | při čtení/zápisu do dvojice `(bank_id, offset)` bez ohledu na to, do kterého CPU okna je banka právě namapovaná |

- Volba je dostupná **jen pro zónu `MMEXT_BANK` a jen pro PEHU memext**
  (8 KB banka, offset `0x0000-0x1FFF`).
- V dialogu Edit BP se při `Zone = MMEXT_BANK` zobrazí dropdown
  `Address as: CPU view | Bank offset`. Při volbě `Bank offset` se řádek
  `Address:` přejmenuje na `Offset:`.
- **Match Mode (SINGLE / RANGE / MASK) funguje v obou režimech.** V
  režimu `Bank offset` se Match Mode aplikuje na offset
  (`0x0000-0x1FFF`) místo na CPU adresu.

Rozdíl: `Bank offset` sleduje fyzickou banku napříč remapováním -
zachytí zápis do `(bank_id, offset)`, ať je banka právě namapovaná do
libovolného CPU okna (a i když se okno mezi zápisy mění). `CPU view` je
naproti tomu banking-aware pohled na jednu konkrétní CPU adresu. Zápis
se k bance dostane jen když je někam namapovaná - nemapovanou banku CPU
zápisem nezasáhne ani v jednom režimu.

## Interakce s Condition expression

Match mode se vyhodnocuje **PŘED** condition expression. Pokud match
mode neprojde, condition se nikdy nevyhodnotí (= optimalizace, hot
path skip).

Sekvence per BP hit:
1. Effective enabled check
2. Zone awareness filter
3. **Match mode check**
4. Skip count
5. Condition expression
6. Hits counter
7. Hit count
8. Action execute

## Persistence

Match mody jsou serializované jako řetězce v `.bpt` JSON souboru:

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

Soubory `.bpt` bez match-mode klíčů jsou loadnuty s defaultem `SINGLE`.

## PC_EXEC MASK

PC_EXEC s MASK match modem je plně podporován. SINGLE a RANGE PC_EXEC
využívají per-PC bytemap (O(1) per-instruction lookup), zatímco MASK
PC_EXEC se vyhodnocuje iterací přes seznam non-SINGLE BP (= sparse
mask nelze enumerate v bytemap). Hot loop má guard testující, zda je
nějaký non-SINGLE PC_EXEC BP registrován - pokud ne, je overhead nulový.

## IORQ Port Mode

Z80 má dva I/O addressing patterny. IORQ_R / IORQ_W proto má per-BP
volbu `port_mode`:

| Port Mode | Z80 instrukce | Adresová sběrnice | Match šířka |
|-----------|---------------|-------------------|-------------|
| `8BIT` (default) | `IN A,(n)` / `OUT (n),A` | n na A0..A7, A8..A15 = "don't care" | low byte |
| `16BIT` | `IN r,(C)` / `OUT (C),r` | BC na A0..A15 (B = high) | full 16-bit |

**Default `8BIT`** = každý IORQ s odpovídajícím low bytem portu fire
(= většina Z80 software).

**`16BIT`** rozliší např. `0x42CE` od `0x88CE` - užitečné pro hardware
s extended port addressing přes B registr (např. unicard / interní
periferie s 16-bit decoderem).

Match Mode (`SINGLE` / `RANGE` / `MASK`) se aplikuje **uvnitř zvoleného
port_mode módu**:

- `8BIT` + RANGE / MASK = porovnává jen low byte.
- `16BIT` + RANGE / MASK = porovnává plný 16-bit BC.

**Persistence:** `port_mode` jako string `"8BIT"` / `"16BIT"`. Soubory
bez tohoto klíče loadnou s defaultem `8BIT`. Plná hodnota `port` je
v souboru zachována nezávisle na módu (= switch 16BIT -> 8BIT neztratí
upper byte v perzistenci, runtime jen low byte matchne).

**JSON příklad:**

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

Per-instruction overhead pro non-SINGLE mode je marginální:
- SINGLE: 1 compare
- RANGE: 2 compares
- MASK: 2 AND + 1 compare

## Související dokumenty

- `expression-syntax.md` - condition expression
- `memory-map.md` - banking + zone awareness
