
# Plán dalších vlastností

- debugger Step variants (Scanline step, until ROM exit, until SP unwind, ...)
- VRAM viewer, především v MZ-800 s možností přepínat si režimy zobrazení a modifikovat palety
- MZ-800: CG-RAM editor
- MZ-1500: PCG viewer/editor
- LUA scripting
- Chytrý mem search
- MCP server
- dynamická IORQ sběrnice, plug-and-play
- možnost připojovat externí moduly/knihovny na IORQ
- Hledání referencí (kde všude se čte/zapisuje právě tohle)
- Reverse trace (kdo zapsal hodnotu na adrese X?)
- Back step
- Cheat search
- vytvoření kompletního overscreen menu
- podpora TapeMZ a online převod z wav na mzf
- podpora gdb
- plná podpora CENTRONICS plotter (simulace tiskárny D100)
- plná simulace plotter/printer Sharp MZ-1P16


## Emulace

- Přeměřit na reálném HW: `VIDEO_H_BACK_PORCH_TICKS`, `VIDEO_H_FRONT_PORCH_TICKS` (aktuálně 104+39=143, ale přesný split není ověřený)
- Zvážit emulaci částečně obsazené VRAM u MZ-800
- MZ-1500 gdg: ověřit nepřítomnost VRAM latch

## Lokalizace

- Projít všechna volání `baseui_show_error_message()` a `baseui_show_message()` v emulátoru a zajistit, aby formátovací řetězce i hlášky procházely přes `_()` makro. Zatím nalezené nelokalizované: `src/emulator/mzarch/bootstrap.c` (2× `baseui_show_error_message`). Testovací infrastruktura (`test-i18n-coverage`) tyto případy neodhalí, protože kontroluje pouze soubory v `src/ui-imgui/` a `src/ui/`.
- Velká část UI textů v `src/ui-imgui/` stále nemá lokalizační makra (`_()` / `_L()`). Je potřeba systematicky projít všechna okna a dialogy a doplnit překlady pro všechny viditelné řetězce (labely, nápovědy, hlášky, popisky tlačítek).

## ImGui - Toto si tu ponechávám trvale, jako připomínku

- U všech lokalizovatelných aktivních prvků je potřeba zajistit jedinečnost názvu přidáním suffixu `##unique_name`
- Je potřeba projít všechny ImGui rutiny v `ui-imgui` a ošetřit, aby se při chybových stavech nevolal `return` dříve, než dojde k uzavření ImGui prvku (především Window)


# Chyby čekající na bugfix

- **topmenu**: Když uživatel použije k navigaci kurzory, jsou načítány jako vstup z klávesnice do emulace (zvážit, zda toto chování půjde potlačit)
- **Chyba v emulaci MZ-800**: Už velice dlouho je pocit, že když hraje Flappy demo, tak se při HW scrollu zrychluje tempo hudby. To by naznačovalo, že v GDG máme nějaký další nedokumentovaný stav vyvolávající CPU WAIT. Tohle je potřeba ověřit měřením na reálném HW.
