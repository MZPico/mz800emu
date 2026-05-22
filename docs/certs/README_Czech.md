Pokud je aplikace spuštěna v Linuxu, nebo v MSYS2 consoli, tak libcurl funguje bez problémů.
Pokud je však aplikace spuštěna ve Windows samostatně z  prostředí s nakopírovanýma runtime knihovnama, tak je potřeba vytvořit adresář ./certs a do něj nakopírovat soubor cacert.pem

Soubor stáhněte z https://curl.se/ca/cacert.pem
