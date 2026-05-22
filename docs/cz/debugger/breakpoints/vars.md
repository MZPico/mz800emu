# Breakpoint - $vars (user variables)

User-defined skalární proměnné `$name` umožňují smart breakpointům držet
stav mezi hity (counter, state machine, manuální flag pro override
condition). Tento dokument popisuje storage, persistenci, action a
expression API a UI panel `Variables`.

Cross-references:
- Action `set $name op= expr` a `clear_vars`: `action-dsl.md`.
- Expression resolve `$name`: `expression-syntax.md`.
- Persist v `.bpt`: `persistence.md`.

## Účel

`$vars` jsou globální (per emulator session) signed 32-bit integer
proměnné. Typické use case:

- **State machine** - BP1 nastaví `$stateA = 1`, BP2 testuje
  `if $stateA == 1 then ...`.
- **Counter** - BP s action `set $count += 1` (= sledování počtu hitů).
- **Manual override** - user v UI nastaví `$dbg_pause = 1`, condition
  BP testuje `$dbg_pause` (= dynamicky přepínatelný BP).
- **Cross-BP koordinace** - BP1 zaznamená kontext do `$last_addr`,
  BP2 ho použije ve své podmínce.

Hodnota neexistující proměnné v expression je `0` (= no-op default).

## Identifier syntax

Platný identifier (po `$` prefixu):

```
^[a-zA-Z_][a-zA-Z0-9_]*$
```

- První znak: písmeno (a-z, A-Z) nebo underscore.
- Pokračování: písmena, číslice, underscore.
- Case-sensitive (`$count` != `$Count`).
- Maximální délka: 31 znaků.
- **Žádné reserved keywords** - `$` prefix je v action / expression
  jazyce sám o sobě distinktivní, takže `$if`, `$set`, `$log` jsou
  platná uživatelská jména.

Validace probíhá:
- V storage layer - defenzivní check.
- V action parseru - early error s popisem regex pravidla.
- V UI Add Var formuláři - feedback v inline error textu.

Příklady:

| Jméno          | Platné? |
|----------------|---------|
| `count`        | ano     |
| `count1`       | ano     |
| `_internal`    | ano     |
| `myStateA`     | ano     |
| `1count`       | NE (číslice na začátku) |
| `count-down`   | NE (pomlčka) |
| `count.x`      | NE (tečka) |
| `if`           | ano (žádné keywords) |
| (prázdný)      | NE      |

## Value type

- Signed 32-bit integer.
- Default 0 pro neexistující jméno (= žádný "undefined" stav).
- Přetečení při `+=` / `-=` / `*=` se chová per expression evaluator
  (= přetečení uvnitř signed 32-bit, viz `expression-syntax.md`).

## Comment field

Volitelný komentář (max 256 znaků) - poznámka pro uživatele. Zobrazuje
se v UI panelu Variables, persistuje se do `.bpt` / `.vars` souboru.
Nepoužívá se v parseru ani v expression evaluator.

## Persist value flag

Per-var boolean `persist_value`. Default `true`.

| Hodnota   | Persist save                              | Load chování           |
|-----------|--------------------------------------------|-------------------------|
| `true`    | name + value + comment + flag              | obnoví poslední value  |
| `false`   | name + comment + flag (bez value)          | value reset na 0        |

Use case rozdíl:

- `$frameCount` (= counter, mění se s každým hitem) - `persist_value=false`,
  na startu emu nedává smysl mít loadnutou starou hodnotu.
- `$gameStateConfig` (= user-set config volba) - `persist_value=true`,
  uchovat user volbu mezi sezeními.

Toggle z UI: checkbox v Persist sloupci tabulky. Default při `[+ Add]`
formuláři je `true` (= safe default, value se ukládá).

## Lifecycle

1. **Per-session storage** - žije od inicializace breakpoint subsystému
   do jeho ukončení.
2. **Persist v `.bpt`** - sekce `"vars"` v JSON, sdílené s BPs.
3. **Per-arch `.vars` separate file** - `mz800.vars` / `mz1500.vars` /
   `mz700.vars` v cfg dir. Auto-load při start (cfg `auto_load=1`),
   auto-save při exit (cfg `auto_save=1`).

Pořadí inicializace:

1. Inicializace storage (prázdné).
2. Load vars sekce z `.bpt` (pokud existuje).
3. Load `.vars` souboru per architekturu (přepíše vars z `.bpt`).

Standalone `.vars` přepíše vars načtené z `.bpt` (= explicit per-arch
separace má přednost). Pokud user chce jen `.bpt` sekci, nastaví
`auto_load=0` v sekci `[BP_VARS]`.

## API

### Action mini-DSL

```
set $name = expr
set $name += expr   (kompaktní, totéž jako `set $name = $name + expr`)
set $name -= expr
set $name *= expr
set $name /= expr
set $name <<= expr
set $name >>= expr
set $name &= expr
set $name |= expr
set $name ^= expr

clear_vars
```

Pravidla:
- `set` na neexistující jméno vytvoří nový záznam s `comment = NULL`,
  `persist_value = true`.
- `set` na existující jméno mění **jen value** (= comment a flag
  zůstanou beze změny).
- `clear_vars` vynuluje hodnoty všech existujících záznamů, záznamy
  zůstanou v storage (= state-machine reset).

Detaily compound operátorů: `action-dsl.md`.

### Expression resolve

```
$name
```

Vrátí hodnotu nebo 0 pokud `name` neexistuje. Žádný typed system - `$x`
je vždy signed 32-bit integer. Detaily: `expression-syntax.md`.

### Load mode

Při načítání `.vars` souboru lze zvolit mode:

- **REPLACE** - smaže storage a načte ze souboru.
- **MERGE OVERWRITE** - sloučí, při konfliktu jména vyhrává soubor.
- **MERGE SKIP** - sloučí, při konfliktu jména vyhrává existující záznam.

## File format `.vars`

JSON top-level objekt s klíčem `"vars"` (= subset `.bpt` schématu).

```json
{
  "vars": [
    {
      "name": "frame_count",
      "value": 1234,
      "comment": "frames since reset",
      "persist_value": true
    },
    {
      "name": "runtime_counter",
      "comment": "reset on each load",
      "persist_value": false
    }
  ]
}
```

Pravidla per-záznam:

- `name` - povinné, validní per regex výše.
- `value` - emit jen pokud `persist_value=true`. Při load + `persist_value=false`
  se klíč "value" v souboru ignoruje (warning na stderr).
- `comment` - emit jen pokud non-NULL & non-empty.
- `persist_value` - vždy emit (explicit flag).

Zpětná kompatibilita s minimalistickým schématem:

- Chybějící `comment` → `NULL`.
- Chybějící `persist_value` → `true`.

## File ops (UI panel)

| Tlačítko          | Akce                                                    |
|-------------------|---------------------------------------------------------|
| `Save`            | Save do default per-arch file (`mz800.vars` etc.)       |
| `Save As...`      | File dialog (extension `.vars`, confirm overwrite)      |
| `Load From...`    | File dialog → REPLACE confirm                          |
| `Merge...`        | File dialog → 3-volby (Overwrite all / Skip all / Cancel) |
| `Clear Values`    | Vynulovat hodnoty (záznamy zachovat) - confirm         |
| `Clear All`       | Smazat všechny záznamy - confirm                       |

Confirm dialogy:
- **Clear All** - "Delete all N variables?"
- **Clear Values** - "Reset all values to 0?"
- **Delete selected** (bulk) - "Delete N selected vars?"
- **Load From** - "Replace current N vars with file content?"
- **Merge OVERWRITE / SKIP / Cancel** - 3-volby pro konfliktní jména.

Per-arch default path:

| Build      | Filename       |
|------------|----------------|
| MZ-800     | `mz800.vars`   |
| MZ-1500    | `mz1500.vars`  |
| MZ-700     | `mz700.vars`   |

Cfg sekce `[BP_VARS]`:

| Klíč         | Default            | Význam                       |
|--------------|--------------------|------------------------------|
| `vars_file`  | `mz<N>.vars`       | Default cesta (rel. cfg dir) |
| `auto_save`  | `1`                | Save při exit emu            |
| `auto_load`  | `1`                | Load při start emu           |

## UI panel `Variables`

Otevírá se z menu Debugger → Variables (bez klávesové zkratky).
Default velikost 800x360, min 760x300. Window-level horizontal scrollbar
se objeví pokud user zmenší okno pod min content width (= scrolluje
sticky header + tabulku spolu, jeden scroll pro celé okno).

### Layout

**Sticky header** (2 řádky, vždy viditelné nahoře okna):

1. `[+ Add]` toggle + Filter input + Clear + count `(visible/total)`
2. `Selected: N` + bulk akce: Delete / Set 0 / +1 / -1 / **Set...** |
   File ops: Save / Save As... / Load From... / Merge... / Clear Values /
   Clear All

**Add Var form** (collapsible blok POD sticky header, NAD tabulkou) - jen
pokud user kliknul `[+ Add]`:

```
[Name:    ___________________________________]  [Value: ___] [Persist]
[Comment: ___________________________________]                     [OK] [Cancel]
```

Šířky **dynamicky** podle skutečného obsahu - lokalizační tolerance,
žádné hardcoded px. Name + Comment textentry mají zarovnané začátky a
roztahují se / stahují s velikostí okna.

### Tabulka (7 sloupců)

| Sloupec   | Edit                                | Význam                       |
|-----------|-------------------------------------|------------------------------|
| Sel       | click checkbox                      | Bulk selection (per-row)     |
| Name      | dvojklik = **rename** inline        | `$<name>`                    |
| Value     | dvojklik = inline IASM input        | Decimal display              |
| Hex       | dvojklik = inline IASM input        | `0x<HEX>` predfilled         |
| Persist   | click checkbox                      | Toggle persist_value flag    |
| Comment   | dvojklik = inline text input        | User comment (max 256)       |
| x         | klik = delete (no confirm)          | Per-row mazací tlačítko      |

**Tooltipy** přímo na tlačítkách + edit cells (= bez `(?)` markerů).
Tooltip pro Name: "Double-click to rename. Existing BPs with $oldname
references stay unchanged (no refactor)."

**Filter** (case-insensitive substring) matchne na **name nebo comment**.

### Tristate select-all checkbox

Hlavička sloupce Sel je **tristate** select-all checkbox:

| Stav             | Visual                              | Klik akce                                        |
|------------------|-------------------------------------|--------------------------------------------------|
| **none** (= 0 vybráno) | prázdný čtvereček             | Vybere všechny visible                           |
| **all** (= vše visible vybráno) | V-checkmark            | Odznačí všechny visible                          |
| **some** (= subset)    | vyplněný menší čtvereček      | Vybere všechny visible (= dotáhne na "all")      |

V-checkmark stroke používá identický vizuál jako standardní ImGui
checkbox v řádcích.

### Rename behavior (Name sloupec)

Double-click na `$<name>` → InputText edit. Pravidla:

- Validace: regex - parse error inline.
- Duplicate check: pokud nový název už existuje, error "Name already exists".
- **Bez refactor BPs**: existující BPs s `$oldname` v condition / action
  zůstanou se starým jménem (= broken reference, lookup vrátí 0). User
  musí ručně opravit BPs, nebo `set $oldname = ...` v action vytvoří
  nový záznam.
- Apply: snapshot value/comment/persist + unset starého jména + set
  nového jména + restore comment + restore persist.
- Selection set update: pokud starý byl selected, nový je též.

### Inline edit Hex sloupec

Double-click na `0x<HEX>` cell → InputText edit s předvyplněnou hodnotou
`"0x{value}"` (unsigned 32-bit hex pro správné zobrazení záporných).
Apply path **sdílený** s Value sloupcem - user může v Hex edit napsat
`42` nebo `#2A` nebo `%101010` - vše parsuje stejně přes IASM parser.

### Bulk Set... popup

Tlačítko `Set...` v sticky header bulk ops sekci → popup s text input:

- IASM parser (42 / 0x2A / #2A / %101010).
- Enter nebo OK = apply na všechny vybrané.
- Esc nebo Cancel = zavřít bez změn.
- Inline error pokud parse selže.

### Inline edit - klávesy

| Klávesa    | Akce                                  |
|------------|---------------------------------------|
| Enter      | Apply (parse + uložit)                |
| Esc        | Cancel (žádné změny)                  |
| Click mimo | Apply (= save by default)             |

Pokud Value/Hex/Name parse / validace selže, edit zůstává aktivní s
červeným error textem.

### IASM value parser

Vstup pro Value sloupec a Add Var formulář:

| Vstup        | Hodnota | Pozn. |
|--------------|---------|-------|
| `42`         | 42      | decimal |
| `-1`         | -1      | negative decimal |
| `0x2A`       | 42      | hex C-style |
| `#2A`        | 42      | hex Sharp/IASM |
| `%101010`    | 42      | binary |
| `abc`        | error   | non-numeric |
| (prázdné)    | 0       | (Add formulář; inline edit error) |

Whitespace tolerantní (leading + trailing). Hex / bin nepřijímají
znaménko (= bitová interpretace, sign nedává smysl).

## Validation rules summary

| Pravidlo                         | Místo kontroly                       |
|----------------------------------|--------------------------------------|
| Name regex                        | parser + storage + UI Add formulář   |
| Name unique                       | UI Add formulář (storage akceptuje upsert) |
| Value IASM format                 | UI inline edit + Add formulář        |
| Value range int32                 | IASM parser (overflow = error)        |
| Comment max 256 znaků             | UI InputText limit (storage tolerantní) |

## Příklady use case

### State machine - dvojice eventů

BP1 (PC=0x1000): `set $stateA = 1`
BP2 (PC=0x2000): `if $stateA == 1 then ; set $stateA = 0`

Tj. BP2 se trigger jen pokud BP1 právě prošel a vyresetuje stav.

### Counter pattern

BP (PC=0x1234): `set $hitCount += 1; if $hitCount > 100 then mark "hot"`

Counter `$hitCount` (= persist_value=false pro reset každý start),
mark akce po překročení prahu.

### Manual override z UI

Stálý BP s podmínkou `if $dbg_pause == 1`. User v UI panelu Variables
nastaví `$dbg_pause = 1` pro aktivaci, `0` pro deaktivaci. Žádné
restartování emu, žádné editování BP definice.

### Cross-BP kontext

BP1 (PC=0x1000): `set $last_a = a` (= snapshot registru A v okamžiku hitu)
BP2 (PC=0x2000): `if a != $last_a then log "A changed: %d -> %d", $last_a, a`

## Limity

- **Žádné nested struktury** - jen skalární signed 32-bit integer. Pro
  pole / struct se použije Memory Browser nebo `mem[addr]` v expression.
- **Žádný typed system** - int32 pro vše. Bool se reprezentuje jako 0/1.
- **Žádný expression v default value** - nově vytvořený var (přes Add
  formulář bez Value) má hodnotu 0. Custom default se nastaví v action
  BP s expression.
- **Žádné scope** - vše je globální per session.
- **Žádná race protection vs emulační vlákno** - UI vidí storage as-is,
  BP action executor mutuje storage z emulačního vlákna. Worst case:
  jeden frame zobrazuje stale hodnotu (= akceptovatelné pro debug UI).

## Související dokumenty

- `expression-syntax.md` - gramatika condition výrazů včetně `$vars`
- `action-dsl.md` - action DSL s `set` a `clear_vars`
- `persistence.md` - JSON schema pro `vars` sekci
