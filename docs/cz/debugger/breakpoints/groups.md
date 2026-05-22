# Breakpoint groups

Skupiny tvoří hierarchický strom nad breakpointy. Slouží k organizaci,
hromadnému enable / disable (cascade) a vizuálnímu členění v UI.

## Účel

- **Organizace** - logické členění BP do tematických skupin (např. "ROM
  monitor", "Game engine", "FDC trace"). Tree view je čitelnější než
  ploché seznamy desítek BP.
- **Hromadné enable / disable** - cascade flag. Disable rodičovské
  skupiny vypne všechny BP ve větvi bez nutnosti měnit per-BP `enabled`.
- **Color coding** - per-skupina barvy pozadí / textu (color labelu),
  děděné v UI labelu skupiny. Per-BP barvy jsou nezávislé (= bez
  inheritance, viz "Color sémantika" níže).
- **Persistence** - skupiny + parent linky se ukládají v `.bpt` JSON
  sekci `groups` paralelně s breakpointy.

## Hierarchický model

### Datový model

Skupina má následující atributy:

- **ID** - unikátní kladné číslo.
- **parent** - ID rodičovské skupiny nebo -1 (= root).
- **order** - pořadí zobrazení v rámci sourozenců.
- **enabled** - cascade flag.
- **name** - název skupiny.
- **bg_rgb**, **fg_rgb** - barva pozadí a textu labelu.

Breakpoint má symetrické pole `parent` se stejnou sémantikou
(-1 = root, jinak ID skupiny).

### Vztahy parent / child

- **Root** je virtuální (nemá entitu) - reprezentován hodnotou
  `parent = -1`.
- **Skupina** může mít rodiče = jinou skupinu (parent >= 0) nebo root
  (parent = -1).
- **Breakpoint** může mít rodiče = skupinu (parent >= 0) nebo root
  (parent = -1).
- **Vícenásobné rodičovství** = nepodporováno (= jeden parent ID per
  child).

### Limit nesting depth

Maximální hloubka stromu je 32 úrovní. Limit kontroluje:

- Load-time scan parent řetězce (validace při načtení `.bpt`).
- Runtime rekurze při výpočtu cascade enabled (belt-and-suspenders
  guard pro případ, kdy by load validace selhala).

Změna parent přes UI / drag-drop má vlastní runtime guard proti cyklu.

## Cascade enable sémantika

### Algoritmus

Effective enabled stav BP se počítá iterací od jeho parent skupiny po
parent řetězci nahoru. Pravidla:

- **AND** napříč celou cestou - všechny skupiny od BP po root musí být
  `enabled = true`.
- **Krátký okruh** - první disabled skupina v cestě stačí na vyloučení
  BP.
- **Per-BP `enabled` flag je AND-ován samostatně** - effective enabled
  = `bpt.enabled && (všechny skupiny v cestě enabled)`.
- **Root je vždy enabled** - není entita, kterou by šlo vypnout.
- **Neexistující skupina** (= dangling parent ID po manuální editaci
  `.bpt`) se chová jako root (= treat as enabled). Není error.

### Důsledek pro hot path

Při každém BP enforce hooku se prochází parent řetězec rekurzivně.
Cascade enabled stav je proto precomputován do interního bytemapu při
každé mutaci group/BP enabled flagu - hot path PC_EXEC pak testuje jen
jednorázový bytemap, ne rekurzi.

Pro per-instruction BP (GLOBAL) a IRQ / HW_EVENT enforce se cascade
test volá runtime, ale jen po short-circuit testu globálního active
flagu.

## Color sémantika

**Inheritance NEEXISTUJE.** Skupina i breakpoint mají vlastní `bg_rgb`
/ `fg_rgb` - tree view UI je renderuje nezávisle:

- Label skupiny používá vlastní bg/fg barvy skupiny.
- Label breakpointu používá vlastní bg/fg barvy breakpointu.

Default barvy nově vytvořené skupiny / BP:

- BG = `0x000000` (černá)
- FG = `0xFFFFFF` (bílá)

Pokud uživatel chce skupinu i její BP vizuálně sjednotit, musí barvy
nastavit na obou úrovních ručně.

### Disabled cascade overlay

Pokud rodičovská skupina je disabled, tree view kreslí strikethrough
overlay (čára přes label) na každém BP v dané větvi.

## UI workflow

### Tree view

Hlavní okno `Breakpoints` vykresluje strom. Per úroveň:

- **Skupina** = TreeNode otevíratelný šipkou vlevo, klik na label =
  selekce.
- **Breakpoint** = řádek s dvojklik = otevřít Edit panel.

### Operace - kontextové menu (right-click)

| Položka                | Akce |
|------------------------|------|
| Expand All             | Rozbalí všechny TreeNode |
| Collapse All           | Sbalí všechny TreeNode |
| Add Breakpoint Event...| Otevře Edit panel pro nový BP |
| Add Group...           | Otevře Edit panel pro novou skupinu |
| Edit row...            | Otevře Edit panel pro vybranou položku |
| Unparent               | Přesune vybranou položku do root (parent = -1) |
| Delete Row/Branch      | Smaže vybranou položku |
| Delete All             | Smaže vše (BP i skupiny) |

### Drag-drop reparenting

Source = každý řádek (skupina i BP).

Target sites:

- **Skupina** - drop na label skupiny = reparent dropped item pod
  cílovou skupinu.
- **Root** - drop pod tree view (mimo všechny skupiny) = reparent na
  root.

## Validace - cycle prevention

Při změně parent skupiny (přes UI nebo drag-drop) probíhá:

1. **Self-parent reject** - skupina nemůže být svým vlastním rodičem.
2. **Non-existing parent reject** - cílová skupina musí existovat.
3. **Cycle scan** - iterace od nového parent řetězce nahoru. Pokud
   narazí na měněnou skupinu, znamenalo by to vznik cyklu = reject.
4. Iterace omezená depth fuse pro případ existujícího cyklu z dřívějšího
   ručního editu `.bpt`.

Pro BP cycle scan není potřeba - BP nemůže být parentem (jen
`parent = group ID | -1`).

## Operace na skupinách

### Add

Vytvoří skupinu s default barvami a `enabled = true`. Vrací nové ID
nebo -1 při alokačním selhání.

### Remove

Při smazání skupiny:

1. Skupiny s touto skupinou jako rodičem → reparent na root.
2. BP s touto skupinou jako rodičem → reparent na root.
3. Skupina se odstraní.

**Důsledek:** delete skupiny **nikdy nemaže její potomky**. Cascade
delete (= smazat skupinu i všechny BP uvnitř) musí UI explicitně
udělat sám smazáním každého child před smazáním skupiny.

### Set enabled / name / colors / order / parent

Per-field settery. Změny `enabled` a `parent` invalidují cache
effective enabled (= recompute bytemap pro hot path).

## Persistence

Skupiny serializují do JSON sekce `groups` (top-level). Per-skupina
objekt:

```json
{
  "id": 1,
  "parent_id": -1,
  "name": "ROM monitor",
  "enabled": true,
  "order": 1.0,
  "color_bg": 0,
  "color_fg": 16777215
}
```

Pořadí klíčů stable (saver vždy stejně). Loader permisivní - chybějící
klíče dostanou default (viz `persistence.md` per-key tabulka).

Breakpointy referencují skupinu přes vlastní pole `parent_id`. Loader
order:

1. Nejprve loadne všechny skupiny (= parent_id existuje).
2. Pak loadne breakpointy (= jejich parent_id reference je validní).
3. Skupiny s neexistujícím parent_id (= dangling) se chovají jako root
   (cascade enable returns true).

## Edge cases

- **Skupina bez parent (-1)** - top-level / root. Jediná možnost pro
  root skupiny.
- **Cyklus po manuálním editu `.bpt`** - load-time cycle scan
  detekuje:
  - Self-loop (= grp.parent == grp.id)
  - Uzavřený kruh napříč více skupinami (depth limit 32)
  - Návrat na current group ID v parent řetězci

  Při detekci: warning na stderr + `parent = -1` (= reparent na root).
  Belt-and-suspenders runtime guard zachytí i případ, kdy by load
  validace selhala (depth fallback = treat as enabled, warning).
- **Skupina se sebou jako parent** - chytá load validace (= reparent
  na root + warning). Runtime set_parent navíc reject už při zápisu.
- **Smazaná skupina s BP children** - children se reparentují do root,
  zůstanou aktivní (= NEcascade delete).
- **Order field** - použit pro řazení v UI tree view. Primárně order,
  pak abeceda, pak ID.

## Související dokumenty

- `README.md` - architektura subsystému
- `types.md` - katalog 9 BP typů (každý BP může být v skupině)
- `persistence.md` - JSON schema pro `groups` sekci + `parent_id` field
- `match-modes.md` - per-BP match modes (nezávislé na groups)
