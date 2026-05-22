# CPU okno - registry Z80 v samostatném plovoucím okně

**CPU Registers** je samostatné dokovatelné ImGui okno zobrazující živý
stav Z80 CPU - hlavní i alternativní registr file, index registry,
flagy, řídicí stav (I, R, IFF1, IFF2, IM) a sekci s cycle/raster
informacemi. Většina hodnot je editovatelná (klik = pauza emulátoru +
in-place edit).

## Otevření / zavření

- **Menu:** Debugger -> CPU Registers
- **Klávesová zkratka:** Alt+R (toggle viditelnosti)
- **Default visibility:** zavřené při startu emulátoru. Uživatel ho
  otevírá explicitně přes menu nebo zkratku. Po prvním otevření se
  pozice a velikost okna persistuje přes `imgui.ini`.

## Layout

Samostatné plovoucí okno s auto-fit velikostí podle obsahu. Vlastní
manuální resize je vypnut - okno se přizpůsobuje obsahu (tabulkový
layout má pevné sloupce).

```
+----------- CPU Registers ----------+
| F: [s][z][5][h][3][P][n][c]        |
|                                    |
| > AF  : 0604    > AF' : 0000       |
| > HL  : E001    > HL' : 0000       |
| > DE  : 0005    > DE' : 0000       |
| > BC  : 0DFF    > BC' : 0000       |
| > PC  : E6C4    > IX  : CEE9       |
| > SP  : 10E4    > IY  : 0000       |
| > R   : 6F      > I   : 00         |
|   IFF1: ON        IM  : [1 v]      |
|   IFF2: ON                         |
|                                    |
| VECA: FE34    ISRA: 1234           |   (jen MZ-800 / MZ-1500)
| VECB: FE36    ISRB: 5678           |
+------------------------------------+
| v Cycles & raster                  |
| Frame: 534  Line: 0  Col: 12       |
|                                    |
| Frame cyc : 0                      |
| Total cyc : 37853340               |
| User cyc  : 37853340  [Set0]       |
+------------------------------------+
```

### Flag rozpis F

Řádek nad tabulkou registrů zobrazuje 8 bitů registru F:
**S Z 5 H 3 P N C** (bit 7..0).

- Velký zlatý symbol = bit nastaven (1)
- Malý šedý symbol = bit vynulován (0)
- Hover ukazuje tooltip s vysvětlením flagu

V paused módu klik na bit toggluje příslušný bit ve F registru. V running
módu klik vyvolá silent pauzu (bez modálního dialogu) - bit se v té chvíli
netoggluje, uživatel musí kliknout znovu už v paused stavu.

### Tabulka registrů

Tabulkový layout (4+4 sloupce + separátor). Pořadí:

- **Levá polovina:** AF, HL, DE, BC, PC, SP, R, IFF1, IFF2
- **Pravá polovina:** AF', HL', DE', BC', IX, IY, I, IM

8-bit registry R a I jsou v samostatných řádcích. IFF1 / IFF2 se
zobrazují jako `ON` / `OFF`, IM jako combo `0 / 1 / 2`.

### PIO-Z80 VEC/ISR (jen MZ-800 / MZ-1500)

Pod tabulkou registrů jsou na MZ-800 a MZ-1500 dva řádky pro IM 2
interrupt vectoring (MZ-700 sekci nemá - PIO-Z80 tam není):

- **VECA** = `(I << 8) | (interrupt_vector_A & 0xFE)`
- **VECB** = `(I << 8) | (interrupt_vector_B & 0xFE)`
- **ISRA** = `MEM[VECA] | (MEM[VECA+1] << 8)`
- **ISRB** = `MEM[VECB] | (MEM[VECB+1] << 8)`

VEC* je adresa dvoubajtové buňky v ISR vector tabulce (= místo, kam
ukazuje Z80 v IM 2 cyklu). ISR* je dereference té buňky (= cílová
adresa ISR rutiny).

Editace:

- **VEC*** se zapisuje atomicky ve dvou krocích: horní byte = nový I,
  dolní byte = nová hodnota interrupt vektoru daného portu PIO-Z80
  (maska `& 0xFE`, bit 0 je dle specifikace vždy 0). Změna I se promítne
  do obou VEC* (sdílí horní byte).
- **ISR*** zapisuje 2 bajty (little-endian) do paměti na adresu VEC*.
  Pokud kterákoliv dotčená adresa leží v ne-zapisovatelném regionu
  (ROM, CG-ROM, VRAM I/II na MZ-800, memory-mapped porty E000-E00F,
  prohibited region, unmapped), žádný bajt se nezapíše a otevře se
  modal warning s adresou a popisem regionu. Zapisovatelné jsou RAM,
  VRAM_TEXT, CGRAM a PCG banky.

## Focus button `>` (navigační, NEpauzuje)

Před každým 16-bit párem registrů je tlačítko `>`.

- **Levý klik** = fokus do hlavní Disassembly na adresu = hodnota
  registru. Pokud je hlavní debug okno zavřené, otevře se. Nepauzuje
  emulaci - navigace funguje i během běhu.
- **Pravý klik** = popup s volbami:
  - Focus in Disassembly (main) / Focus in Disassembly #2..#5
  - Toggle format (cyklus hex -> dec -> bin)
  - Add to Watch $<name> = $<hex>
  - Add breakpoint at <reg> ($<hex>)
  - Copy hex / Copy dec / Copy bin

Pro 8-bit virtuální registry I a R (= ne 16-bit adresa) popup nabízí
jen Copy hex / dec / bin a Toggle format. Focus, Watch a Add breakpoint
se neukazují (nedávají na 8-bit hodnotu smysl).

### Omezení Add to Watch

Watch záznam je **statický snapshot** v okamžiku přidání - hodnota
se nezískává živě při změně registru. Pro skutečné live tracking
neexistuje aktuálně dedikovaný subsystém.

## Editace hodnoty registru

Klik na hodnotu registru:

- **V paused mode** = otevře in-place InputText nad hodnotou.
- **V running mode** = silent autopauza (= pauza bez modálního info
  dialogu) + okamžité otevření InputText. Žádný "second click required".

Klávesy v edit modu:

- **Enter** - parse hodnoty dle aktuálního formátu, validace range,
  zápis hodnoty. Při neplatném vstupu zůstává v edit modu (bez
  modálního dialogu) - lze opravit.
- **Esc** - cancel beze změny.
- **Tab** - apply + skok na další registr v pořadí:
  AF, BC, DE, HL, IX, IY, SP, PC, AF', BC', DE', HL', R, I.
- **Klik mimo InputText** - cancel.

Pokud emulátor přejde do running stavu zatímco je edit otevřený
(Play / step / breakpoint resume), edit se automaticky cancelne.

### Per-register format

Cyklus přes položku **Toggle format (cur -> next)** v RMB popupu nad
`>` tlačítkem. Formáty:

- **HEX** (default): 4 znaky velkým písmem, bez prefixu (`042A`).
- **DEC**: unsigned 0..65535.
- **BIN**: 16-bit oddělený mezerou na dvě 8-tice (`00000100 00101010`).

Format je session-only - neperzistuje, při restartu emulátoru se
vrací na HEX. Šířka sloupce hodnoty je pevně fixovaná (5 monospace
glyfů), takže přepínání formátu nezpůsobí jitter layoutu.

### Parser - tolerance a range

Vstup tolerován:

- HEX prefix `0x` / `$` (volitelný), znaky `0-9` `a-f` `A-F`
- DEC znaménko `+` / `-`, znaky `0-9`
- BIN prefix `0b` (volitelný), znaky `0` `1`
- Whitespace, podtržítko a oddělovače `_` uvnitř čísla

Range:

- 16-bit unsigned: 0..65535
- 16-bit signed: -32768..32767 (DEC se znaménkem, převedeno na
  two's complement)
- 8-bit unsigned: 0..255 (R, I)

Při overflow nebo neplatném znaku vrací false a edit zůstává otevřený.

### Editace IFF1 / IFF2 / IM

- **IFF1 / IFF2** se zobrazují jako SmallButton `ON` / `OFF`. Klik
  v paused mode toggluje, v running mode silent autopauza + toggle.
- **IM** je combo s položkami `0`, `1`, `2`. Změna value silent
  pauzou + zápisem nového módu.

## Change highlighting (golden fade)

Po každém refresh ticku se hodnoty porovnají s předchozími. Změněný
registr se na 1.5 s rozsvítí zlatou barvou a postupně linearně faduje
zpět na default. 8-bit virtuální registry (I, R) tento fade nemají.

## Cycles & raster sekce

Default collapsible sekce pod tabulkou registrů. Power-user informace,
ve výchozím stavu sbalená (= šetří refresh roundtripy do emu vlákna).

První řádek: raster pozice (`Frame: NNN  Line: NNN  Col: NNN`).

Pod ní tabulka counterů:

| Sloupec | Význam |
|---------|--------|
| Frame cyc | T-states v aktuálním snímku |
| Total cyc | Kumulativní T-states od resetu |
| User cyc | Stopwatch - rozdíl `total_cycles - origin` |

**Set0** vedle User cyc nastaví origin na aktuální `total_cycles`,
takže displej spadne na 0. Slouží jako interakční stopwatch pro
měření rozdílu T-states mezi dvěma body. Edit kliknutím na hodnotu
User cyc - parse decimal, vypočte nový origin.

Counter je 32-bit; při běžné rychlosti Z80 přeteče po cca 20 minutách.

## Refresh cyklus

Panel je napojen na centralizovaný refresh kontroler (interval 100 ms).
Při force refresh (pauza, step, RESET, akce z context menu) proběhne
update okamžitě.

Refresh probíhá v jediném batch roundtripu do emu vlákna - per-sekce
gating zajišťuje, že collapsed sekce neposílají data dokud nejsou
rozbalené. Default-collapsed sekce (Cycles & raster) nepřispívá k
refresh nákladu dokud ji uživatel neotevře.

## Persistence

Konfigurační soubor `imgui.ini` drží pozici okna. Visibility flag
(otevřené / zavřené) je session-only - výchozí stav po startu je
zavřené.

Per-register format, edit historie a fade timer jsou session-only.

## Klávesové zkratky (shrnutí)

| Zkratka | Akce |
|---------|------|
| Alt+R | Toggle viditelnosti CPU okna |
| Enter | Apply edit hodnoty |
| Esc | Cancel edit |
| Tab | Apply + skok na další registr |

## Související dokumentace

- [stack-window.md](stack-window.md) - Stack Monitor okno
- [breakpoints.md](breakpoints.md) - Breakpointy a podmínkové BP
