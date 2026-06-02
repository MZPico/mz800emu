# Pipe transport

Pipe transport je výchozí způsob, kterým Python wrapper komunikuje
s emulátorem. AI klient (přes Python wrapper) **spustí emulátor jako
podproces** (subprocess) a komunikuje s ním přes anonymní rouru
napojenou na `stdin`/`stdout`.

## Kdy ho použít

- **CI / batch testy** - automatizované běhy bez živé GUI session.
- **Claude Desktop** - klient typicky spawnuje vlastní MCP server,
  nepotřebuje sdílenou session s uživatelem.
- **Headless workflowy** - regression suite, fault injection, AI
  prochází definovaný scénář bez human-in-the-loop.
- **Per-session izolace** - každá MCP session má svoji vlastní čistou
  instanci emulátoru bez kontaminace stavem z předchozího běhu.

Pokud chcete naopak sdílet **živou GUI session** s AI (uživatel vidí
a může zasahovat), použijte [TCP transport](transport-tcp.md).

## Příkaz

```bash
mz800emu.exe --mcp-pipe --headless --no-first-run-windows
```

| Flag | Význam |
|------|--------|
| `--mcp-pipe` | aktivuje MCP backend ve stdio režimu; emu čte JSONL ze `stdin`, zapisuje na `stdout` |
| `--headless` | nevykresluje GUI okno (per [Headless režim](headless-mode.md)) |
| `--no-first-run-windows` | potlačí first-run dialogy, které by jinak blokovaly headless start |

Tento příkaz Python wrapper spouští **automaticky** jako podproces -
uživatel obvykle neinvokuje přímo z příkazové řádky. Manuální použití
je vhodné pro debug (= ověřit, že emu odpoví na ručně poslaný JSONL
request).

## Co se stane při startu

1. Emu se nakonfiguruje jako headless (žádné SDL okno, no-op audio
   device).
2. MCP backend zaregistruje `stdin` reader thread a `stdout` writer.
3. CPU emulace běží normálně v emulátorovém vlákně.
4. Klient pošle JSONL request na `stdin`, MCP backend ho rozparsuje,
   předá do dispatch tabulky, vrátí JSONL response na `stdout`.
5. Když klient zavře `stdin` (= ukončí MCP session), emu detekuje
   EOF a korektně se ukončí.

## Lifetime

Lifetime emu instance je svázaný s lifetime MCP session:

- Klient ukončí session - emu dostane EOF a ukončí se.
- Klient spustí novou session - Python wrapper spawne novou emu
  instanci.

Pro persistentní session, kde stav emu přežívá mezi konexemi, použijte
[TCP transport](transport-tcp.md).

## Limitace

- **1 klient per session** - pipe je point-to-point, nelze sdílet mezi
  více klienty.
- **Žádný human-in-the-loop** - GUI emu neběží, uživatel nevidí
  obrazovku ani neslyší zvuk.
- **Žádný persistentní stav** - po skončení session jsou snapshot,
  breakpointy a media-mounty pryč. Použít MCP tool pro `state_save`
  do souboru, pokud chceme přenést stav mezi sessions.

## Oddělení výstupních kanálů (stdout vs stderr)

V pipe režimu je `stdout` **vyhrazený výhradně pro JSONL protokol** -
každý řádek je právě jedna JSON-RPC zpráva (hello, response, event).
Veškerý ostatní výstup emulátoru (informační hlášky, klávesové tipy,
hlášky o rychlosti, bannery periferií, chybové hlášky) jde na `stderr`.

Emulátor toho při startu i běhu vypisuje hodně na stdout. Aby tento
plain text nerozbil JSONL stream u klienta, emu hned na začátku pipe
režimu přesměruje svůj stdout na stderr a původní stdout si ponechá jen
pro protokol. Důsledky pro integraci:

- **Parsujte jen `stdout`** jako JSONL - každý řádek je validní JSON.
- **`stderr` je log** - logujte ho zvlášť, nebo zahoďte; není součástí
  protokolu a jeho formát není stabilní.
- **Nikdy nemíchejte oba kanály** do jednoho streamu (např. `2>&1`) -
  porušilo by to JSONL parsing.

## Diagnostika

Pokud emu spadne hned po startu, zkuste spustit manuálně bez wrapperu
a oddělte kanály do souborů:

```bash
mz800emu.exe --mcp-pipe --headless --no-first-run-windows 1>out.jsonl 2>err.log
# (zůstane viset, čeká na JSONL na stdin - ukončete Ctrl+C nebo EOF)
```

`out.jsonl` musí obsahovat jen JSONL (na začátku hello message),
`err.log` všechny logy včetně inicializace. Pokud emu nedoběhne ani do
prvního log řádku v `err.log`, problém je mimo MCP backend (= chyba
v inicializaci emu samotného). Pro viditelné logy v konzoli na Windows
je potřeba `FORCE_CONSOLE=1` build (viz [Headless režim](headless-mode.md)).

## Související

- [Python wrapper](python-wrapper.md) - jak ho Python wrapper
  používá z FastMCP
- [TCP transport](transport-tcp.md) - alternativní transport pro
  human-in-the-loop
- [Headless režim](headless-mode.md) - nezávislé použití
  `--headless` flagu
