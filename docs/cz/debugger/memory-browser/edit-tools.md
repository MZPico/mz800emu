# Memory Browser - Edit tools

Souhrn editačních nástrojů Memory Browseru: režim editace, undo/redo,
Pattern fill, Annotations, Freeze bytes a vizualizace nedávno
editovaných bajtů. Všechny pracují přes hex view a toolbar Memory
Browser okna.


## 1. Režim editace (Edit mode)

Memory Browser má dva základní stavy - **prohlížecí** (default) a
**editační**. V editačním režimu hex view přijímá klávesnicový vstup
a změny se okamžitě zapisují přes backend `write_bytes` do paměti.

### Zapnutí / vypnutí

| Cesta | Co se stane |
|---|---|
| **Klávesa F2** | Toggle Edit mode pro aktivní instanci Memory Browseru |
| **Tlačítko Edit: OFF / ON** v 1. řádku toolbaru | Toggle (= ekvivalent F2) |
| **Klávesa Esc** | Vypne Edit mode |

Tlačítko **Edit** má v zapnutém stavu **červené pozadí** jako vizuální
varování, že vstup z klávesnice teď mění obsah paměti.

Pro **read-only regiony** (ROM, FLASH) je editace force-disabled -
tlačítko Edit je zašedlé a F2 nereaguje. Stejně tak pro **disconnected
regiony** (= region momentálně nenamapovaný v banking schématu).

### Dva sub-mody editace

V Edit mode existují dva pod-režimy podle toho, který sloupec hex view
je aktivní:

- **HEX sub-mode** - editace po nibblech v hex sloupci. Každý byte se
  edituje jako dvojice hex digitů; po druhém digitu se kurzor posune
  na další byte a write se commitne.
- **ASCII sub-mode** - raw 7-bit ASCII vstup v ASCII sloupci (0x20-0x7E),
  každý stisk klávesy = 1 byte zapsaný na aktuální pozici kurzoru. Pro
  speciální znaky (česká diakritika, Kanji, MZ CG-ROM glyfy) viz
  Char Inserter v [search](search.md).

Přepínání mezi sub-mody: klávesa **Tab**.


## 2. Recently edited badge (žluté podtržení)

Každý úspěšný edit byte přidá záznam do "recently edited" storage
**per instance** Memory Browseru. Editované byty se v hex view zobrazují
se **žlutým podtržením** pod hex digity i pod odpovídajícím ASCII glyfem.

### Storage parametry

- Max **1024 záznamů per instance** (`MB_EDITED_MAX_PER_INSTANCE`),
  FIFO - po překročení se nejstarší vyhazuje
- Každý záznam drží i `original_byte` = hodnota bytu **před prvním
  editem v aktuální session**

### Auto-unmark po návratu na original

Pokud následný edit (typing, fill, undo, redo, Char Inserter) vrátí
byte na uloženou `original_byte` hodnotu, záznam se automaticky odebere
a podtržení zmizí.

Pro multi-level undo to funguje korektně: posloupnost A -> B -> C ->
undo na B -> undo na A vrátí byte na A (= original), badge zmizí.

### Ruční / automatický clear

- **Tlačítko Clear edited** v 1. řádku toolbaru - manuální vymazání
  pro aktivní instanci
- **Region switch** - při přepnutí na jiný region se badges
  automaticky vymažou (= nová session)
- **mzarch switch / shutdown** - vymazání napříč všemi instancemi

### Vizuální styl

Tenký žlutý underline (`RGB 255, 220, 70`) pod hex bajtem i ASCII
glyfem. Záměrně tenký, aby nezakrýval čitelnost hex digitů.


## 3. Per-byte undo / redo

Každá změna paměti přes Memory Browser pushne pre-write snapshot do
**undo stacku**. Stacky jsou per region (klíč = `region_kind + sub_id`).

### Klávesy

| Klávesa | Akce |
|---|---|
| **Ctrl+Z** | Undo - obnoví předchozí stav (per aktivní region) |
| **Ctrl+Y** | Redo - znovu aplikuje právě undonenou změnu |

Stejné akce jsou dostupné v pravém-klik kontextovém menu hex view:
- **"Undo (X levels)"** - X = počet dostupných úrovní pro region
- **"Redo (Y levels)"** - Y = počet dostupných redo úrovní

V status řádku hex view se po operaci zobrazí "Undo: applied" nebo
"Undo: nothing to undo".

### Limity stacku

- **Max 10 úrovní per region** (`MB_UNDO_MAX_LEVELS`) - ring buffer,
  po překročení nejstarší záznam vypadne
- **Max 1 MB per snapshot** (`MB_UNDO_MAX_BYTES`) - pro fill operace
  větší než 1 MB se undo záznam nevytvoří a status hlásí
  "(undo unavailable - too large)"

### Které operace pushují do undo

- HEX typing (per byte = 1 push)
- ASCII typing (per byte = 1 push)
- Pattern fill (= 1 push pro celý range jako jeden snapshot)
- Char Inserter direct write

Standardní browser/editor behavior: **nový edit po sérii undo zahodí
redo stack**.


## 4. Pattern fill

Modální dialog pro hromadné vyplnění range bajtů jedním z 6 generátorů.

### Otevření

Pravý klik v hex view -> položka menu **"Fill at cursor..."**.

### Dialog Fill Memory

```
+-- Fill Memory ----------------------------+
| Region: User RAM                          |
| Offset: 0x1000   Length: 256              |
| Mode:                                     |
|   ( ) Zero (0x00)                         |
|   ( ) Fill (0xFF)                         |
|   ( ) Ramp (0x00 + i)                     |
|   ( ) Random (seed = ___)                 |
|   ( ) Pattern (string AA BB CC)           |
|   ( ) Increment (start = 0x__)            |
| Pattern: [_________________________]      |
|                                           |
|         [Apply]   [Cancel]                |
+-------------------------------------------+
```

### 6 fill módů

| Mód | Význam |
|---|---|
| **Zero** | Vyplní všemi 0x00 |
| **Fill 0xFF** | Vyplní všemi 0xFF |
| **Ramp** | i-tý byte = `i mod 256` (0, 1, 2, ..., 255, 0, 1, ...) |
| **Random** | Pseudo-random (seedovaný xorshift32; seed = 0 znamená "derive z času") |
| **Pattern** | Opakovaný hex pattern z user stringu (max 64 bajtů per pattern, `MB_FILL_PATTERN_MAX_BYTES`) |
| **Increment** | i-tý byte = `(start + i) mod 256` |

### Pattern parser

Mód **Pattern** přijímá hex string ve formátu typu `"AA BB CC dd"` -
2 hex digity per byte, oddělovače `space` / čárka / žádný (= páry typu
`"AABBCC"`). Whitespace je ignorován. Při syntax chybě (lichý počet
hex digitů, neplatný znak) dialog vrací chybu.

### Workflow Apply

Při kliknutí **Apply**:

1. Backend načte aktuální obsah range do tmp bufferu (pre-fill snapshot)
2. Snapshot se pushne do undo stacku pro aktivní region
3. Backend zavolá `write_bytes` s vygenerovanými daty
4. Range se označí jako "recently edited", **originals = pre-fill bajty**
   (aby auto-unmark fungoval správně po undo)

V status řádku: `"Fill: %u bytes written, undo level available"`.


## 5. Annotations s color tag

Per-byte textový komentář + RGBA color tag. Storage v in-memory poli
(max **1024 záznamů**, `MB_ANNOT_MAX_ENTRIES`). Perzistence do souboru
`membrowser_annotations.txt` v pracovním adresáři - load při startu,
save při shutdown.

### Přidání / editace

Pravý klik v hex view -> položka menu:
- **"Add annotation at cursor..."** - pokud na pozici žádná annotace
  není
- **"Edit annotation at cursor..."** - pokud již existuje

### Dialog

```
+-- Annotation @ 0xC100 ---------------------+
| Color: [#] (color picker)                  |
| Text:                                      |
| [________________________________________] |
| [________________________________________] |
|                                            |
|         [Save]  [Delete]  [Cancel]         |
+--------------------------------------------+
```

### Limity textu

- Max **256 znaků** UTF-8 (`MB_ANNOT_TEXT_MAX`) per annotace
- Pro delší poznámky použij CDL Notes nebo externí dokumentaci

### Color tag

RGBA picker s několika přednastavenými svatchemi. Hodnota `0` (= plně
transparentní) znamená "neutrální default" - žádné barevné zvýraznění.

### Hover tooltip

Hover myší nad bytem s annotací v HEX nebo ASCII sloupci zobrazí ImGui
tooltip s color swatch (12x12), adresou a textem annotace.

### Indikátor v status řádku

Pokud byte pod kurzorem má annotaci, ve spodní bar liště se zobrazí
swatch barvy + text annotace.

### Formát perzistence

Soubor `membrowser_annotations.txt`, 1 annotace per řádek, pole oddělená
tabulátorem:

```
kind\tsub_id\taddr\tcolor_rgba8\ttext\n
```

Escape sekvence v textu:
- `\\n` -> newline
- `\\t` -> tab
- `\\\\` -> literal backslash

Při syntax chybě je řádek tiše přeskočen; validní zbytek souboru se
načte.


## 6. Freeze bytes (cheat lock)

Per (region, offset) záznam + uložená **hodnota**. Emulátor v každém
frame přepíše tyto byty zpět na uloženou hodnotu (= "lock byte" proti
přepsání běžícím programem).

### Limity

- **Max 256 současně zafrozených bajtů**
- Pokud žádný byte není zafrozený, freeze nemá runtime overhead

### Freeze byte

Pravý klik v hex view -> **"Freeze byte at cursor (= 0xXX)"** kde `XX`
je aktuální hodnota bytu, která se zafreezuje.

Vrstva **Frozen bytes** v Layers panelu (ON) vykreslí na zafrozených
bajtech **tmavě fialové BG** jako vizuální indikaci.

### Unfreeze

Pravý klik na zafrozeném bytu -> **"Unfreeze byte at cursor"**.

### Banking-aware apply

Region je resolvován dynamicky při každém apply. Pokud je region
**disconnected** uprostřed session (např. Memext/Ramdisk odpojen),
entry se v daném frame skipne (= soft tolerance). Po znovupřipojení
regionu freeze opět funguje.

### Persistence

Freeze záznamy jsou jen runtime - po restartu emulátoru se ztratí.

### Příklad použití: cheat HP lock

1. Najít HP byte v game RAM (search nebo manual scroll)
2. Pravý klik -> **"Freeze byte at cursor (= 0x64)"** (= 100 HP)
3. Hra dál bere damage interně, ale viditelné HP zůstane 100
4. Visual badge zapnout přes Layers panel -> **Frozen** ON


## 7. Save / Load BIN

Tlačítka **Load BIN** a **Save BIN** v 1. řádku toolbaru.

### Save BIN

Dialog:

```
+-- Save region as BIN -----+
| From: 0x0000              |
| Size: 65536               |
| [x] Whole region          |
| [ ] Offset from cursor    |
|                           |
| File: [_______________]   |
|        [Save]  [Cancel]   |
+---------------------------+
```

Disabled pokud je region prázdný nebo disconnected.

### Load BIN

Načte binární soubor a zapíše do regionu od offsetu 0 (nebo od pozice
kurzoru, podle volby). Disabled pokud:
- Edit mode je vypnutý
- Region je read-only (ROM, FLASH)


## 8. Char Inserter pro speciální znaky

Pro vkládání znaků, které nelze typovat přímo z klávesnice (česká
diakritika v Raw encoding, Kanji, MZ CG-ROM glyfy), použij okno
**Char Inserter** - dostupné přes položku "Insert character..." v
kontextovém menu hex view nebo přes Menu Debugger.

Detailní popis: [search](search.md).


## 9. ASCII edit dispatch (V1.5+)

V Edit mode + ASCII sub-mode hex view přijímá raw printable 7-bit ASCII
(0x20..0x7E) přímo z klávesnice. Pro českou diakritiku a extended
znaky je input silent skip - pro tyto znaky použij Char Inserter.


## 10. Souhrn kláves Edit tools

| Klávesa | Akce |
|---------|------|
| **F2** | Toggle Edit mode |
| **Tab** | Přepnutí HEX <-> ASCII sub-mode |
| **Esc** | Vypnutí Edit mode |
| **Ctrl+Z** | Undo (per region) |
| **Ctrl+Y** | Redo (per region) |


## 11. Související

- [hex-view](hex-view.md) - core hex viewer, edit dispatch, kurzor
- [search](search.md) - search engine a Char Inserter
- [layers-regions](layers-regions.md) - Frozen layer a region switching
- [diff-pcg](diff-pcg.md) - Memory Diff a PCG editor
