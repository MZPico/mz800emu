# Bookmarks - pojmenované adresové záložky

Okno **Bookmarks** umožňuje ukládat libovolné množství pojmenovaných
záložek na adresy (zadané hex literálem nebo jménem symbolu). Záložky
persistují přes restart emulátoru (per-arch JSON soubor) a klikatelně
přesměrují focus do libovolné disasm instance (#1 hlavní, #2-5
sekundární).

## Datový model

Záložka má tři vlastnosti:

- **id** - monotónní counter, perzistentní napříč session
- **user_input** - hex string (`0xE6CA`, `#1234`, `1234h`) nebo jméno
  symbolu (`MAIN_LOOP`); resolve probíhá dynamicky
- **comment** - nepovinný komentář

**Adresa je dynamická.** Storage drží jen `user_input` jako raw string;
při render UI se každý frame provádí resolve:

1. Pokus o parsing jako hex: `0x1234`, `0X1234`, `#1234`, `$1234`,
   `1234h`/`1234H`, holé hex `1234` (max 4 znaky, jen hex digits)
2. Fallback: lookup v symbol DB (case-sensitive)
3. Pokud i symbol selže, resolve `false` a řádek v UI je greyed disabled

**Důsledek:** symbol bookmark sleduje aktuální symbol DB - import nového
`.map` souboru se okamžitě promítne do Address sloupce i labelu.

## File format - JSON

```json
{
  "version": 1,
  "bookmarks": [
    {"id": 1, "input": "MAIN_LOOP", "comment": "hlavni vypocetni smycka"},
    {"id": 2, "input": "0xE6CA", "comment": ""},
    {"id": 3, "input": "$1234", "comment": "screen RAM"}
  ]
}
```

Auto-load probíhá při startu emulátoru, auto-save při ukončení, oboje
podle níže uvedených cfg klíčů.

Cfg sekce `[BOOKMARKS]`:

| Klíč | Typ | Default |
|------|-----|---------|
| `bookmarks_file` | string | per-arch (`mz800.bookmarks`, `mz1500.bookmarks`, `mz700.bookmarks`) |
| `bookmarks_auto_save` | bool | true |
| `bookmarks_auto_load` | bool | true |

## UI okno

Aktivace: top menu **Debugger -> Bookmarks**. Default closed. Pozice a
velikost okna se pamatuje přes `imgui.ini`.

### Layout

```
+------------------------------------------------------------+
| Bookmarks                                            [X]   |
+------------------------------------------------------------+
| Filter:[__________] [Clear]   3 bookmarks (2 filtered)     |
| Selected: 0  [Delete selected]                             |
| [Save] [Save As...] [Load...] [Merge...]                   |
+------------------------------------------------------------+
| Add                                                        |
|   Bookmark: [_______________________] [Insert PC]          |
|   Comment:  [_______________________] [Add]                |
+------------------------------------------------------------+
| [v] | [> ...] Label  | $address | Comment       | Del      |
| [ ] | [> ...] MAIN_..| $E6CA    | hlavni smycka | [x]      |
| [ ] | [> ...] 0x1234 | $1234    |               | [x]      |
| [ ] | (--unresolvable--) ----   | invalid sym   | [x]      |
+------------------------------------------------------------+
```

### Add form

Layout: 2 řádky, label + input + tlačítko:

- **Bookmark:** InputText (placeholder "address or symbol") + **[Insert PC]**
  (vloží aktuální `PC` jako `#XXXX` do entry)
- **Comment:** InputText + **[Add]**

Validace: prázdný bookmark input -> červený okraj + tooltip "Address or
symbol required" + Add tlačítko disabled. Po úspěšném Add se oba bufferů
vyčistí a fokus se vrátí na Bookmark entry.

### Tabulka

4 sloupce (+ select checkbox jako sloupec 0):

| # | Sloupec | Obsah | Šířka |
|---|---------|-------|-------|
| 0 | Sel | per-row checkbox + 3-state header (none/some/all) | fixed |
| 1 | Label | `[> ...]` button + plain text label | flex |
| 2 | $address | dynamický hex `$XXXX` nebo `----` | fixed |
| 3 | Comment | klikatelný (= edit on click) | flex |
| 4 | Del | `[x]` button | fixed |

**Sloupec Label** obsahuje:

- **`[> ...]` SmallButton:**
  - LMB klik -> focus v primární disasm instanci (Disassembly #1)
  - RMB klik -> popup "Show in Disassembly #1..#5"
  - Disabled pokud adresa není resolvable
  - Tooltip: "Focus in primary disassembly. Right-click for additional actions."
- **Plain text label** (= `user_input`, případně symbol jméno):
  - Hover -> underline + tooltip "Click to edit"
  - Klik -> přepne řádek do inline edit režimu
  - Greyed disabled pokud unresolvable

**Sloupec Comment:**

- Hover -> underline + tooltip "Click to edit"
- Klik -> přepne řádek do inline edit režimu
- Zobrazí `(no comment)` pokud je komentář prázdný

### Inline edit

Klik na Label nebo Comment přepne řádek do edit režimu:

- Sloupec Label -> InputText s aktuálním `user_input`
- Sloupec Comment -> InputText s aktuálním komentářem
- Sloupec Del -> nahrazen tlačítky `[v]` Apply + `[x]` Cancel

**Klávesy:**

- **Enter** v InputText -> Apply (uloží změny)
- **Escape** -> Cancel (zruší úpravy)
- Pokud uživatel stiskne Enter a Escape ve stejném frame, Apply má prioritu

### Filter

InputText "name, comment..." - case-insensitive substring match nad:

- `user_input` (raw vstup)
- resolved label (symbol jméno pokud `user_input` je hex)
- `comment`

Prázdný filter zobrazí všechny záložky.

### 3-state checkbox v header sloupce Sel

Vizualizuje stav selekce napříč viditelnými řádky (= po aplikaci filtru):

| State | Symbol | Akce na klik |
|-------|--------|--------------|
| **none** | prázdný čtvereček | select all visible |
| **some** | indeterminate (vyplněný kvadrát) | select all visible |
| **all** | checkmark | deselect all visible |

### File ops

Tlačítka v hlavičce okna spouští souborový dialog:

- **Save** - rychlý save do default file
- **Save As...** - file dialog, save na zvolenou cestu
- **Load...** - file dialog, replace mode (nahradí současný seznam)
- **Merge...** - file dialog, append mode (přidá záložky, deduplikace
  podle ID a stejného obsahu)

## Klikatelné chování `[> ...]` buttonu

**LMB klik**: focus v primární disasm instanci (Disassembly #1):

1. Pokud je hlavní debug okno zavřené, otevře se
2. Pokud `follow_pc=true` a emu běží, follow_pc se trvale vypne
3. Disasm se nasměruje na adresu záložky

**RMB klik** otevře popup s 5 položkami "Show in Disassembly #1..#5".
Pro sekundární okno se okno otevře, nastaví focus a (pokud emu běží)
trvale vypne Follow PC.

## Přidání záložky z Disassembly

V right-click context menu sekce Disassembled je položka **"Add to
bookmarks"**:

1. Pokud na adrese existuje symbol, použije se jméno symbolu jako
   `user_input`. Jinak se použije hex literál `#XXXX`.
2. Záložka se vloží s prázdným komentářem.
3. Okno Bookmarks se otevře, aby uživatel viděl přidanou záložku a
   případně doplnil komentář.

Stejná položka je dostupná v RMB popupu klikatelného PC v I/O Ports
History (Selected Event panel).

## Vazba na další panely

- **Disassembly** ([disassembly.md](disassembly.md)) - cíl LMB/RMB
  z `[> ...]` buttonu i položky "Add to bookmarks" v context menu.
- **Symbols** - resolve symbol jména na adresu probíhá přes symbol DB;
  import `.lbl`/`.map` se okamžitě promítne do tabulky bookmarks.
- **I/O Ports History** - RMB popup nad klikatelným PC nabízí "Add to
  bookmarks".

## Známá omezení

1. **Multi-select**: per-row checkbox + bulk delete v hlavičce. Není
   Ctrl/Shift range select ani Click-and-Drag pro range.
2. **Inline edit cancel přes Escape** je globální v rámci framu - Escape
   kdekoli zruší edit.
3. **Žádná klávesová zkratka** pro toggle Bookmarks okna - jen menu
   položka.
4. **Comment max length** je soft 256 znaků (UI buffer); storage
   akceptuje delší texty.
