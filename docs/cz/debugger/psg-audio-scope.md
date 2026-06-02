# PSG Audio Scope

Samostatné ladicí okno pro **dynamickou analýzu zvukového výstupu** čipu
PSG SN76489. Doplňuje okno **PSG State**, které ukazuje statický
snapshot vnitřních registrů a latch fáze. PSG Audio Scope se naopak
soustředí na **slyšitelný výstup v čase** - co kanály právě hrají,
jakou mají amplitudovou obálku a jaké noty se na chipu objevují.

Okno je výhradně **pozorovací** - jen čte aktuální stav PSG přes
neinvazivní zrcadlové rozhraní. Nemění běh emulace, lze jej nechat
otevřený trvale.

Dostupné **pouze na MZ-800 a MZ-1500**. MZ-700 čip PSG nemá - v buildu
pro MZ-700 není okno, menu položka ani klávesová zkratka dostupná.


## Aktivace a perzistence

Okno otevřete jednou z těchto cest:

- Menu **Debugger -> PSG Audio Scope**.
- Klávesovou zkratkou **Alt+Shift+A**.
- Z DBG Workplace - v submenu **Workplace -> PSG Audio Scope** zapnete,
  aby se okno otevíralo automaticky s hlavním debugger oknem. Default
  vypnuto.

Pozice, velikost, stav rozbalených sekcí a hodnota Tempo BPM se pamatují
mezi spuštěními emulátoru.

Vzorkování PSG pro vykreslení probíhá průběžně **i když je okno
zavřené**. Díky tomu po jeho otevření okamžitě uvidíte posledních
zhruba 10 sekund historie a piano roll už obsahuje doposud detekované
noty. Vzorkování má zanedbatelnou režii a je side-effect free vůči
emulátoru.


## Společné UI prvky

- Okno má **resizable** rozměry, sekce uvnitř jsou v rozbalovacích
  **collapsing headerech** - sbalený stav se pamatuje.
- Per-kanál grafy (osciloskop a envelope) automaticky **respondují na
  šířku okna**, takže při roztažení dostanete vyšší časové i amplitudové
  rozlišení.
- Bary v piano roll mají **hover tooltip** s detailem konkrétní noty.


## Layout okna

Okno je rozděleno shora dolů na tyto bloky:

1. **Toolbar** - export do MIDI / CSV + nastavení Tempo BPM.
2. **Info hlavička (statusline)** - dva řádky:
   - První řádek: frame counter, celkový počet zaznamenaných not a
     údaj o aktuálně hrajících notách.
   - Druhý řádek: trojice diagnostických checkboxů **Log samples**,
     **Log events**, **Log writes** a indikátor `(s:N e:N w:N)` s počty
     dosud zapsaných řádků v jednotlivých log souborech.
3. **Per-chip sekce PSG (chip 0)** - v mono režimu, případně **PSG Left
   (chip 0)** + **PSG Right (chip 1)** v stereo režimu.
   Každá sekce obsahuje 4 řádky (kanály 0-3), v každém řádku
   osciloskop + envelope.
4. **Piano roll** - timeline detekovaných not.


## Per-channel osciloskop

Pod štítkem `Channel N (TONE)` resp. `Channel N (NOISE)` je miniaturní
osciloskop, který kreslí aktuální průběh výstupu kanálu.

| Stav | Co se vykreslí |
|------|----------------|
| Aktivně hraje TONE | Synthetický obdélníkový průběh s aktuální frekvencí |
| Aktivně hraje NOISE | Pseudo-náhodný šumový vzor (při každém vykreslení vypadá jinak) |
| Attenuation = 15 | Štítek `silent` |
| Tone divider < 2 | Štítek `DC` (žádná oscilace) |
| Před prvním tickem | Štítek `no data` |

Omezení, která je dobré znát:

- Osciloskop kreslí přibližně **4 periody** pevně, bez ohledu na
  frekvenci. Slouží k tomu, abyste poznali, že kanál opravdu osciluje
  a v jakém tvaru, ne k přesnému měření doby trvání jedné periody.
- Šířka grafu **responzivně škáluje** s šířkou okna - roztažením
  získáte detailnější vykreslení.


## Per-channel envelope

Pod osciloskopem je druhý graf, který ukazuje **historii hlasitosti
kanálu v čase**. Vodorovná osa je čas (vpravo = teď, vlevo = až 10 s
zpět), svislá osa je hlasitost (nahoře = `Attenuation = 0`, dole =
`Attenuation = 15`, ticho).

Graf je vykreslen jako sytá plocha pod křivkou, takže okamžitě poznáte
**tvar ADSR obálky** - rychlý nárůst (attack), pokles na sustain
úroveň, případně rampu vypouštění (release). Pomáhá rozpoznat:

- Tremolo / vibrato (= pravidelné kmity hlasitosti v rámci jedné noty).
- Hru "polyfonie přes attenuation" (kanál si periodicky tlumí, aby
  zazněly přerušované noty).
- 1-bit PCM samply (= rychlé střídání plné hlasitosti a ticha v cyklu
  několika milisekund).


## Note event detector

Okno průběžně sleduje přechody mezi tichem a hraním per kanál a
detekuje **note on / note off** události:

- **Note on** = `Attenuation` klesne z hodnoty 15 (ticho) na hodnotu
  menší než 15.
- **Note off** = `Attenuation` se vrátí na hodnotu 15.

V okamžiku **note on** se zaznamenají tyto údaje:

| Údaj | Jak se získá |
|------|--------------|
| Pitch | Nejbližší MIDI nota odpovídající aktuálnímu Tone divideru |
| Cents | Odchylka skutečné frekvence od rovnoměrně temperované noty (-50 až +50 centů) |
| Velocity | 0-127, odvozená z hodnoty Attenuation (`0` = 127, `15` = 0) |
| Channel + chip | Z kterého zdroje pochází |

Důležité chování:

- Pitch se zachytává **jen v okamžiku note on**. Pokud se Tone divider
  mění během hraní noty (= glissando), nota se nerozdělí, ponechá si
  svůj počáteční pitch.
- Pokud kanál během hraní změní typ TONE -> NOISE (případně naopak),
  aktuální nota se uzavře a začne nová s odpovídajícím typem.
- NOISE noty nemají pitch (zaznamenává se `Noise`).
- Pokud kanál hraje déle, než dovolí kapacita interního bufferu (cca
  1000 not na kanál), nejstarší noty se přepíší. Pro běžnou hru je
  kapacita zhruba 10-20 minut soustavné hudby.

### Sledování změn hlasitosti v rámci noty

Kromě počátečního Attenuation se v průběhu noty zaznamenávají i
všechny **další změny hlasitosti** (= další zápisy do attn registru
stejného kanálu, dokud nota trvá). Každá změna se uloží jako bod v
interním seznamu (max 32 bodů na notu). Pokud nota přesáhne tento
limit, příznak "overflow" se nastaví a další změny se ztichu zahodí,
samotná nota běží dál nedotčená.

Tyto údaje se promítají do tří míst:

- **Piano roll** - uvnitř baru noty se kreslí krátké svislé tick
  marky v místech, kde došlo ke změně hlasitosti.
- **Tooltip noty** - kromě běžných údajů obsahuje řádek
  `Volume changes: N (attn min..max)`, případně doplněný značkou
  `(overflowed)`, pokud nota přesáhla limit 32 bodů.
- **Export CSV a MIDI** - viz sekce o exportu níže.


## Piano roll

Spodní rozbalovací sekce **Piano roll** zobrazí všechny zachycené
noty jako vodorovné barevné pruhy na časové ose.

### Ovládání

- **Range** - výběr časového rozsahu:
  - **Last 10s** (default) - zobrazí jen posledních 10 sekund.
  - **Last 30s** - posledních 30 sekund.
  - **All** - vše, co je v bufferu (může to být i 10+ minut).
- **Channels** - legenda barev (CH0, CH1, CH2, CH3 NOISE). Slouží pro
  orientaci, není to filtr.

### Vykreslení

- **Hlavní oblast** - tóny na svislé ose (MIDI pitch). Rozsah se
  **auto-fit** podle obsahu, takže nevidíte mrtvé oktávy nad ani pod
  reálným rozsahem hry.
- **Levý sloupec** - octave labels (`C2`, `C3`, ...) jako orientační
  body.
- **Noise lane** (`NS`) - úzký vodorovný pruh dole pro NOISE noty.
  Rozdělen na 4 sub-pruhy podle indexu kanálu, takže vidíte i překryvy.
- **Vertikální mřížka** - adaptivní časové dělení (po 1 s, 5 s nebo
  10 s podle hustoty), pravá hrana je značka "teď".
- **Aktivní nota** (= právě hraje) je vykreslena prodloužená do
  okamžiku "teď" s tenkým obrysem.

### Tooltip a barvy

Při najetí myší na bar uvidíte tooltip s detailem:

```
Channel 1 (chip 0)
Pitch: A4 (MIDI 69) +5 cents
Duration: 0.250 s
Velocity: 110
Volume changes: 4 (attn 2..9)
```

Pokud nota obsahuje tick marky uvnitř baru, jsou to právě tyto změny
hlasitosti. Řádek `Volume changes` se ukáže jen u not, kde k nějaké
změně skutečně došlo. Pokud nota přesáhla limit 32 bodů, řádek je
doplněn značkou `(overflowed)`.

Barvy odpovídají kanálům (CH0 modrá, CH1 oranžová, CH2 zelenožlutá,
CH3 NOISE růžová). V stereo režimu má pravý chip světlejší odstín
stejné palety.


## Export not

Toolbar nahoře obsahuje tlačítka:

- **Export MIDI...** - uloží noty do souboru `.mid` (Standard MIDI
  File, type 1).
- **Export CSV...** - uloží noty do tabulkového `.csv` souboru.
- **Tempo BPM** - metadata tempa zapsaná do MIDI souboru (40-300,
  default 120). Ovlivňuje notační mřížku v DAW, ne skutečnou rychlost
  přehrávání.

Tlačítka jsou neaktivní, dokud nejsou v bufferu žádné zachycené ani
aktivně hrající noty.

### MIDI export

- Formát: **Standard MIDI File, type 1**, rozlišení 480 PPQN.
- Pro každý PSG kanál vznikne **samostatný MIDI track**. V mono režimu
  je to 4 tracky (CH0-CH3), v stereo režimu 8 tracků (Left CH0-CH3,
  Right CH0-CH3) plus track 0 jako conductor (tempo + time signature
  4/4).
- **NOISE** noty se mapují na MIDI drum channel (kanál 10), pitch 38
  (Acoustic Snare).
- Hodnota Tempo BPM je pouze **metadata** zapsaná do MIDI souboru
  (default 120). Ovlivňuje notační mřížku v DAW (jak se zachycené noty
  rozloží do taktů), ne rychlost přehrávání - skutečné délky not v
  sekundách jsou zachovány absolutně.
- Změny hlasitosti uvnitř každé noty se zapisují jako MIDI **CC 7
  (Channel Volume)** mezi `note_on` a `note_off`. Hodnota CC 7 se
  počítá z aktuální Attenuation podle vzorce `round(127 * (15 - attn)
  / 15)`. Většina MIDI playerů a DAW tyto změny respektuje, takže
  průběžná dynamika noty (tremolo, decay rampa, polyfonie přes attn)
  zazní i při externím přehrávání.
- Výstup je hardwarově nezávislý - lze ho otevřít v libovolném MIDI
  playeru nebo importovat do DAW (Reaper, Cakewalk, MuseScore atd.)
  a tam dál editovat.

### CSV export

- Soubor je UTF-8 bez BOM, LF konce řádků, desetinný oddělovač `.`
  (locale-safe).
- První řádek je hlavička:
  ```
  time_s,channel,chip,pitch_midi,note_name,duration_s,velocity,cents,attn_changes
  ```
- Každý další řádek odpovídá jedné notě:
  - `time_s` - čas note on od začátku záznamu (3 desetinná místa).
  - `channel` - 0..3 (3 = NOISE).
  - `chip` - 0 = mono / levý, 1 = pravý.
  - `pitch_midi` - 0-127 pro TONE, `-1` pro NOISE.
  - `note_name` - např. `A4`, `C#5`, případně `NOISE`.
  - `duration_s` - délka noty v sekundách.
  - `velocity` - 0-127.
  - `cents` - odchylka od rovnoměrně temperované noty (-50 až +50).
  - `attn_changes` - počet změn hlasitosti zaznamenaných během noty
    (0 = nota měla konstantní hlasitost). Pokud nota přesáhla interní
    limit 32 bodů, údaj odpovídá uloženému počtu, ne skutečnosti.
- Lze přímo otevřít v tabulkovém procesoru a dále analyzovat (filtry,
  pivot tabulky, grafy).


## Tip - záznam soundtracku z hry

1. Spusťte emulátor a otevřete okno PSG Audio Scope (Alt+Shift+A).
2. Spusťte hru z níž chcete pořídit záznam.
3. Hrajte / nechte hudbu hrát. Piano roll průběžně narůstá.
4. Až máte dost materiálu, klikněte na **Export MIDI...**, případně
   předtím doladěte **Tempo BPM** podle vašeho odhadu - hodnota
   slouží jen jako metadata pro notační mřížku v DAW.
5. Vyberte cílový soubor a potvrďte.

Soubor pak otevřete v libovolném MIDI playeru (např. `vlc`,
`timidity`, `aplaymidi`, `Synthesia`, `MuseScore`...) a porovnejte se
zvukem z emulátoru. Skutečné délky not v sekundách jsou zachovány
absolutně, takže přehrávání tempo-nezávisle odpovídá originálu.


## Tip - hledání PCM samplů

Pokud hra používá 1-bit PCM přes rychlou modulaci Attenuation (např.
samplovaná řeč nebo bicí), v envelope grafu uvidíte velmi rychlé
oscilace mezi `Attenuation = 0` a `Attenuation = 15`. Note event
detector toto **interpretuje jako sérii velmi krátkých not** - v piano
roll uvidíte hustý "ovesný déšť" krátkých barů. To je správné chování,
pouze ne použitelné pro hudební transkripci.

V tomto případě je užitečnější se dívat přímo na envelope graf a
sledovat tvar amplitudové obálky než interpretovat výsledný piano
roll.


## Diagnostické logy

Na druhém řádku statusline jsou tři nezávislé checkboxy. Každý zapíná
samostatný log soubor v pracovním adresáři emulátoru. Indikátor
`(s:N e:N w:N)` ukazuje aktuální počet zapsaných řádků v jednotlivých
souborech.

| Checkbox | Co loguje | Frekvence |
|----------|-----------|-----------|
| **Log samples** | Stav PSG vyčtený zrcadlem v každém vykreslovacím ticku - polling diagnostika | cca 60 Hz |
| **Log events** | Detekované note_on / note_off události | per nota |
| **Log writes** | Každý zápis do registru PSG s časovým razítkem - autoritativní 1:1 záznam HW komunikace | per zápis |

Logy jsou textové (TSV), generované jako `psg_scope_samples_*.tsv`,
`psg_scope_events_*.tsv` a `psg_writes_*.tsv` se značkou data a času
v názvu. Lze je otevřít v textovém editoru, tabulkovém procesoru nebo
zpracovat skriptem.

### Formát log writes

Zápisový log je nejvhodnější pro **offline post-processing** - obsahuje
plnou autoritativní stopu komunikace s PSG. Začíná hlavičkou:

```
# PSG write log
# emulator_clock_hz=17734475
# stereo=1
# columns: pxclk_ticks	channel_mask	raw_byte_hex
```

Každý další řádek je oddělen tabulátorem a má tři sloupce:

- `pxclk_ticks` - absolutní čas v emulátorových ticích od startu
  emulace. Dělením `emulator_clock_hz` získáte sekundy.
- `channel_mask` - bitová maska určující, na který čip zápis šel.
  V mono režimu vždy `0x01`, v stereo režimu kombinace bitů pro levý
  a pravý čip.
- `raw_byte_hex` - 8bitová hodnota zapsaná do PSG v hexa zápisu (např.
  `0x8F` pro latch tone CH0 nebo `0x9A` pro attn CH0).

Z této stopy lze offline zrekonstruovat kompletní state machine PSG
(viz example tool níže).


## Offline konverze PSG write logu na MIDI

K dispozici je příklad Python skriptu pro post-processing zápisového
logu mimo emulátor: `docs/tools/example_psg_write_log_to_midi.py`.
Popis a kompletní seznam přepínačů viz `docs/tools/README.md`.

Skript:

- Načte TSV soubor `psg_writes_*.tsv` produkovaný checkboxem **Log
  writes**.
- Replikuje state machine PSG (latch CS, latch attn, per-channel
  divider / attn / noise) identicky s emulátorem.
- Detekuje noty a pitch ze sekvence zápisů do registrů, mimo běžící
  emulátor.
- Vyrobí výsledný `.mid` soubor.

Závislosti: Python 3 a balíček `mido` (`pip install mido`).

Použití:

```bash
python docs/tools/example_psg_write_log_to_midi.py psg_writes_20260523_143000.tsv -o soundtrack.mid
```

Skript je **ukázkový** - demonstruje, jak ze zápisového logu vyrobit
MIDI bez nutnosti modifikovat emulátor. Není to produkční nástroj,
ale dobrý odrazový můstek pro vlastní analytické skripty.


## Vztah k oknu PSG State

PSG State a PSG Audio Scope se doplňují:

| Okno | Co zobrazuje |
|------|--------------|
| **PSG State** | Aktuální obsah registrů, latch fáze, dekódovaný typ kanálu a frekvence |
| **PSG Audio Scope** | Časový průběh, historie hlasitosti, detekované noty, export |

Typický postup: PSG State otevřít, když řešíte konkrétní chybu zápisu
do PSG (špatný kanál, špatná hodnota). PSG Audio Scope otevřít, když
chcete pozorovat **co PSG hraje v delším horizontu** a případně z toho
něco získat (záznam, identifikace tempa, analýza zvukového efektu).
