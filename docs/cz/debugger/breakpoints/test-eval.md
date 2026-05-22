# Live Test Eval

Live Test Eval je UI pomůcka v Edit BP dialogu, která dovoluje
vyhodnotit libovolný condition expression proti **aktuálnímu emu state**
**bez nutnosti vytvářet a uložit BP**. Slouží k ladění výrazů před
finálním zápisem do `condition` nebo `action`.

## UI

Sekce "Live Eval Test (optional)" je posledním blokem v Edit BP
panelu, pod sekcemi BP Options / Name / Trigger logic / Action on
trigger.

Komponenty:

- **Textbox** - full width input pro výraz.
- **Button "Test Eval"** - 100 pt širokým zarovnaný vpravo, na
  samostatném řádku pod textboxem.
- **Výsledek** - text pod tlačítkem. Format `= <dec> (0x<hex>)` pro
  úspěch, `ERR: <message>` pro syntax/eval error.

Textbox není svázaný s polem condition ani action - je to nezávislý
scratch buffer per Edit panel session. Hodnota se neukládá nikam (ne
do BP, ne do persistence).

## Naplnění kontextu

Po stisku Test Eval se naplní expression context z live emu state:

- **CPU registry** - aktuální obsah Z80 CPU.
- **Cycle** = kumulativní GDG pixel ticks (ne Z80 T-state counter).
- **Frame** = počet kompletních snímků.
- **Scanline** = aktuální raster row paprsku.
- **BankPC**, **BankAddr** = aktuální banking zone pro PC adresu.

Pole závislá na BP hit kontextu (Address / Value / IsRead / IsWrite /
IsExec / IsPort) zůstávají na 0 / false (= caller je obvykle plní
per-typ enforcement vrstvou, ale Test Eval žádný hook context nemá).

### Co Test Eval plní = funguje

- **CPU registry** - `A`, `B`, `C`, `D`, `E`, `H`, `L`, `BC`, `DE`,
  `HL`, `SP`, `PC`, `IX`, `IY`.
- **Z80 flagy** `Cf`, `Zf`, `Sf`, `Pf`, `Hf`, `Nf` - bit dekompozice z F.
- **Shadow registry** `AF'`, `BC'`, `DE'`, `HL'`.
- **`I`, `R`, `IM`, `IFF1`, `IFF2`**.
- **`PC`, `SP`** - hlavní speciální registry.
- **`BankPC`, `BankAddr`** - banking zone pro aktuální PC.
- **Memory deref** `[addr]`, `{addr}` - čte přes live emu memory
  (přes aktuální banking).
- **`$vars`** - globální storage (perzistentní, sdílené napříč všemi BP).
- **Built-in funkce** `min`, `max`, `abs`, `if`, `bit`, `s8`, `s16`,
  `s32`.
- **`Cycle`, `Frame`, `Scanline`** - z GDG state.

### Co Test Eval NEplní = vždy 0 / false

- **`Address`, `Value`** - bez BP hit kontextu obě 0. Výrazy `Address`
  / `Value` ve Test Eval vždy vrátí 0.
- **`IsRead`, `IsWrite`, `IsExec`, `IsPort`** - všechny false. Výrazy
  jako `IsWrite && Value == 0x42` ve Test Eval vždy false.
- **`self_id`** - explicitně -1, ne ID žádného BP (ani editovaného).
- **I/O port read** `port[N]` - vrací 0 (bezpečnostní opatření, aby
  Test Eval neměl side-effects na live I/O).

## Threading caveat

Test Eval se volá z UI vlákna (= main thread, ImGui render loop).
Kontext čte live CPU strukturu a memory subsystem, které mutuje
emulační vlákno.

**Race s emulačním vláknem** je technicky možný:

- Pokud emulátor běží (= není pause), CPU registry se mění mezi
  vyhodnocením různých identifikátorů ve výrazu (= výraz `A == HL` může
  vidět A z jednoho cycle a HL z jiného).
- Memory deref `[addr]` chodí přes banking, který se může přepnout
  mid-eval.

Praktická situace:

- **Paused emu** (debugger zastavil běh) - ctx je stable, výsledek
  deterministický.
- **Running emu** - výsledek je snapshot best-effort. Per-frame
  konzistence OK pro lidskou interpretaci ("aktuálně asi A=0x42"),
  pro precizní debugging neslouží.

Doporučení: Test Eval používat **při paused debugger** (= klasický
workflow před stiskem Run).

## Příklady ladění

### Test CPU registru

```
A == 0x42
```

Vrátí 1 pokud aktuální A registru emu je 0x42, jinak 0.

```
HL & 0xFF00 == 0x4000
```

Test horního byte HL.

### Memory probe

```
[0xE000]
```

Přečte byte z paměti na adrese 0xE000 (přes aktuální banking) a vrátí
hodnotu.

```
{0xC000}
```

16-bit read z 0xC000 (low byte na 0xC000, high byte na 0xC001 - little
endian).

### Banking-aware

```
BankPC == 1
```

Test pokud aktuální PC je v zone ROM_LOWER (= zone hodnota 1).
Užitečné pro detekci "code v ROM monitoru".

### Built-in funkce

```
if(A < 10, 1, 0)
```

Vrátí 1 pokud A < 10, jinak 0.

```
bit(F, 6)
```

Extract Z flag bit (= flag bit pozice 6 v F registru, ekvivalent `Zf`).

### $vars

```
$myCounter
```

Read user var (= musí být dříve nastaveno akcí `set $myCounter = ...`
v nějakém BP, ručně přes UI panel `Variables` (Debugger → Variables),
nebo přes programové API).

```
$myCounter > 100
```

Test threshold. Neexistující jméno → 0 (= no error). Detail viz
`vars.md`.

## Co NEjde testovat

Tyto výrazy jsou ve Test Eval **syntakticky validní**, ale nedávají
smysluplný výsledek:

- **`Address`, `Value`** - vždy 0 (= žádný BP hit ctx).
- **`IsRead`, `IsWrite`, `IsExec`, `IsPort`** - vždy false.
- **`Cycle`** - vrací GDG pixel ticks (= ne čistý Z80 T-state).
  Pro per-frame relativní timing je OK.
- **Side-effect memory read** `[addr]!` - parsováno, ale flag `!` se
  v Test Eval ignoruje (= vždy běžný memory read bez side-effectu, viz
  `expression-syntax.md`).
- **Side-effect port read** `port[N]!` - dtto.
- **Hit count / skip count semantika** - per-BP runtime stav, výraz
  k němu nemá přístup.

Pro test podmínek závislých na hit ctx (= "co se stane při MEM_W
0xE000 hodnotou 0x42") je nutné BP skutečně vytvořit a sledovat jeho
fire (= action `log "%X", Value`).

## Související dokumenty

- `expression-syntax.md` - kompletní gramatika condition výrazů
  (= co lze v Test Eval testovat)
- `action-dsl.md` - action DSL syntax (= Test Eval vyhodnocuje **jen**
  expression, ne action; pro action neexistuje analog)
- `types.md` - per-typ kontext (= co plní BP enforce, kontrast oproti
  Test Eval)
- `README.md` - přehled subsystému
