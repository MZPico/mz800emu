# Breakpoint - akce při triggeru (Action mini-DSL)

Akce breakpointu je sekvence příkazů, které se vykonají když BP střelí
(= `condition` vrátila true a hit/skip count proběhly). V Edit BP
dialogu se akce vybírá radio buttonem Mode:

| Mode | Působení | Action string |
|------|----------|---------------|
| Stop | Klasický BP - zastaví emulátor | (prázdná akce, NULL) |
| Log only | Vypíše do TTY a pokračuje | `log "BP <jméno>"` |
| Custom | Plný mini-DSL skript | uživatelův text |

Tento dokument popisuje syntaxi mini-DSL pro Custom mode v breakpoint systému.

## Sémantika "stop vs continue"

Action mini-DSL nemá explicit `stop` příkaz. Rozhodnutí o zastavení emulátoru se
řídí pouze podle toho, zda action string je prázdný:

- **action == NULL nebo whitespace-only** -> emulátor zastaví
  (= klasický BP stop).
- **action s libovolným non-empty obsahem** -> po vyhodnocení všech
  příkazů emulátor pokračuje (= implicit continue).

Důsledek: pokud chcete BP, který logne a pak zastaví, **není možné**
to vyjádřit přímo (vyžadovalo by `stop`). Workaround pro stop-on-N
je inkrement `$hits` a externí Stop BP, nebo Stop mode bez Custom
skriptu.

## Základní pravidla

- Příkazy se oddělují středníkem `;` nebo novým řádkem.
- Komentář od `#` do konce fyzického řádku. `#` uvnitř `"..."` se
  nepovažuje za komentář.
- Whitespace (mimo stringy) je ignorovaný.
- Identifikátory jsou case-insensitive (= konzistentní s expression
  evaluatorem).
- Argumenty výrazového typu jsou parsovány stejnou gramatikou jako
  condition - viz `expression-syntax.md`.
- Parser je striktní: neznámý příkaz, špatná arita nebo neukončený
  string literál = chyba; BP s neparsovatelnou akcí v UI signalizuje
  inline error pod textboxem a runtime se chová jako prázdná akce
  (= stop fallback).

## Příkazy

Action mini-DSL poskytuje 19 příkazů: 11 základních (log, continue,
disable_self, enable, disable, poke, set, mark, přiřazení `$name`,
clear_vars, if) a 8 forward příkazů pro řízení záznamu (cdl_start, cdl_stop,
cdl_reset, cdl_export, trace_start, trace_stop, trace_save, snapshot - viz
sekce "Forward příkazy" níže).

### `log "fmt"[, expr...]`

Vypíše formátovanou zprávu do stdout (`[BP-LOG] ...\n`). Format string
je první argument, je to string literal v dvojitých uvozovkách s
escape sekvencemi `\n`, `\t`, `\"`, `\\`.

| Spec | Význam |
|------|--------|
| `%X` | hex uppercase (`%04X` apod. **NEpodporováno** - jen holé `%X`) |
| `%x` | hex lowercase |
| `%d` | signed decimal |
| `%u` | unsigned decimal (32-bit) |
| `%b` | binary (= bez leading zeros, "0" pro nulu) |
| `%c` | znak (low byte hodnoty jako ASCII) |
| `%s` | arg = expression (adresa); lookup v sym_db, vypiše `name` symbolu nebo placeholder `<no-symbol-at-0xADDR>` |
| `%%` | literál `%` |

Důležité: format width / padding (`%04X`, `%8d`) parser **nepodporuje** -
spec je vždy jednoznakový. Pokud chcete zero-padded hex, vytvořte si
fixed-width výpis externě (nebo více `log` volání).

Maximum 8 argumentů.

```
log "PC=%X A=%X HL=%X", PC, A, HL
log "frame=%d cycle=%d", Frame, Cycle
log "flags: C=%d Z=%d S=%d", Cf, Zf, Sf
log "JP target=%X (%s)", HL, HL          # %s lookup v sym_db
log "ret to %X (%s)", {SP}, {SP}         # %s s 16-bit memory deref
```

Sémantika `%s`:

- arg vyhodnocen jako 16-bit adresa (low 16 bits, zbytek se ignoruje)
- lookup v globálních symbolech ("CPU view", viz `../symbols.md`)
- pokud najde symbol, vypíše jeho `name` (= identifier z .lbl/.map/.noi/.sym)
- pokud nenajde, vypíše placeholder `<no-symbol-at-0xADDR>` (4 hex digits)

Bank-specific lookup není v action DSL exposed.

Výstup jde na stdout.

### `continue`

Marker bez side-effectu. Default je už continue (viz "Sémantika
stop vs continue" výše), takže `continue` slouží jen jako čitelný
self-document pro reader skriptu.

```
continue
```

### `disable_self`

Vypne BP, který akci spustil (= one-shot pattern).

Pokud je akce vyhodnocována mimo enforcement (= Test Eval, manual
trigger), příkaz vypíše warning na stderr a je no-op.

```
disable_self
```

### `enable <name>`

Aktivuje BP s daným jménem (= nastaví `enabled = true`). Jméno se
hledá case-sensitive lineárním scanem podle `name` breakpointu.

```
enable trace_isr
```

Pokud BP neexistuje, příkaz vypíše warning na stderr a je no-op.
Argument není v uvozovkách - jméno se bere jako zbytek řádku po
keyword + whitespace.

### `disable <name>`

Deaktivuje BP s daným jménem (= `enabled = false`). Stejná lookup
sémantika jako `enable`.

```
disable trace_isr
```

Argument je povinný; bez něj parser hlásí chybu "disable: missing
target name".

### `poke <addr> <value>`

Zápis 1 bajtu do paměti. Oba argumenty jsou výrazy oddělené whitespace
(= ne čárkou). Split najde první whitespace mimo závorky / brackets /
strings.

```
poke 0x8000 0x42
poke HL A
poke 0xE000 + Y A & 0x0F
```

`value` se ořeže na low byte (`& 0xFF`), `addr` na 16-bit.

### `set <reg> <value>`

Zápis hodnoty do Z80 registru. `reg` je identifikátor (alpha + číslice +
`_`), case-insensitive. `value` je výraz.

Podporované registry:

- 8-bit: `A`, `F`, `B`, `C`, `D`, `E`, `H`, `L`
- 16-bit: `AF`, `BC`, `DE`, `HL`, `IX`, `IY`, `SP`, `PC`
- 8-bit půlené index registry: `IXH`, `IXL`, `IYH`, `IYL`
- shadow páry (16-bit): `AF'`, `BC'`, `DE'`, `HL'`
  (apostrof součástí jména, parser ho povoluje na konci identifikátoru)
- speciální: `I`, `R`, `IM` (`& 3`), `IFF1` (`& 1`), `IFF2` (`& 1`)

```
set A 0x42
set HL 0x8000
set PC 0x100  # vynucený jump
set IFF1 1    # povolení interruptů
set BC' 0x1234  # zápis do shadow BC'
set IXH 0xFF  # high byte IX, low byte zachován
```

Mimo enforcement (bez CPU kontextu) vypíše warning a je no-op.

### `mark "name"`

Marker do trace-suite + volitelný stdout výpis. `name` je string literal
v dvojitých uvozovkách s escape sekvencemi (jako `log` fmt).

```
mark "ISR enter"
mark "frame boundary"
```

**Dvouvětvové chování:**

Při fire BP s `mark "name"` action se vykoná **každá z následujících
větví nezávisle** podle konfigurace:

1. **Marklog binární záznam** - pokud `[TRACE_MARKLOG] mode` je
   `WITH_WINDOW` nebo `ALWAYS` (a writer je aktivní), zapíše se 24 B
   záznam do `<dir>/<name>.NNN.bin` s pre-resolved `marker_id`.
   Mapování id -> name je v `meta.json`.
2. **Stdout printout** `[BP-MARK] <name>` - pokud
   `[TRACE_MARKLOG].stdout_enabled` je `1` (default).

Default chování bez konfigurace = jen stdout.

**Marker name limity:**

- Max 63 chars (+ NUL terminator = 64 B). Delší se truncne při parse +
  stderr warning.
- Max 65535 unikátních markerů per session. Overflow = stderr warning,
  marklog část no-op pro tu BP action (stdout pokud cfg ANO funguje).

**Detaily formátu:** `docs/cz/debugger/formats/MARK-log_format.md`

Marker ID se registruje při parse BP. Hot path fire pak nevolá žádné
string operace - zapisuje 24 B record z pre-resolved id.

### `$name = <expr>`, `$name <op>= <expr>`

Přiřazení uživatelské proměnné. `$name` je identifikátor validovaný
regex `^[a-zA-Z_][a-zA-Z0-9_]*$` (= první znak alfa nebo `_`,
pokračování alfa / číslice / `_`), max 31 znaků. Žádné reserved
keywords (= `$` prefix je sám distinktivní). Lifetime proměnných je
per-emulator-session, sdílí se napříč všemi BP a perzistují do `.bpt`
JSON sekce `vars` plus per-arch standalone `.vars` souboru. Detail
viz `vars.md`.

Operátory:

| Op | Sémantika |
|----|-----------|
| `=` | prosté přiřazení |
| `+=` | `cur + rhs` |
| `-=` | `cur - rhs` |
| `*=` | `cur * rhs` |
| `/=` | `cur / rhs` (dělení nulou -> 0) |
| `<<=` | shift left (rhs maskováno na 5 bitů) |
| `>>=` | shift right |
| `&=` | bitwise AND |
| `\|=` | bitwise OR |
| `^=` | bitwise XOR |

Pokud `$name` ještě neexistuje, current value je 0 (= konzistentní s
read-side default). Compound operátor pak pracuje s 0 jako levou
stranou.

```
$hits += 1
$mask = 0xFF
$last_pc = PC
$accumulator |= 1 << bit_pos
```

Single `=` v evaluatoru je odmítnutý (= condition expression nemá
přiřazení); v action DSL je naopak přiřazení jediný platný význam.

### `clear_vars`

Vynuluje hodnoty všech existujících `$name` proměnných (záznamy
**zůstávají** v storage, comment + persist_value flag se zachovají).
Sémantika "state machine reset" - UI panel pak vidí stejné
identifikátory s hodnotami 0.

Není totéž jako smazání záznamů - pro úplné odstranění proměnné
slouží UI tlačítko `x` v řádku tabulky nebo bulk "Delete" tlačítko +
výběr.

```
clear_vars
```

### `if <expr> then <stmt> [else <stmt>]`

Jednořádkový conditional. Po vyhodnocení `expr` (= výraz dle
`expression-syntax.md`), pokud je nenulový, vykoná se `<stmt>` v
then-body. Pokud je nulový a je přítomna `else` větev, vykoná se
`<stmt>` v else-body.

`else` je volitelný. Žádný blok více příkazů, žádné nested
`if/then/if/then` blokově (technicky parser nezakazuje, ale není
testováno).

Pro vícepříkazový body použijte víc samostatných `if` se stejnou
condition, případně `$flag` proměnnou:

```
if A == 0x42 then log "match A=0x42"
if A == 0x42 then $matched = 1
```

Keywords `then` a `else` musí být celé slovo (= obklopené whitespace
nebo na konci řetězce), na top-level depth (= ne uvnitř `()` / `[]` /
`{}` / stringu).

```
if PC >= 0x1000 && PC < 0x2000 then log "in code seg PC=%X", PC
if $hits >= 5 then disable_self
if HL == 0 then mark "HL underflow"
if A == 0 then log "zero" else log "nonzero=%X", A
if $state == 1 then enable trace_bp else disable trace_bp
```

## Forward příkazy (řízení záznamu z BP)

Forward příkazy umožní akci breakpointu spustit záznamové operace
automaticky při hitu BP - stejný efekt jako odpovídající MCP / GUI
volání, ale bez ručního zásahu. Cíl: jeden chytře umístěný breakpoint
místo desítek ručních příkazů. Při hitu se provedou synchronně.

### `cdl_start`, `cdl_stop`, `cdl_reset`

Lifecycle Code/Data Loggeru (Memory Heatmap). Bez argumentů. `cdl_start`
zapne recording, `cdl_stop` vypne, `cdl_reset` vynuluje countery.

```
cdl_start
cdl_reset
```

### `cdl_export "fmt"[, args...]`

Export CDL dat do souboru. `fmt` je šablona názvu souboru se stejnými
specifikátory a argumenty jako `log` (= `%X`, `%d`, `$proměnné`, ...).

```
cdl_export "cdl-dump.json"
cdl_export "cdl-%d.json", $id
```

### `trace_start <kanál>`, `trace_stop <kanál>`

Start / stop binárního trace kanálu trace-suite. `<kanál>` je jeden z
`cputrack` / `iorqlog` / `intlog` / `hwlog` (case-sensitive, bez
uvozovek).

```
trace_start cputrack
trace_stop cputrack
```

### `trace_save <kanál>, "fmt"[, args...]`

Uloží / přesměruje segment trace kanálu. Za názvem kanálu následuje
čárka a šablona názvu souboru (stejná jako `log` fmt + args).

```
trace_save cputrack, "seg-%d.bin", $id
```

### `snapshot "fmt"[, args...]`

Uloží .mzs snapshot. `fmt` je šablona názvu souboru (jako `log` fmt +
args). Funguje i z pokračujícího BP (neprázdná akce = continue): akce běží
na emu vlákně mezi instrukcemi (safe-point), takže snapshot se uloží i bez
ručního zastavení emulátoru. Díky tomu jde trace segment i snapshot vzít
z jednoho pokračujícího BP.

```
snapshot "snap.mzs"
snapshot "snap-%d.mzs", $id
```

Všechny šablony názvu (`cdl_export` / `trace_save` / `snapshot`) sdílejí
stejný renderer jako `log`, takže lze generovat číslované soubory pomocí
`$proměnné`, např.:

```
snapshot "snap-%d.mzs", $id
$id += 1
```

Detaily MCP ekvivalentů viz [Přehled MCP tools](../../mcp-server/tools-overview.md),
konfigurace trace kanálů viz [Trace Suite](../Trace_Suite.md).

### Ochrana před zahlcením (rate-limit + saturace)

Těžké forward akce (`snapshot`, `trace_save`) zapisují na disk. Aby
breakpoint, který fajruje velmi často, nezahltil emulátor ani disk,
mají implicitní ochranu:

- **Rate-limit.** Mezi dvěma těžkými akcemi téhož BP platí minimální
  prodleva; rychlejší opakování se tiše přeskočí. Volitelně lze také
  nastavit tvrdý strop počtu uložení za session - po jeho dosažení se
  BP sám zakáže. Tyto limity jdou nastavit per-BP (přes MCP / GUI),
  jinak platí globální default z konfigurace.
- **Byte saturace.** Při překročení kumulativního objemu zapsaných dat
  z forward akcí emulátor sám zapauzuje a vydá varování s důvodem.
- I když pokračující BP spustí těžkou akci, příkazy pauza / stop /
  vyčištění breakpointů zaberou vždy - emulátor se nezasekne.

## Kompletní příklady

### Trace bez zastavení (= classic Log only ekvivalent)

```
log "PC=%X HL=%X A=%X", PC, HL, A
```

### Hit counter s one-shot disable

```
$hits += 1
log "hit #%d at PC=%X", $hits, PC
if $hits >= 5 then disable_self
```

### Conditional poke (= patch ROM call výsledek)

```
if A == 0xFF then poke HL 0x00
if A == 0xFF then log "patched HL=%X", HL
```

### Cross-BP enable / disable (= state machine)

BP "isr_enter":
```
enable isr_trace
mark "ISR enter"
```

BP "isr_exit":
```
disable isr_trace
mark "ISR exit"
```

BP "isr_trace" (initially disabled):
```
log "ISR step PC=%X", PC
```

### Force jump po N opakováních

```
$count += 1
if $count >= 100 then set PC 0x8000
if $count >= 100 then $count = 0
```

## Chybové stavy

### Parse-time

Lexer / parser chyba (špatný keyword, neukončený string, špatná
syntaxe `set` / `poke`, chybějící `then`, ...) zobrazí inline error
pod action textboxem v Edit BP dialogu. BP runtime se chová jako
prázdná akce (= Stop fallback - emulátor zastaví), dokud chybu
neopravíte.

### Runtime

Většina runtime chyb je tolerantní s warning na stderr:

- `enable` / `disable` s neexistujícím target -> warning, no-op.
- `set` s neznámým registrem -> warning, no-op.
- `set` bez CPU kontextu -> warning, no-op.
- `disable_self` mimo enforcement (= Test Eval) -> warning, no-op.
- `poke` mimo addressable RAM (= banking) -> typicky no-op nebo zápis
  do bank-aliased oblasti, podle chování paměťového subsystému.

`/` a `%` v $var compound a v expr argumentech: dělení nulou -> 0.
