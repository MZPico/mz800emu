# MAX SPEED benchmark

Měření efektivity emulace v režimu MAX SPEED.

## K čemu slouží

V normálním režimu běží emulátor rychlostí odpovídající skutečnému počítači
(100 %). V režimu MAX SPEED běží tak rychle, jak hostitelský počítač zvládne.
Benchmark měří, jak je emulace efektivní, tj. kolik "času skutečného MZ-800"
se stihne vykonat za jednu reálnou sekundu.

100 % odpovídá rychlosti skutečného hardwaru (50 obrazovek za sekundu pro
MZ-800). Hodnota nad 100 % udává, kolikanásobně rychleji než reálný stroj
emulace běží (například 5000 % = padesátkrát rychleji). Výsledek se mění
podle toho, co emulace zrovna vykonává (režim MZ-700 / MZ-800, zda hraje PSG,
jak často se mění obsah obrazovky). Na skutečném hardwaru tyto věci rychlost
neovlivňují, počítač vždy běží na 100 %.

Měření je aktivní pouze v MAX SPEED. V normálním režimu, při pauze nebo během
blokujících operací (například načítání přes CMT-hack) se neměří, takže tyto
úseky výsledek nezkreslují.

## Okno benchmarku

Okno otevřete přes menu **Emulator -> MAX SPEED Benchmark...**. Zobrazuje živé
hodnoty aktualizované za běhu.

| Hodnota | Význam |
|---------|--------|
| Measured time | Reálný čas strávený v MAX SPEED od posledního resetu. |
| Emulated pxCLK | Počet emulovaných pixelových taktů (pxCLK). |
| Throughput | Emulované pxCLK za jednu reálnou sekundu. |
| Efficiency | Efektivita v % (100 % = rychlost reálného hardwaru). |
| FB-FPS | Počet vykreslených video snímků za reálnou sekundu. |
| Distribuce | Rozptyl okamžité efektivity (min / max / průměr / medián / směrodatná odchylka), vzorkováno jednou za sekundu. Ukazuje, jak stabilní nebo kolísavá rychlost byla. |

Tlačítka:

- **Reset** - vynuluje měření a začne nové od aktuálního okamžiku.
- **Print to console** - vypíše aktuální report na konzoli.

## Klávesové zkratky

| Klávesa | Akce |
|---------|------|
| `Alt + T` | Vypsat report benchmarku na konzoli |
| `Alt + Shift + T` | Resetovat benchmark |

## Měření z příkazové řádky (headless)

Volba `--maxspeed-bench` spustí emulátor rovnou v MAX SPEED a nechá periodicky
(každých 5 sekund) vypisovat report na konzoli. Hodí se pro automatizované
měření bez GUI. Kombinujte s `--headless` a `--run-mzf`:

```
mz800emu --headless --maxspeed-bench --run-mzf program.mzf
```

## Jak číst výsledky

- Kumulativní hodnoty (Efficiency, Throughput) jsou průměr přes celý běh od
  resetu. Integrují přes čas, takže jsou stabilní.
- Distribuce ukazuje, jak efektivita kolísala v čase. Úzký rozsah (malá
  směrodatná odchylka) znamená stabilní zátěž, široký rozsah kolísavou.
- Chcete-li porovnat dvě verze nebo konfigurace, měřte na nezatíženém počítači
  a zprůměrujte víc běhů. Výsledek mezi jednotlivými běhy ovlivňuje stav
  hostitele (ostatní spuštěné procesy, frekvenční škálování CPU), což může
  být víc než samotný měřený rozdíl.
