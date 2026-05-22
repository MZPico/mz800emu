# Breakpoint - IRQ signal source filter

`IRQ_SIG` breakpoint triggeruje při **edge raise** peripheral INT line
(= pre-dispatch). Filtruje per source: PIOZ80 PORT_A/B, CTC2, FDC,
Other.

Tento BP type doplňuje `IRQ` o druhou perspektivu:

| Typ | Kdy fire | Filter |
|-----|----------|--------|
| `IRQ` | CPU acknowledged INT (= dispatched ISR) | IM mode + RST opcode + IM 2 vector/ISR |
| `IRQ_SIG` | Peripheral X raise INT line | Source (PIOZ80 A/B, CTC2, FDC, Other) |

`IRQ` fire vidí ISR side (= "začal jsem obsluhovat přerušení"),
`IRQ_SIG` vidí peripheral side (= "peripheral X chce moji pozornost,
ještě ho CPU neacknowledged").

## Kdy se enforce volá

Hook se volá **po** update interrupt bus mask, **před** Z80 dispatchem.
Hook drží snapshot předchozího bus mask a detekuje **edge raise**
(bity 0 -> 1).

Sustained level (= peripheral drží INT line po několika voláních) NEFIRE
opakovaně - jen při hraně přechodu. Čistá hrana 1 -> 0 (= peripheral
release INT) také nefire (= filter sleduje raise edge).

```
prev_irq:  0b 00000000
curr_irq:  0b 00000010   (PIOZ80 just raised)
edge:      0b 00000010   <- fire pro BP s PIOZ80 source
```

Pokud několik bits raise zároveň (= jeden frame), všechny BP s
matching source dostanou fire (= multi-source detect).

## Source bits

| Bit | Význam |
|-----|--------|
| 0   | Z80 PIO port A IRQ |
| 1   | Z80 PIO port B IRQ |
| 2   | CTC channel 2 (timer/counter) |
| 3   | WD279x floppy controller (FDC) |
| 4   | nedetekovatelný / bus latch (OTHER) |

### PIOZ80 sub-detekce

Z80 PIO je v interrupt bus mask single bit pro celý PIO chip; A vs B
se rozlišuje runtime přes aktuální port, který drží INT pin v daisy
chain. Při PIOZ80 raise enforce přečte tento stav a mapuje na port A
nebo B.

Race-safe fallback: pokud aktuální port není ještě nastavený, hook
reportuje source jako `OTHER`.

### Other (bus latch)

Bit `OTHER` chytá:
- Budoucí MZ peripherals s vlastním INT bitem (= jak ekosystém roste).
- Bus latch / nedefinované zdroje.

V současném kódu žádný bit mimo PIOZ80 / CTC2 / FDC neexistuje, ale
filter zachová zpětnou kompatibilitu při budoucích rozšířeních.

## Multi-source semantika (OR)

BP s více source bity fire pokud **libovolný** vybraný source raise:

```
source_mask = CTC2 | FDC

prev_irq:  0b 00000000
curr_irq:  0b 00000100   (CTC2 raised)
=> fire (CTC2 selected, raised)

curr_irq:  0b 00001000   (FDC raised next call)
=> fire (FDC selected, raised)

curr_irq:  0b 00000010   (PIOZ80 raised, ne v mask)
=> nefire
```

Je-li potřeba **AND** semantika ("fire jen pokud A i B současně"),
použijte 2 separátní `IRQ_SIG` BP s condition expression - debugger
nemá built-in AND match.

## Default a validace

- Default pro nový BP: source mask = 0 (= invalid, žádný source).
- UI **vyžaduje aspoň 1 source bit** (= validation blokuje OK).
- Setter source masku 0 uloží (= UI defenzivně checkuje), ale enforce
  ho ignoruje (= no fire).

## Persistence

`.bpt` JSON ukládá source mask jako **array of stable string names**:

```json
{
  "type": "IRQ_SIG",
  "irq_sig_sources": ["PIOZ80_A", "CTC2"]
}
```

Stable jména:
- `"PIOZ80_A"` = Z80 PIO port A
- `"PIOZ80_B"` = Z80 PIO port B
- `"CTC2"` = CTC channel 2
- `"FDC"` = WD279x FDC
- `"OTHER"` = bus latch / nedetekovatelný

Format dovoluje budoucí přidání nových sources bez breaking persistence
(= unknown jméno je při loadu ignorováno + warning na stderr; známé
loadnou OR-em do mask).

Chybějící `irq_sig_sources` klíč = mask 0 (= invalid; UI validation
zachytí).

## Kontext pro condition / action

Hook plní expression kontext:
- `Address` = newly raised bits (= edge bus mask)
- `Value` = matched source bits z mask
- žádné `IsRead/Write/Port/Exec` flags

Příklad condition:
```
Value & 0x04   ; fire jen pokud CTC2 byl mezi raised
```

(Stejný efekt jako single-source BP, ale dovoluje granulárnější trigger
v multi-source BP.)

## Use cases

1. **Trace peripheral activity bez ISR overhead.** `IRQ_SIG` fire i v
   době, kdy CPU má `IFF1=0` nebo dispatchuje pomalu. `IRQ` (post-dispatch)
   by se opozdil/nefire-l.

2. **Detect race conditions mezi raise a INTACK.** `IRQ_SIG` fire
   pre-dispatch, `IRQ` fire post-dispatch. Mezi nimi může proběhnout
   několik instrukcí (= window kdy peripheral drží line a CPU finishes
   předchozí instrukci).

3. **Single source isolation.** `IRQ_SIG` jen s `FDC` source
   triggeruje **jen** FDC raise, ne ostatní zdroje. `IRQ` post-dispatch
   neumí (musíš použít IM 2 vector filter, ale ten vyžaduje IM 2 + znalost
   přesného vector slotu peripheralu).

4. **Latch trace.** BP s mask `OTHER` fire na nedefinovaný source -
   užitečné pro debug nového peripheralu (= "drží INT bit, ale neznám
   zdroj").

## Performance

Hook je guard-aware: před voláním se testuje, zda je registrován aspoň
jeden `IRQ_SIG` BP. Bez registrovaného BP = zero overhead na hot path
(= 1 array lookup).

Při registrovaném BP enforce hook check edge (`prev != curr`) PŘED
iterací BP listu (= zero cost při stabilním bus mask).

## Související dokumenty

- `irq-filter.md` - post-dispatch IRQ filter
- `types.md` - katalog všech BP typů
- `expression-syntax.md` - condition gramatika
