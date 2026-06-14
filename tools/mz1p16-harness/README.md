# mz1p16-harness - kreslicí self-test plotteru MZ-1P16

Standalone nástroj, který spustí embeddovaný firmware plotteru Sharp MZ-1P16
(4 KB ROM interního Intel 8050) nad emulačním jádrem `mcs48` a modelem
mechaniky `mz1p16`, vyvolá **vestavěný kreslicí (drawing) self-test** a
vyexportuje výsledný plot do PNG.

Je to cesta jak rozkreslit pero **bez připojeného MZ-800** - vzor pochází
přímo z ROM plotteru.

## mz1p16_print - offline tisk captnutých dat

`mz1p16_print.exe <vstup.bin> [vystup.png] [maxper] [warmup]` vezme syrový
capture bajtů poslaných z MZ-800 na tiskárnu (soubor `printer-<timestamp>.bin`
z printer capture) a **předá ho reálnému firmwaru plotteru ke zpracování** -
offline, bez MZ-800. Firmware bajty interpretuje sám (znaky, příkazy plotteru,
barvy); harness zaznamená tahy pera a vyexportuje PNG.

```bash
tools/mz1p16-harness/mz1p16_print.exe captures/printer-XXXX.bin out/print.png
```

Pacing: po každém bajtu (data na P2 + puls /INT) se čeká na KLID mechaniky
(steppery stojí + pero nahoře po ~25000 krocích) = firmware bajt dokreslil.
BUSY (P1.4) se k pacingu nehodí - firmware v klidu drží BUSY=1, ready je jen
krátký puls (viz `knowledge/hw/mz1p16-offline-replay-busy-semantics.md`).
Ověřeno: vstup `ptest` + `list/p` + `print/p` se vykreslil věrně 1:1.

## Build (UCRT64)

```bash
export MSYSTEM=UCRT64
export PATH=/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH
tools/mz1p16-harness/build.sh
```

## Spuštění

```bash
# tools/mz1p16-harness/mz1p16_drawtest.exe [sekundy] [vystup.png]
tools/mz1p16-harness/mz1p16_drawtest.exe 90 ../out/drawing_selftest.png
```

- `sekundy` = simulovaná doba běhu self-testu (přepočet 1 s ~= 400000
  strojových cyklů 8050, viz níže). Delší běh = víc řádků vzoru.
- `vystup.png` = cesta k PNG (default `../out/drawing_selftest.png` vůči
  binárce, tj. `mz1p16-plotter/out/`).

## Jak se self-test vyvolá

Plotter má 3 čelní tlačítka, všechna aktivní v LOW. **Podržení PAPER FEED
(vstup T1) na několik sekund spustí kreslicí self-test.** Harness to
modeluje nastavením `cpu.T1 = 0` (PAPER FEED stisknuto) už od resetu.

Empiricky (tento harness, fáze 2c):
- Firmware nejdřív odhomuje vozík (open-loop k levému dorazu) a po
  ~2.15 mil. strojových cyklů (~5.4 s) **začne klást body**.
- Self-test naběhne i při stisku T1 *po* startupu (drženo >= ~2 s), ale
  T1=0 od resetu dává nejbohatší a nejstabilnější vzor.

## Co firmware kreslí

- Řádek napříč šířkou papíru (X ~61..530 kroků), rozdělený na **4 barevné
  segmenty** (pozorované pořadí: černá -> červená -> zelená -> modrá).
- Po dokončení řádku se posune papír (Y) a vozík se vrátí vlevo, kde
  pen-change mechanika pootočí buben na další barvu -> další řádek.
- Pero kreslí **tečkováním**: spustí pero na místě (nulový drag), zvedne,
  přejede jinam. Hustá řada teček tvoří souvislou čáru.

**Y stepper funguje** (posuv papíru oběma směry, horní nibble BUS) - to
tento harness poprvé prakticky prokázal (dřív se Y v testech nevyvolal).

## Render

Po sobě jdoucí pen-down body **stejné barvy** se spojí úsečkou (segment se
přeruší při změně barvy nebo skoku vozíku zpět vlevo = nový řádek). Navíc se
v každém bodě vykreslí tečka. Výstup je škálovaný na bounding box. PNG se
zapisuje vlastním minimálním writerem (deflate "stored" bloky), bez externí
knihovny.

## Přepočet času

MCS-48 strojový cyklus = 15 oscilátorových period; plotter běží na 6 MHz
krystalu (`knowledge/hw/mz1p16-8050-port-mapping.md`). 1 cyklus = 2.5 us,
1 s ~= 400000 cyklů. `[neověřeno přesně pro 8050; standard MCS-48 manuál]`.
