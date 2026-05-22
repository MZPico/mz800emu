# Stack Monitor okno - hex dump kolem SP + stack regions

**Stack Monitor** je samostatné dokovatelné ImGui okno zobrazující
surový obsah paměti zásobníku v okolí aktuálního Z80 SP, doplněný
registrem pojmenovaných stack regionů s low-water mark trackingem,
historií SP a heuristickým dekodérem návratových adres.

**NENÍ to callstack** (= rekonstrukce CALL/RET historie); jde o pohled
na to, co Z80 SW reálně používá jako stack v paměti.

Samostatné okno **Stack History** zobrazuje SP history sparkline ve
větším měřítku (= detailní průběh SP v čase).

## Otevření / zavření

- **Menu:** Debugger -> Stack Monitor
- **Klávesová zkratka:** Alt+S (toggle viditelnosti)
- **Default visibility:** zavřené při startu emulátoru. Uživatel
  otevírá explicitně přes menu nebo zkratku. Pozice a velikost okna
  se persistuje přes `imgui.ini`.

Související samostatné okno Stack History:

- **Menu:** Debugger -> Stack History
- **Klávesová zkratka:** Alt+Shift+H
- Aktivní jen pokud je v hlavním Stack Monitor okně zapnutý
  checkbox `SP history`. Sdílí state (vybraný sample, Show events
  toggle) s hlavním oknem.

## Layout

```
+--- Stack Monitor ----------------------------------------+
| SP: 10D2  Depth: 30 B  Region: [system v] [Reset W]      |
| [Set BP from SP-256]  [+ Add region from current SP]     |
| [x] SP history   [x] Lock SP center   [stack creep]      |
+-------------------+--------------------------------------+
| Addr  Byte  Word  Decode      | Regions                  |
| 10F0  ..    ..    --          | Name             Trend   |
| 10EE  CD 4A CD4A  [ret CALL]  | system    [sparkline]    |
| 10EC  80 40 4080  --          | Base Limit SP%  Min Act  |
| ...                           | 10F0 1000  18%  10C4 RX  |
| 10D2  AB CD ABCD  > --        |                          |
| 10D0  .. ..       = --        | game        ...          |
| ...                           |                          |
+-------------------+--------------------------------------+
| v SP history                                             |
| [sparkline plot full width                              ]|
| Samples 1234  First 10F0  Last 10C8  Slope -1.23e-5      |
+----------------------------------------------------------+
```

Sloupce hex dump tabulky:

| Sloupec | Význam |
|---------|--------|
| Addr | Adresa řádku, 4 hex znaky |
| Byte | Surové bajty (2 v word módu, 1 v byte módu) |
| Word | LE word (= bajt + následující), zobrazený jako kandidát na return-addr. V byte módu disabled (`--`) |
| Decode | Heuristický decode kandidáta na návratovou adresu (CALL/RST). Klikatelné. |
| M | Marker `>` na řádku aktuálního SP, `=` na watermark vybraného regionu. `>` má přednost pokud SP == watermark. |

Sticky header:

| Pole | Význam |
|------|--------|
| SP | Aktuální hodnota Z80 SP (s `h` suffixem pro čitelnost) |
| Depth | `base - sp_now` pokud je SP ve vybraném regionu, jinak `--` |
| Region dropdown | Combo se seznamem všech definovaných regionů + `(none)` |
| Reset W | Vynuluje watermark + push/pop countery vybraného regionu. Disabled pokud žádný region nevybrán. |
| Set BP from SP-256 | Quick action - vytvoří execution BP typu SP_THRESHOLD s threshold = current_sp - 256 |
| + Add region from current SP | Otevře modal pro definici nového regionu |
| SP history | Toggle nahrávání průběhu SP do ring bufferu |
| Lock SP center | Přepnutí layoutu hex dumpu mezi default 32/8 split (SP v dolní třetině) a 20/20 split (SP uprostřed) |
| [stack creep] | Oranžový warning text - viditelný jen pokud detekce zjistila pomalu klesající SP (memory leak ve stacku) |

### Side panel Regions

Per-region "card" se dvěma minigridy:

```
+-----------------------------------+
| Name              | Trend         |
+-----------------------------------+
| <region_name>     | <sparkline>   |
+-----------------------------------+
| Base | Limit | SP% | Min | Act    |
+-----------------------------------+
| XXXX | XXXX  | NN% | XXX | [R][X] |
+-----------------------------------+
```

| Pole | Význam |
|------|--------|
| Name | Jméno regionu. Klik na řádek = vyber regionu (sync s dropdownem) |
| Trend | Mini sparkline filtrovaná na vzorky, kde SP ležel uvnitř regionu. Tooltip ukazuje počet filtrovaných vzorků. |
| Base | Vrchol regionu (nejvyšší adresa) |
| Limit | Dno regionu (nejnižší povolená adresa) |
| SP% | `(base - sp_now) * 100 / (base - limit)` pokud SP ∈ region, jinak `--` |
| Min | Watermark = nejnižší zaznamenaný SP |
| Act | `R` (reset watermark + countery), `X` (delete region) |

### Splittery

Layout okna obsahuje dva splittery:

- **Vertikální splitter** mezi hex tabulkou a Regions panelem
  (kurzor `ResizeEW` při hover). Drag mění poměr šířek.
- **Horizontální splitter** mezi top sekcí (hex + regions) a bottom
  sekcí (SP history sparkline). Kurzor `ResizeNS`. Drag mění výšku
  obou sekcí.

Pozice splitterů jsou session-only (resetují se při restartu).

## Stack regions

Pojmenované záznamy `<base, limit>` s runtime statistikami (watermark,
push/pop countery). Max 8 regionů. Slouží k vymezení oblastí paměti,
které program používá jako stack(y) - CP/M BIOS má typicky 3 stacky
(STCK0/STCK1/STCK3 + aplikační), různé sub-systémy mohou mít
samostatné stacky atd.

### Watermark a countery

Pro každý region se sleduje:

- **watermark** - nejnižší SP, kterého region kdy dosáhl (= jak hluboko
  šel stack v rámci regionu)
- **push_count** - počet PUSH-like událostí (SP klesl o 2). Zahrnuje
  PUSH, CALL, RST a INT acknowledge (= implicit PUSH PC).
- **pop_count** - počet POP-like událostí (SP vzrostl o 2): POP, RET.
- Změny SP, které nejsou o ±2 (`LD SP,nn`, `EX (SP),HL`, `INC SP`,
  `DEC SP`) neaktualizují countery, ale watermark se updatuje vždy.

Default OFF stav (žádný region definovaný) = zero overhead v hot-path
emulátoru. Aktivní stav = jeden SP change check + lookup do max 8
slotů.

### Marker `=` watermark

V hex dump tabulce se na řádku odpovídajícím watermark **vybraného**
regionu zobrazuje znak `=` ve sloupci M. Pokud SP == watermark,
přednost má marker `>` (= SP).

### Reset semantika

- **Emu reset** - definice regionů se zachovají, watermark + countery
  všech regionů se vynulují (= "spustit znovu od začátku").
- **Tlačítko Reset W** (header) - vynuluje watermark + countery jen
  vybraného regionu.
- **Tlačítko R** (side panel Act sloupec) - totéž pro daný region.
- **Tlačítko X** (side panel) - odebere region úplně.

## Add region modal

Tlačítko **+ Add region from current SP** otevře dialog:

- **Name** - text input, default `region_N` kde N = první volný index.
  Validace: neprázdné, max 31 znaků, znaky `[a-zA-Z0-9_]+`.
- **Base (hex)** - 4 hex znaky, default = current SP.
- **Limit (hex)** - 4 hex znaky, default = base - 256.

Validace v UI dává rychlou zpětnou vazbu (název, hex format,
`base > limit`). Při úspěšném potvrzení se nový region autoselect-uje
v dropdownu i side panelu. Při neúspěchu zůstává dialog otevřený s
chybovou hláškou.

## Set BP from SP-256

Quick action v sticky header pro rychlý safety net "pokud stack
klesne o víc než 256 B pod aktuální hodnotu, zastav":

1. `threshold = current_sp - 256` (16-bit wrap při SP < 256 zachován
   jako-je; uživatel hodnotu vidí a může upravit v BP edit panelu)
2. Vytvoří execution breakpoint typu **SP_THRESHOLD** s nastaveným
   threshold a SINGLE módem (default sémantika "fire když SP < threshold")

User si může v BP edit panelu přepnout na WINDOW mód, změnit hodnoty
atd. Tlačítko je disabled pokud cache zatím není naplněna (cold start
nebo emu offline).

## Decode sloupec (heuristika návratových adres)

Pro každý řádek hex dump tabulky:

1. Vezme word `W = mem[addr] | (mem[addr+1] << 8)` (LE).
2. Otestuje opcode na adrese `W-3`:
   - `0xCD` = `CALL nn` (3 bajty); target = `mem[W-2] | (mem[W-1] << 8)`
   - `0xC4 / 0xCC / 0xD4 / 0xDC / 0xE4 / 0xEC / 0xF4 / 0xFC` =
     `CALL cc,nn` (3 bajty, podmíněný)
3. Pokud žádný z výše: otestuje opcode na adrese `W-1`:
   - `0xC7 / 0xCF / 0xD7 / 0xDF / 0xE7 / 0xEF / 0xF7 / 0xFF` =
     `RST n` (1 bajt); target = `opcode & 0x38`
4. Jinak NONE - sloupec ukáže `--`.

Decode formáty:

- `[ret CALL XXXX]` - unconditional CALL
- `[ret CALL cc XXXX]` - conditional CALL (konkrétní podmínka neuvedena)
- `[ret RST XX]` - RST n
- `--` - NONE nebo byte-mode (lichý SP)

### Klikatelný Decode

- **LMB klik** = Focus primary Disassembly na target adresu. Stejně
  jako `>` button v CPU panelu - **bez pauzy**. Pokud je hlavní debug
  okno zavřené, otevře se.
- **RMB klik** = popup s volbami:
  - Focus in Disassembly (main) / Focus in Disassembly #2..#5
  - Copy target hex
- **Hover** = tooltip s opcode hodnotou, target adresou a poznámkou
  "Possible return address (heuristic). Click: focus primary
  Disassembly. Right-click: choose slot or copy."

Řádky bez decode info (`--` nebo byte-mode) zůstávají neklikatelné.

### Limit heuristiky

- **False positives** - jakákoliv data na stacku, která náhodou mají
  CALL/RST opcode 3 / 1 bajty před cílem, vyrobí matching decode.
  Bez callstack featury nelze 100 % odlišit "true return" od náhody.
  Decode je proto označen jako heuristika v tooltipu.
- **3-byte CALL ambiguity** - `CALL nn` (0xCD) vs `CALL cc,nn`
  (C4..FC) jsou obě 3 bajty, target offset stejný. UI zobrazí typ
  (CALL vs CALL cc), ale neukáže konkrétní podmínku (NZ/Z/NC/...).
- **IM2 vector dispatch** - v IM 2 módu CPU pushuje return adresu
  bez explicitní CALL instrukce v paměti. Tyto sloty zůstanou NONE.
- **Byte-mode (lichý SP)** - word kandidát není definovaný, decode
  sloupec je vždy `--`.

## SP history sparkline

Ring buffer 4096 vzorků `{cycles, sp}`. Vzorek se zaznamenává **při
každé změně SP**. Recording se zapíná checkboxem `SP history` v sticky
header.

- Default OFF = zero overhead.
- Pri zapnutí flag persistuje do `.ini`, takže se zachová mezi
  restartami.

### Plot

Sparkline pod hex tabulkou (default expanded). Vykresluje:

- Křivka SP v čase (X = pořadí vzorku, Y = SP hodnota)
- Volitelné vertikální markery push / pop / other event (toggle
  checkbox **Show events**)
- Hover crosshair s tooltipem (idx, SP, cycle, delta od minulého
  vzorku)
- LMB klik na sample = persistent žlutý crosshair pro vybraný sample;
  klik mimo nebo tlačítko Clear ho zruší

Pod plotem info text:

- `Samples N` (first / last SP, first / last cycle)
- `Slope X.XXe-Y` - linearní regrese SP vs cycles přes posledních
  256 vzorků
- `Selected: idx=N SP=XXXX [Clear]` - viditelné jen při aktivním výběru

### Stack creep detekce

Pokud `slope` z lineární regrese klesne pod prahovou hodnotu a buffer
obsahuje alespoň 256 vzorků, ve sticky header se rozsvítí oranžový text
`[stack creep]` s tooltipem aktuální slope hodnoty - indikuje pomalu
klesající SP (asymetrický PUSH/POP, leak ve stacku).

### Reset emu

Při resetu emulátoru se ring buffer vyprázdní, sparkline se vrátí na
prázdný stav. Recording flag se zachová.

### Stack History okno

Samostatné plovoucí okno (Alt+Shift+H nebo Debugger -> Stack History)
ukazuje stejnou sparkline ve velikosti celého okna (plot resize s
oknem ve X i Y). Default velikost 800x300 px.

- Sdílený state s hlavní sekcí - vybraný sample / Show events /
  recording flag se mezi okny propaguje.
- Pri vypnutém recording okno ukazuje hint "(SP history is disabled,
  enable in Stack Monitor)" místo plotu.
- Persistence viditelnosti okna v `.ini`.

## Lock SP center

Toggle v sticky header (checkbox `Lock SP center`):

- **Default OFF** - hex dump tabulka má asymetric layout 32 řádků nad
  SP a 8 pod (SP v dolní třetině). Vhodné pro sledování již použitého
  stacku.
- **ON** - symetric 20/20 split (SP uprostřed). Vhodné pokud chceš
  vidět hluboké pushe i prostor pod SP zhruba stejně.

Hodnota persistuje do `.ini`.

## Color highlighting

### Per-řádek background podle regionu

Řádek hex dump tabulky spadající do některého stack regionu má modrý
background:

- **Aktivní region** (= vybraný v dropdownu): syté tónování
- **Jiné regiony**: tlumené (cca poloviční alpha)
- **Mimo regiony**: default RowBg (bez highlight)

Pokud dropdown ukazuje `(none)`, všechny regiony se zobrazí tlumeně -
uživatel pořád vidí, kde leží, i bez aktivního výběru.

### Word fade highlight

Při změně Word hodnoty (= memory write na té adrese) cell krátce
zlátne a postupně linearně faduje 1.5 s zpět na default. Funguje
**jen v word módu** a **jen pro skutečné memory writes** - auto-scroll
a SP move nezpůsobí fade (řádek by ukazoval jinou adresu, takže
hodnota se "změnila" jen vizuálně).

Multiple writes ve stejné cell během fade efektu = fade restartuje.

## CDL "S" klasifikace v Memory Heatmap

Memory Heatmap (samostatné okno) má vedle kategorií `R/W/X` i kategorii
`S` (Stack write). Klasifikace je region-based:

- Pokud je definován alespoň jeden stack region a zápis cílí do rozsahu
  `<region.limit..region.base>`, heatmap zaznamená event jako **S**
  místo W.
- `S` a `W` jsou exkluzivní pro jeden write event.

V Memory Heatmap okně:

- Filter bar - 5. radio button `[S]` (S-only monochrome mode)
- Checkbox `Show S` - toggle viditelnosti S kategorie v RGB blendingu
  i threshold testu
- Color popup - ColorEdit3 pro S counter (default cyan)
- Hover tooltip per-cell + Selected cell sidebar: `S = N (X.XX%)`
- Region statistics: `Total S` + `Max S`

Default OFF stav (žádný region definovaný) = `S` counter zůstává 0
napříč celou pamětí, chování je identické s pre-S verzí.

## Konstanty

| Konstanta | Hodnota | Význam |
|-----------|---------|--------|
| Default lines above SP (asymetric) | 32 | Řádky nad SP - default layout |
| Default lines below SP (asymetric) | 8 | Řádky pod SP - default layout |
| Total lines | 40 | Celkový počet řádků hex tabulky |
| Stack regions max | 8 | Maximální počet definovaných regionů |
| Region name max | 31 | Max délka jména (bez '\0') |
| SP history ring | 4096 | Velikost ring bufferu vzorků |
| Word fade duration | 1.5 s | Délka golden fade při memory write |
| Refresh interval | 100 ms | Frekvence updatu okna |

## Persistence

Konfigurační soubor `cfgmain.ini`:

### Sekce `[STACK_REGIONS]`

| Klíč | Typ | Popis |
|------|-----|-------|
| `count` | unsigned | Počet definovaných regionů (0..8) |
| `region_<i>_name` | text | Jméno regionu i (i=0..7) |
| `region_<i>_base` | unsigned | Base adresa regionu i |
| `region_<i>_limit` | unsigned | Limit adresa regionu i |

Watermark, push/pop countery a ring buffer SP history se NEpersistují
(runtime state, reset při startu emu).

### Sekce `[STACK_HISTORY]`

| Klíč | Typ | Default | Popis |
|------|-----|---------|-------|
| `enabled` | bool | 0 | Recording flag SP history |

### Sekce `[STACK_PANEL]`

| Klíč | Typ | Default | Popis |
|------|-----|---------|-------|
| `lock_sp_center` | bool | 0 | Lock SP center toggle |
| `show_regions_window` | bool | 0 | Visibility samostatného Regions okna |
| `show_history_window` | bool | 0 | Visibility samostatného Stack History okna |

### Memory heatmap sekce `[DEBUGGER]` (S klasifikace)

| Klíč | Typ | Default | Popis |
|------|-----|---------|-------|
| `mhwindow_color_s_rgba` | unsigned | cyan | RGBA barva S kategorie |
| `mhwindow_show_s_category` | bool | 1 | Toggle Show S v heatmap UI |
| `mhwindow_color_mode` | keyword | RGB | Rozšířen o hodnotu `S` |

### Lifecycle

- **Load** při startu emulátoru: regiony se naplní s plnou validací
  (fail-soft pro invalid záznamy - hodnota se tiše ignoruje, ostatní
  pokračují).
- **Save** při ukončení emulátoru. Žádný autosave při ADD/REMOVE/RESET
  region - akceptovatelné riziko ztráty mezi exit a real disk write
  (regiony nejsou user dokument).

### Zpětná kompatibilita

Starý INI bez `[STACK_*]` sekcí: emu startne s default hodnotami
(count=0, enabled=0, lock_sp_center=0). Sekce se vytvoří při následujícím
exit. Žádný crash, žádná error hláška.

## Klávesové zkratky (shrnutí)

| Zkratka | Akce |
|---------|------|
| Alt+S | Toggle viditelnosti Stack Monitor okna |
| Alt+Shift+H | Toggle viditelnosti samostatného Stack History okna |

## Test workflow

Doporučené typy programů pro testování:

- **Krátký MZ-700 textový program** (defaultní SP 0x10F0) - rychlý
  smoke test: přidat region (base = 0x10F0, limit = 0x1000), sledovat
  watermark klesat během běhu.
- **IM 2 ISR demo** - viditelný PUSH/POP pattern v push_count/pop_count,
  ISR frame v hex dumpu kolem SP během IM 2 dispatch.
- **Program s mid-frame palette a heavy ISR** - hluboký stack, ověřit
  hluboký watermark a SP% bar v side panelu.
- **CP/M aplikace** - multi-stack scénář (BIOS 3-stack design:
  STCK0/STCK1/STCK3 vs aplikační stack).

Verifikace funkčnosti:

1. Otevřít debugger (Alt+D), otevřít Stack Monitor (Alt+S)
2. Header ukazuje aktuální `SP: XXXXh`, Region: (none), Depth: `--`
3. Klik **+ Add region from current SP** -> modal -> OK
4. Region se objeví v dropdownu i side panelu, dropdown auto-select
5. Header: Depth se aktualizuje na (base - sp_now)
6. Step Into / Step Over - hodnoty se updatují, marker `>` se posouvá
7. Po pár PUSH-ech: watermark v side panelu klesá pod base, marker `=`
   v hex dumpu se objeví na watermark řádku
8. Reset W: watermark se vrátí na base, marker `=` zmizí (resp. překryje
   `>` pokud SP == base)
9. Set BP from SP-256 - v BP overlay (Alt+B) vidět nový BPT typu
   SP_THRESHOLD s hodnotou (current_sp - 0x100)
10. X v side panelu - region zmizí, dropdown reset na `(none)`
11. Reset emulátoru - regiony zůstanou definované, watermarks se vynulují

## Související dokumentace

- [cpu-window.md](cpu-window.md) - CPU Registers okno
- [breakpoints.md](breakpoints.md) - Breakpointy včetně SP_THRESHOLD
