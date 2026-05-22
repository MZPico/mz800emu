# CPU Tracking Log - formát exportních souborů

## Co je cputrack

**cputrack** je první ze čtyř subsystémů trace-suite (společně s
`iorqlog`, `intlog`, `hwlog`). Loguje **jeden záznam za každou
dokončenou Z80 instrukci** během běhu emulátoru. Slouží pro reverse
engineering, analýzu hot paths, identifikaci self-modifying kódu
a externí re-simulaci stavu paměti.

cputrack se ovládá z menu debug okna v položce
`Debugger Settings -> Trace Suite -> CPU Track`. Má tři režimy:

- `Off` (výchozí) - žádné zaznamenávání, žádná režie.
- `Only With Debug Window` - aktivní jen pokud je otevřené debug okno.
- `Always` - aktivní trvale.

Volby `Save on Exit`, `Set directory...`, `Chunk MB` a `Max Total MB`
v témže menu řídí finalizační chování, cílový adresář a velikostní
limity. Všechny volby jsou persistentní v emulátorové ini konfiguraci
(sekce `[TRACE_CPUTRACK]`, klíče `mode`, `dir`, `name`, `chunk_mb`,
`max_total_mb`, `save_on_exit`).

Recording běží ve společné pomalé cestě s CDL/cpuhist (callback swap),
hot path emulátoru bez aktivních diagnostik zůstává beze změny vůči
vanilní verzi.

## Adresářová struktura exportu

```
<dir>/
    <name>.json                    # meta + chunks anchor + initial regs
    <name>_initial_ram.bin         # 64 KB CPU RAM dump
    <name>_initial_vram.bin        # 32 KB VRAM (jen MZ-800)
    <name>_initial_exvram.bin      # 32 KB EXVRAM (jen MZ-800)
    <name>_initial_cgram.bin       # 4 KB CG-RAM (jen MZ-1500)
    <name>_initial_pcg.bin         # 24 KB PCG (jen MZ-1500)
    <name>_initial_memext.bin      # 512 KB Memext (pokud připojeno)
    <name>_initial_rdN.bin         # per-Ramdisk (pokud aktivní)
    <name>.000.bin                 # chunk 0 - per-instr eventy
    <name>.001.bin                 # chunk 1
    ...
```

`<name>.json` je textový metadata soubor (viz níže). `<name>_initial_*.bin`
jsou snapshoty fyzických pamětí v okamžiku startu recordingu (= header).
`<name>.NNN.bin` jsou flat sekvence per-instrukčních eventů.

## Per-event záznam (12 B fixed)

| Offset | Velikost | Pole                                          |
|--------|----------|------------------------------------------------|
| 0      | 2 B      | regPC (uint16 LE)                              |
| 2      | 1 B      | insn_length (1-4)                              |
| 3      | 4 B      | insn_bytes (jen prvních insn_length valid)     |
| 7      | 4 B      | wait_clk (uint32 LE)                           |
| 11     | 1 B      | reserved (zarovnání na 12 B)                   |

Endianita zápisu je host byte order; na všech podporovaných platformách
emulátoru (Windows x86_64, Linux x86_64) je to little-endian.

### Pole regPC

Adresa instrukce v okamžiku jejího provedení (= PC před vykonáním
instrukce, ne PC po něm). Pro `JP 1234h` je `regPC` adresou opcode `JP`,
ne cílem skoku.

### Pole insn_length

Počet platných bajtů v `insn_bytes`. Z80 instrukce mají délku 1-4 B
(prefix `CB`, `DD`, `ED`, `FD` + opcode + případné offset/imm bajty).

### Pole insn_bytes

Až 4 bajty opcode + operandů. Bajty mimo `insn_length` jsou nedefinované
(pravděpodobně 0, ale parser nesmí spoléhat).

Z `regPC` + `insn_bytes[0..insn_length-1]` lze plně rekonstruovat:
- co Z80 vykonal (= disassemble)
- standardní délku v T-states (Z80 ISA tabulka)
- které registry instrukce čte / zapisuje
- které memory operations dělá (z register stavu, který si externí
  re-simulátor sám trackuje)

### Pole wait_clk

Počet **WAIT T-states navíc** nad rámec standardní délky instrukce.

| Hodnota | Význam |
|---------|--------|
| `0` | Žádný WAIT (běžný případ pro většinu instrukcí v běžném stavu emulátoru). |
| `1..0xFFFFFFFE` | WAIT trval N CPU T-states. |
| `0xFFFFFFFF` | **Saturated sentinel** - WAIT byl delší než ~20 minut při 3.5 MHz, hodnota přetekla uint32. Externí parser ví, že "dlouho nic, nelze přesně určit kolik". Reálný Sharp by za tu dobu beztoho ztratil DRAM (refresh), emu může pauzu udržet do nekonečna. |

#### Zdroj WAIT T-states

Hodnota `wait_clk` pochází ze dvou hot-path míst, kde emulátor vkládá
WAIT cycles:

1. **MREQ do VRAM v MZ-700 mode** - WAIT kvůli synchronizaci s HBLN
   (= aby CPU nezapsal do VRAM během video čtení). Velikost dle aktuální
   raster pozice.
2. **OUT na PSG** - WAIT kvůli synchronizaci CPU CLK s PSGCLK (PSG je
   1.108 MHz, CPU 3.546 MHz; write musí trefit PSG cycle boundary).

Akumulátor WAIT T-states je saturující uint32. Pokud HALT/self-loop
collapse je aktivní, akumulace pokračuje napříč iteracemi (= součet
všech WAIT + všech standard T-states od první iterace).

`insn_length`/`insn_bytes` v eventu odpovídá **standardní instrukci**
(= bez WAIT). Standardní délka v T-states je odvoditelná z opcode přes
Z80 ISA (viz dále).

Délka samotné instrukce v T-states **se neukládá** - je deterministicky
odvoditelná z opcode podle Z80 ISA tabulky. U podmíněných skoků
(taken/not-taken se liší v T-states) externí post-process dopočítá
ze stavu flagů (z re-simulace).

### HALT a self-loop collapse

HALT, `JP $`, `JR $-2`, `CALL` na sebe (= libovolná instrukce, která
zanechá PC na stejné adrese a opakuje se) se sbaluje do **jednoho
eventu** s `wait_clk` = celkový čas strávený ve smyčce (do exit přes
IRQ accept / state change).

Důsledek: tight-loop "wait for IRQ" idiom v MZ-800 monitoru / hrách
(`HALT` nebo `JR $-2` v polling smyčce) negeneruje desetitisíce events
za sekundu, ale jeden event s `wait_clk` ekvivalent doby čekání. To
je side-effect cputrack designu - **bez collapse by byl log na real
software nepoužitelný**.

Externí re-simulátor pozná collapsed HALT jednoduše: opcode = `0x76`
(HALT) + nenulové `wait_clk`. Pozná collapsed self-loop: instrukce
mění PC na PC, a `wait_clk` > 0.

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

### Klíčová pole

- `subsys` - vždy `"cputrack"`.
- `platform` - `"MZ-800"`, `"MZ-700"` nebo `"MZ-1500"`.
- `pxclk_freq` - frekvence pixel clock (Hz). MZ-800/700/1500 standard:
  17734475 (= 17.7 MHz). Lze odvodit i `cpu_freq = pxclk_freq /
  cpu_divider`.
- `cpu_divider` - dělič pxCLK pro CPU CLK. MZ-800: `5` (CPU 3.546895 MHz).
- `pxclk_per_screen` - počet pxCLK na jednu obrazovku (raster cycle).
  Použitelné pro raster-aligned analýzu.
- `chunks[]` - seznam chunků v pořadí. Každý chunk je nezávislý
  binární soubor.
- `truncated` - `true` pokud recording dosáhl `max_total_mb` limitu.
- `truncated_reason` - vysvětlení (`"max_total_mb"` nebo prázdné).

### chunks[i] anchor

Každý chunk má anchor s `start_pxclk`, `start_cpuclk`, `start_screens`
v okamžiku začátku chunku. To umožňuje **O(1) seek na chunk** + lineární
scan uvnitř pro rekonstrukci timestampu kterékoliv konkrétní instrukce:

```
cumul_cpuclk(N) = chunk.start_cpuclk + sum_{i=0..N}( insn_T_states[i] + wait_clk[i] )
cumul_pxclk(N)  = chunk.start_pxclk + (cumul_cpuclk(N) - chunk.start_cpuclk) * cpu_divider
screens_at(N)   = floor(cumul_pxclk(N) / pxclk_per_screen)
pxclk_in_screen_at(N) = cumul_pxclk(N) mod pxclk_per_screen
```

`insn_T_states[i]` se odvozuje z opcode podle Z80 ISA tabulky.

### subsys_header.initial_state

`regs` obsahuje kompletní snímek Z80 CPU registrů v okamžiku startu
recordingu (před prvním logovaným eventem). Hodnoty jsou hex stringy
(uint16 jako `"0xABCD"`, uint8 jako `"0xCD"`).

`memory_dumps[]` referencuje binární soubory s initial dumps fyzických
pamětí. Každá položka má `region` (= symbolický název), `file`
(= relativní cesta vůči adresáři meta souboru), `bytes` (= velikost).

Initial state + sekvence events je **hermeticky uzavřený dataset** -
externí re-simulátor z toho deterministicky odvodí stav paměti
v každém okamžiku (= žádný HW na MZ-800/700/1500 nemění RAM autonomně,
veškerá data se objevují přes IORQ, který logujeme zvlášť v `iorqlog`).

## Initial memory dumps

Per-platforma se mohou lišit jednotlivé regiony. V hlavičce jsou jen
ty, které pro danou architekturu existují / jsou připojené.

### MZ-800

| Soubor | Velikost | Popis |
|--------|----------|-------|
| `<name>_initial_ram.bin` | 64 KB | Hlavní DRAM |
| `<name>_initial_vram.bin` | 32 KB | VRAM 4 banky (linear) |
| `<name>_initial_exvram.bin` | 32 KB | EXVRAM (linear) |
| `<name>_initial_memext.bin` | 512 KB | Memext RAM (jen pokud připojen) |
| `<name>_initial_rd0.bin` | variabilní | RAM-disk 0 (jen pokud aktivní) |
| `<name>_initial_rd1.bin` | variabilní | RAM-disk 1 (pokud aktivní) |

ROM se **neukládá** - je deterministicky známá z compile-time embedded
ROM image (= verze v `meta.json` přes `platform`).

### MZ-1500

| Soubor | Velikost | Popis |
|--------|----------|-------|
| `<name>_initial_ram.bin` | 64 KB | Hlavní DRAM |
| `<name>_initial_cgram.bin` | 4 KB | CG-RAM |
| `<name>_initial_pcg.bin` | 24 KB | 3× 8 KB PCG bank (R/G/B) |
| `<name>_initial_memext.bin` | 512 KB | Memext (pokud připojeno) |

## Použití

### Z UI

`Debugger Settings -> Trace Suite -> CPU Track`:

1. Vybrat režim recording (Off / Only With Debug Window / Always).
2. Volitelně zapnout `Save on Exit` (default zapnuto).
3. Nastavit cílový adresář a basename, případně `chunk_mb` /
   `max_total_mb`.
4. Spustit emulovaný program. Recording běží na pozadí.

### Načtení dat v Pythonu (příklad)

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
# Příklad: kolik bylo HALT instrukcí
halt_count = sum(1 for (_, l, b, _) in events if l == 1 and b[0] == 0x76)
```
