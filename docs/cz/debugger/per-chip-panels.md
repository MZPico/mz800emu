# Per-chip detail panely (CTC / PPI / Z80 PIO / PSG)

Čtyři samostatná ladicí okna, která zobrazují aktuální vnitřní stav
periferních obvodů emulovaného počítače. Doplňují obecný přehled portů
v okně **I/O Ports Overview** - tam vidíte ploché mapování portů a
jejich hodnotu, zde vidíte strukturovaný pohled na konkrétní čip
(stavy čítačů, klávesnicovou matici, latch fáze PSG, vektory přerušení
atd.).

Panely jsou výhradně **pozorovací** - jen čtou a zobrazují, nikdy
nemění běh emulace. Můžete je nechat otevřené trvale.

| Panel | Klávesa | Dostupný na |
|-------|---------|-------------|
| CTC 8253 State | Alt+Shift+C | MZ-700, MZ-800, MZ-1500 |
| PPI 8255 State | Alt+Shift+I | MZ-700, MZ-800, MZ-1500 |
| Z80 PIO State | Alt+Shift+Z | MZ-800, MZ-1500 (MZ-700 čip nemá) |
| PSG SN76489 State | Alt+Shift+G | MZ-800 (mono), MZ-1500 (stereo) (MZ-700 čip nemá) |

MZ-700 nemá obvody Z80 PIO ani PSG SN76489 - v buildu pro MZ-700 se
odpovídající položky menu, klávesové zkratky i okna vůbec nezobrazí.


## Aktivace a perzistence

Okno otevřete jednou z těchto cest:

- Menu **Debugger -> CTC State** (resp. PPI State / Z80 PIO State / PSG State).
- Klávesovou zkratkou **Alt+Shift+C / I / Z / G**.
- Z DBG Workplace - v submenu **Workplace -> CTC / PPI / Z80 PIO / PSG**
  zapnete, aby se panel otevíral současně s hlavním debugger oknem.
  Default vypnuto.

Pozice, velikost okna i stav rozbalených sekcí se pamatují mezi
spuštěními emulátoru. Polling stavu probíhá pouze pokud je okno
otevřené - zavřená okna emulátor nezatěžují.


## Společné UI prvky

- Tabulky mají **resizable sloupce** - šířky si nastavte tažením
  oddělovače; rozložení se uloží.
- Sekce uvnitř panelu jsou v rozbalovacích **collapsing headerech** -
  zhroucený stav se pamatuje.
- Hodnoty se vykreslují s tooltipem (přejed myší = popis pole).
- Hex 8-bit hodnota se zobrazuje vedle s binárním zápisem,
  hex 16-bit s decimálním zápisem.
- Flagy 0/1 jsou barevně zvýrazněné (zapnuto = zelené, vypnuto = šedé).
- Enum hodnoty (módy, stavy) jsou dekódované textem (např. `Mode 3
  Square Wave`), surové číslo je vedle.


## CTC 8253 State

Čip Intel 8253 je trojitý čítač / časovač. Tři nezávislé kanály
(CTC0, CTC1, CTC2), každý 16-bit, šest pracovních módů a volitelně
BCD čítání. CTC řídí audio (CTC0), generuje vlastní hodinový signál
a slouží jako zdroj přerušení.

### Last Control Word

Snímek posledního Control Word zapsaného CPU na řídicí port.
Dekódovaná pole:

| Pole | Bity | Význam |
|------|------|--------|
| SC | D7-D6 | Select Counter - 0 = CTC0, 1 = CTC1, 2 = CTC2, 3 = Read-Back |
| RLF | D5-D4 | Read/Load Format - Latch / LSB only / MSB only / LSB+MSB |
| Mode | D3-D1 | Pracovní mód 0 až 5 |
| BCD | D0 | 0 = binární čítání, 1 = BCD (4-dekádové) |

### Per-channel sekce

Každý kanál (CTC0, CTC1, CTC2) má vlastní rozbalovací sekci. Hlavní
pole:

| Pole | Význam |
|------|--------|
| `out` | Aktuální úroveň výstupu čítače |
| `gate` | Poslední úroveň GATE vstupu |
| `mode` | Aktuální mód (0 = Interrupt on Terminal Count, 3 = Square Wave atd.) |
| `bcd` | Binární / BCD čítání |
| `rlf` | Read/Load Format - kolik bytů a v jakém pořadí CPU čte/píše |
| `state` | Vnitřní stavový automat (LOAD / COUNTDOWN / WAIT_GATE1 ...) |
| `load_done` | Preset byl plně zapsán a aktivován |
| `rl_byte` | Index aktuálního bytu v probíhající Read/Load sekvenci |
| `preset_value` | Preset, na který se čítač nahrává |
| `value` | Aktuální hodnota čítače (živé odpočítávání) |
| `mode3_destination_value` | Jen Mode 3: cílová hodnota aktuální půlperiody |
| `mode3_half_value` | Jen Mode 3: polovina periody (50 % duty obdélník) |

### Tip - sledování mid-frame palette efektů

Pokud ladíte program, který přepíná paletu nebo border barvu uprostřed
snímku (klasický raster trik), sledujte v CTC State okně postup
`value` u CTC2. Jakmile dosáhne nuly, vygeneruje přerušení, ve kterém
hra zapisuje nové barvy. Když `value` zamrzne nebo se nepohybuje,
přerušení se negeneruje - obvykle špatně zapsaný preset nebo zakázaný
INT mask v PPI Port C.

### Tip - audio ticho

Pokud kanál CTC0 běží správně (`value` klesá od `preset_value` k nule
v Mode 3), ale ve sluchátkách je ticho, podívejte se do PPI State na
PC0 (audio mask) - hodnota 0 znamená utlumeno, 1 propustnuto.


## PPI 8255 State

Čip Intel 8255 má tři 8-bit porty (A, B, C). V MZ-800 / MZ-700 /
MZ-1500 obstarává centrální I/O - vzorkování klávesnice, čtení CMT
dat, ovládání motoru pásky, beeper mask, signál VBLN.

### Last Control Word

Poslední byte zapsaný na řídicí port PPI. PPI rozlišuje dvě
interpretace podle bitu D7:

- **Mode Set (D7 = 1)** - konfigurace módu a směru každého portu.
  Bity nastavují směry portů A, B, vyšší a nižší poloviny C a
  módové skupiny.
- **Bit Set/Reset (D7 = 0)** - jednorázové nastavení nebo vynulování
  konkrétního bitu Port C.

Panel zobrazí obě interpretace - aktivní (podle D7) běžnou barvou,
neaktivní zašedlou.

### Port A - klávesnicový sloupec

Port A je výstupní. CPU sem zapisuje, který sloupec klávesnicové matice
chce vzorkovat. Dekód bitů:

| Bity | Význam |
|------|--------|
| D3-D0 | Sloupec klávesnice (0-9) |
| D4 | JOY1 enable (active LOW, jen na MZ-800 / MZ-1500) |
| D5 | JOY2 enable (active LOW, jen na MZ-800 / MZ-1500) |

Na MZ-700 řádky JOY1/JOY2 zobrazí poznámku "not present on this arch".

### Port B - klávesnicová matice 10x8

Při IN na Port B vrátí PPI bity aktuálně vzorkovaného řádku klávesnice
podle sloupce nastaveného na Port A. Panel zobrazí celou matici
10 sloupců x 8 bitů najednou ve formě gridu se třemi módy:

| Mode | Co ukazuje |
|------|------------|
| **Combined** | Co reálně dostane CPU při IN (fyzická klávesnice AND virtuální) |
| **Physical** | Jen fyzická SDL klávesnice |
| **Virtual** | Jen virtuální klávesnice (VKBD / autotyping) |

Polarita je **active LOW** - buňka `*` zelená = klávesa stisknutá
(bit 0), buňka `.` šedá = klávesa uvolněná (bit 1). Záhlaví sloupců
0 až 9 je hodnota PA, řádky b0 až b7 odpovídají bitům v Port B.

### Port C - per-bit dekód

Port C má část bitů výstupních (D0-D3) a část vstupních (D4-D7).
Výstupní bity panel ukazuje přímo z PPI (klidový stav). Vstupní bity
D5-D7 se v reálném HW počítají až v okamžiku čtení a panel je zobrazí
zašedlé s poznámkou "computed on read" - nesleduje je proto, že
samotné jejich přečtení by mělo vedlejší účinek (vzorek CMT, vyhodnocení
GDG rasteru).

| Bit | Směr | Význam |
|-----|------|--------|
| D0 | OUT | CTC0 audio mask (0 = ticho, 1 = propustnuto). Není na MZ-700. |
| D1 | OUT | CMT data out (bit do pásky) |
| D2 | OUT | CTC2 INT mask (0 = blokováno, 1 = povoleno) |
| D3 | OUT | CMT motor control (rising edge přepíná motor) |
| D4 | IN  | CMT motor state (aktuální stav motoru) |
| D5 | IN  | CMT data in (počítáno při čtení) |
| D6 | IN  | Cursor blink generátor (počítáno při čtení) |
| D7 | IN  | VBLN - vertical blanking flag (počítáno při čtení) |

Pokud potřebujete sledovat D5-D7 v reálném čase i s vedlejšími účinky,
otevřete okno **I/O Ports Overview**, které čte port stejně jako CPU.

### VKBD autotype state

Spodní sekce (default sbalená) ukazuje stav virtuální klávesnice -
zbývající text k odeslání, fázi key-down / key-up, časování. Užitečné
pro ladění VKBD makro skriptů nebo pokud program nereaguje na vstup z
virtuální klávesnice.


## Z80 PIO State

Z80 PIO je dvouportový programovatelný I/O obvod od Zilogu, plně
integrovaný do daisy-chain přerušovacího systému Z80. Slouží zejména
pro joystick a paralelní rozhraní.

K dispozici **pouze na MZ-800 a MZ-1500**. MZ-700 tento obvod nemá,
v buildu pro MZ-700 není panel dostupný (menu položka, klávesová
zkratka i okno jsou compile-time vyřazené).

### Globální stav - INT / IEO + ICENA event

| Pole | Význam |
|------|--------|
| `interrupt` | Daisy-chain interrupt registr (bit INT, bit IEO) |
| `interrupt_port_id` | Port, který drží aktuální INT (Port A / Port B / NONE) |
| `icena_event` | Odložený ICENA event (čas, kdy se enable aplikuje) |
| `icena_event_port_id` | Pro který port je event naplánován |

INT bit nasvícený = PIO drží signál `/INT` aktivní na sběrnici.
IEO bit (Interrupt Enable Out) je výstup pro navazující zařízení v
daisy chain - když je 0, blokuje přerušení od zařízení s nižší
prioritou.

### Per-port (Port A, Port B)

Každý port má svou rozbalovací sekci.

#### Last Control Byte

Z80 PIO řídicí port je sekvenční stavový automat - význam zapsaného
bytu závisí na předchozí operaci. Panel dekóduje byte podle aktuálně
očekávaného typu (IVW Interrupt Vector, MCW Mode Set, ICW Interrupt
Control Word, IDW Disable Word, IOMCW IO mask, INTMCW Interrupt mask).

#### Hlavní pole portu

| Pole | Význam |
|------|--------|
| `mode` | Output (0) / Input (1) / Bidirectional (2) / Control (3) |
| `io_mask` | Mode 3 maska směru (bit 0 = output, 1 = input) - mimo Mode 3 zobrazeno zašedle |
| `ctrl_expect` | Jaký byte PIO očekává příště (Command / IOMCW / INTMCW) |
| `data_output` | Poslední byte zapsaný CPU na Data port |
| `masked_input` | Maskovaný snapshot, který IN vrátí |
| `interrupt_vector` | IM2 vektor (bit 0 vždy 0) |
| `icfnc` | Interrupt funkce - OR (jakýkoliv bit) / AND (všechny bity) |
| `iclvl` | Interrupt level - LOW (active 0) / HIGH (active 1) |
| `icmask` | Maska bitů sledovaných pro interrupt podmínku |
| `icena` | Interrupt enable - Enabled / Disabled |
| `port_int` | Stav INT pipeline portu (None / Pending / Received / Re-Pending) |

### Tip - joystick nereaguje

Pokud joystick nereaguje, sledujte v Z80 PIO State okně `data_output`
nebo `masked_input` odpovídajícího portu - hodnota se musí měnit, když
hýbete pákou. Pokud se nemění, problém je výš ve vrstvě (mapování
SDL joysticku v iface), ne v emulaci PIO.


## PSG SN76489 State

Programovatelný zvukový generátor - tři tónové kanály + jeden šumový
kanál. Na MZ-800 jeden chip (mono), na MZ-1500 dva chipy (stereo
Left + Right). MZ-800 podporuje stereo volitelně podle ROM presetu.

K dispozici **pouze na MZ-800 a MZ-1500**. MZ-700 tento obvod nemá,
v buildu pro MZ-700 není panel dostupný (menu položka, klávesová
zkratka i okno jsou compile-time vyřazené).

### Latch state

PSG má dvoubytový protokol. Nejdřív LATCH byte (bit 7 = 1) vybere
cílový kanál a typ následujícího data bytu (tone / attenuation),
pak DATA byte (bit 7 = 0) zapíše hodnotu.

| Pole | Význam |
|------|--------|
| Latched channel | Index kanálu (0-3), do kterého půjde příští DATA byte |
| Next DATA targets | TONE / NOISE config (modrá) nebo ATTENUATION (oranžová) |

### Per-kanál stav

Čtyři rozbalovací sekce - Channel 0, 1, 2 (vždy TONE) a Channel 3
(vždy NOISE).

#### Společná pole

| Pole | Význam |
|------|--------|
| Channel type | TONE / NOISE |
| Attenuation | 0-15. 0 = max hlasitost (0 dB), krok -2 dB, 15 = utlumeno (kanál vypnut) |

Sloupec dB v UI vypisuje například `0 dB (max)`, `-10 dB`, `silent (off)`.

#### TONE kanál

| Pole | Význam |
|------|--------|
| Tone divider | 10-bit hodnota 0-1023 |
| Frequency | Vypočítaná výstupní frekvence v Hz / kHz |
| MIDI note | Nejbližší MIDI nota + odchylka v centech (např. `A4 +5c`) |

Pokud je divider menší než 2, výstup je DC (žádná oscilace) - panel
zobrazí "DC (no oscillation)".

#### NOISE kanál (jen CH3)

| Pole | Význam |
|------|--------|
| Noise divider type | 0, 1, 2 = pevné PSG děličky 16 / 32 / 64; 3 = řízeno tone dividerem kanálu 2 |
| Noise feedback | PERIODIC (jednoduchý LFSR) / WHITE (XOR feedback, širší spektrum) |

### PSG osciloskopy

Pod stavovými poli každého kanálu je miniaturní osciloskop, který
vykresluje aktuální průběh výstupu daného kanálu.

Omezení, která je dobré znát:

- Osciloskop kreslí **přibližně 4 periody** signálu pevně, bez ohledu
  na frekvenci. Slouží k tomu, abyste poznali, že kanál opravdu osciluje
  a v jakém tvaru, ne k přesnému měření doby trvání.
- Noise kanál zobrazuje **náhodný vzorek šumového vzoru** - při každém
  vykreslení vypadá graf jinak. To je správné chování.
- Při utlumení (Attenuation = 15) je výstup čára na nule.

### Stereo (MZ-1500, volitelně MZ-800)

V stereo režimu panel zobrazí **PSG Left (chip 0)** a **PSG Right
(chip 1)** ve dvou samostatných sekcích. Každý chip má vlastní sadu
4 kanálů. V mono režimu se zobrazí jen jeden chip.

### Tip - tichý kanál

Pokud kanál hraje špatně:

1. Zkontrolujte Attenuation - 15 = úplně utlumeno.
2. Zkontrolujte Tone divider - hodnoty 0 nebo 1 znamenají DC, žádný
   slyšitelný tón.
3. U Channel 3 zkontrolujte typ - pokud máte zapnut NOISE, hraje šum,
   ne tón.
4. Pokud PSG vypadá v pořádku, podívejte se do PPI State na PC0
   (audio mask CTC0) - musí být 1, jinak je celá audio cesta
   utlumena.


## Vztah k I/O Ports Overview

I/O Ports Overview a per-chip panely se doplňují:

| Okno | Co zobrazuje |
|------|--------------|
| **I/O Ports Overview** | Plochý seznam všech portů a jejich poslední hodnota |
| **Per-chip State panel** | Vnitřní strukturu konkrétního čipu - sequencery, automaty, dílčí registry |

Typický postup: I/O Ports Overview nechte otevřené trvale jako přehled,
per-chip panely otevírejte tematicky podle toho, co ladíte (CTC State
při raster glitch, PSG State při zvukovém problému, PPI State při
problému s klávesnicí nebo CMT).
