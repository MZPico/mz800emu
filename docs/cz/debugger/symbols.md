# Symbols - mapování adresa <-> jméno

Symbolová databáze přiřazuje uživatelská jména konkrétním adresám v paměti
emulátoru a nabízí je v debugger UI (disasembler, breakpoint targety,
memory browser). Tento dokument popisuje storage, priority zdrojů,
podporované soubory a UI panel `Symbols`.

## Účel

Typické use case:

- **Disassembler labels** - `CALL 0xD8A2` se zobrazí jako `CALL wboot`,
  pokud je pro danou adresu zaregistrovaný symbol.
- **Breakpoint targety** - smart BP ukáže jméno místo adresy v BP listu
  (= "BP at print_char (0x4042)").
- **Memory browser tooltipy** - hover nad address ve výpisu paměti zobrazí
  symbolové jméno + komentář.
- **User labels (LBL)** - uživatel přidá vlastní jména a komentáře pro
  funkce / konstanty které se znova používají napříč session.
- **Import z assembleru** - načtení symbol exportu po `.noi`/`.map`/`.sym`
  formátu pro sledování běhu zdrojového projektu (sdldz80, sjasmplus, ...).

## Source priority

Symbol může mít více zdrojů. Při lookup po adrese se použije ten s nejvyšší
prioritou:

| Priorita | Source | Popis | Soubor |
|----------|--------|-------|--------|
| 3 (max)  | LBL    | User write-back (= ručně přidané labely + komentáře) | `.lbl` |
| 2        | MAP    | sdldz80 linker map (incl. module name jako hint) | `.map` |
| 1        | NOI    | SDCC NoICE export | `.noi` |
| 0 (min)  | SYM    | sjasmplus symbol export | `.sym` |

Vyšší priorita "vyhraje" při lookup po adrese. Lookup po jménu je
case-sensitive a vrací jediný záznam s daným jménem (= jméno je unikátní
v rámci storage).

**Auto-promote na LBL:**
- Nastavení komentáře pro symbol přepíše source na LBL (= write-back
  zachová user comment do `.lbl` souboru).
- Ruční přidání user labelu pro existující jméno overwrite záznamu na
  LBL prioritu.

## Filtrace per bank

Symbol má `bank_id` field. Při lookup po adrese:

- `bank_id == 0` = "CPU view" pohled, vrátí jen záznamy s `bank_id == 0`
  (= globální symbols viditelné nezávisle na MMEXT bank).
- `bank_id != 0` = vrátí záznam s odpovídajícím bank_id, fallback na
  `bank_id == 0` pokud žádný explicitní bank-specific symbol neexistuje.

Většina symbolů z importu (`.noi`/`.map`/`.sym`) má `bank_id = 0`, MMEXT
bank scope se používá hlavně pro user-defined LBL (= ručně rozlišené per
banking config).

## File formáty

### `.noi` (SDCC NoICE export)

Per řádek:
```
DEF <name> 0x<hex>
```

Příklad:
```
DEF wboot 0xD8A2
DEF print_char 0x4042
```

Komentáře nepodporovány.

### `.map` (sdldz80 linker map)

Parser čte sekce `Global Defined In Module`. Module name se extrahuje
jako optional comment hint (= "z modulu utils.rel"). Format je standardní
sdldz80 výstup.

### `.sym` (sjasmplus symbol export)

Per řádek:
```
<NAME> EQU <value>
```

Hodnota může být:
- `EQU $ABCD` (= sjasmplus hex prefix `$`)
- `EQU 0xABCD` (C-style hex)
- `EQU 12345` (decimal)

### `.lbl` (user write-back)

Format line-based:

```
# komentář (skip)
<addr_hex>  <name>  <bank>  ; per-symbol comment
```

Příklad:
```
4042  print_char  0  ; print one character to display
D8A2  wboot       0  ; warm boot entry from BIOS
```

Per-symbol comment je za znakem `;`.

Save `.lbl` serializuje **JEN** LBL záznamy (ne import-driven NOI/MAP/
SYM). User write-back persistuje user labels + comments mezi sezeními bez
duplicit s import zdroji.

### Auto-detect

Při načtení podle suffixu vybere parser:
- `.noi` -> NOI parser
- `.map` -> MAP parser
- `.sym` -> SYM parser
- `.lbl` -> LBL parser

Neznámý suffix se nenačte.

## UI panel `Symbols`

Otevírá se z menu Debugger -> Symbols. Default 820x420, min 760x300.
Window-level horizontal scrollbar pokud user zmenší pod min content.

### Layout

**Sticky header (2 řádky, vždy nahoře okna):**

1. `[+ Add]` toggle + Search:`[___]` + `[Clear]` + count `(visible/total symbols)`
2. `Selected: N` + bulk: `[Delete]` | file ops: `[Load From...]` `[Save .lbl As...]` `[Clear All]`

**Add Label collapsible block** (pod sticky header, jen pokud `[+ Add]` aktivní):

```
Addr (hex):[____]  Name:[____________]
Comment:[_______________________________]  [OK] [Cancel]
```

Při validation chybě zobrazí inline error pod blokem (červený text).

### Tabulka (7 sloupců)

| Sloupec | Edit | Význam |
|---------|------|--------|
| Sel     | klik checkbox | Bulk selection (per-row) |
| Name    | dvojklik = rename inline | Identifier |
| Addr    | dvojklik = relocate inline | `0x%04X` |
| Bank    | (read-only) | `bank_id` decimal |
| Source  | (read-only) | `LBL` / `MAP` / `NOI` / `SYM` |
| Comment | dvojklik = inline edit | User comment, auto-promote LBL |
| x       | klik = delete (no confirm) | Per-row delete |

**Tooltipy** přímo na hover prvků.

**Filter** (case-insensitive substring) matchne na **name** nebo
**comment** nebo **addr-hex string** (např. `"40"` najde `0x40FF`).

### Tristate select-all checkbox

Hlavička sloupce Sel je **tristate** select-all checkbox:

| Stav | Visual | Klik akce |
|------|--------|-----------|
| **none** (0 vybráno) | prázdný čtvereček | Vybere všechny visible |
| **all** (vše visible) | V-checkmark | Odznačí všechny visible |
| **some** (subset) | vyplněný menší čtvereček | Vybere všechny visible |

### Inline edit Name (rename)

Dvojklik na Name buňku -> InputText edit. Pravidla:

- Validace: písmena/číslice/`_`/`.`/`'`, žádný leading digit, max 63.
- Duplicate check: nový name nesmí kolidovat s existujícím.
- Pro **non-LBL source** (NOI/MAP/SYM): old symbol zůstane (= rename
  neumí smazat import-driven symbol). Nový LBL alias odkazuje na stejnou
  addr; user může chtít smazat starý ručně nebo restartovat emu (= NOI
  data se nepersistují bez explicit reimport).
- Apply: Enter / klik mimo. Esc = cancel.

### Inline edit Addr (relocate)

Dvojklik na Addr buňku -> InputText edit (hex characters only).
Pravidla:

- Parse hex ("4042" / "0x4042" / "#4042").
- Overwrite symbolu na novou addr (same name -> LBL promote).
- Apply: Enter / klik mimo. Esc = cancel.

### Inline edit Comment

Dvojklik na Comment buňku -> InputText. Při Enter / klik mimo se nastaví
komentář a symbol se **auto-promote** na LBL source (= další save_lbl
ho serializuje).

### Bulk Delete

`[Delete]` v sticky header bulk sekci -> confirm dialog `"Delete N
selected symbols?"`.

Bulk Delete úspěšný jen pro **LBL** source symbols. Non-LBL symbols z
import zůstávají.

### File ops

| Tlačítko | Akce |
|----------|------|
| `Load From...` | File dialog pro `.noi/.map/.sym/.lbl` (auto-detect podle suffixu) |
| `Save .lbl As...` | File dialog s `.lbl` filtrem + confirm overwrite |
| `Clear All` | Confirm dialog `"Wipe all N symbols (LBL + imported)?"` |

Status řádek pod sticky header zobrazí výsledek poslední operace
(`Loaded N symbols from path` / `Saved N LBL symbols to path` / chyba).

### Klávesové zkratky inline edit

| Klávesa | Akce |
|---------|------|
| Enter | Apply (= parse + uložit) |
| Esc | Cancel (= žádné změny) |
| Klik mimo | Apply |

Pokud Name / Addr parse selže, edit zůstává aktivní s červeným error
textem.

## Persistence

Symbolová databáze má auto-load a auto-save default `.lbl` souboru
symetricky k breakpoints lifecycle.

### Cfg sekce `[SYMBOLS]`

| Klíč | Typ | Default | Význam |
|------|-----|---------|--------|
| `lbl_file` | TEXT | per-arch `mz800.lbl` / `mz1500.lbl` / `mz700.lbl` (relativně k home cfg dir) | Cesta k default `.lbl` souboru pro auto-load a auto-save |
| `auto_load` | BOOL | 1 | Při startu zkusit načíst default `.lbl` (mlčky tolerantní k missing souboru = první start) |
| `auto_save` | BOOL | 1 | Při ukončení uložit LBL záznamy do default `.lbl` |

### UI Persist... popup

V toolbaru (vedle Load/Save/Clear All) je tlačítko **Persist...**, které
otevře popup s:

- **Auto Load on start** toggle (mění `auto_load` cfg)
- **Auto Save on exit** toggle (mění `auto_save` cfg)
- Display current default file path
- **Browse...** pro override default cesty
- **Load now** / **Save now** pro manual trigger nezávislý na auto-* flagách

Default behavior = plně automatický persist obousměrně. User může vypnout
přes Persist... popup nebo přímo INI editem.

### Co se persistuje

`.lbl` soubor uchovává **jen LBL záznamy** (= user-driven labels +
comments). Import-driven (NOI/MAP/SYM) symboly nutno znovu načíst
z původních zdrojových souborů.

## Use case příklady

### Import SDCC build artefaktů + user labels

```
1. Stáhnout sdcc kompilací: my_program.noi + my_program.map
2. V Symbols panelu [Load From...] -> my_program.noi (= NOI source)
3. [Load From...] -> my_program.map (= MAP overwrite NOI per priority)
4. Disassembler nyní zobrazuje MAP-priority symboly.
5. User dvojklik na Comment "main_loop" -> zadá "tick handler, called
   every 50Hz" -> auto-promote na LBL.
6. [Save .lbl As...] -> my_program.lbl (= jen user comments, ne MAP/NOI).
```

### Vlastní labely pro ROM rutiny

```
1. [+ Add] toggle.
2. Addr: 0xD8A2, Name: wboot, Comment: BIOS warm boot
3. [OK] -> LBL záznam.
4. Disassembler ukazuje "CALL wboot" místo "CALL 0xD8A2".
5. Při restartu emu - default `.lbl` se auto-loaduje a restoruje labels.
```

### Reorganizace LBL po refaktoringu

```
1. Memory map se změnila - move funkce z 0x4042 na 0x4500.
2. Dvojklik na Addr u "print_char" -> změna na 0x4500.
3. Symbol nyní mapuje 0x4500 -> print_char (= overwrite se stejným name
   na novou addr).
```

## Limity

- **Žádné multi-bank labely v import** - `.noi`/`.map`/`.sym` parsery
  ignorují bank info; user-driven `.lbl` může mít explicit `bank_id`.
- **Žádné sub-symbol struktury** - jen flat name -> addr mapping.
- **Rename non-LBL source nemůže smazat původní** (= import-driven
  symbol zůstává).
- **Žádný diff/merge mezi loadnutými soubory** - second `[Load From...]`
  prostě overwrite per priority. Pro merge workflow nutný explicit
  `[Clear All]` před.
