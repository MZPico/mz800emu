#!/usr/bin/env python3
"""Jednorázový skript: přidá překlady IGFD stringů do .po souborů všech jazyků."""
import re
import os

LOCALE_DIR = "src/locale"

TRANSLATIONS = {
    "sk": {
        "Reset search": "Resetovat hladanie",
        "Devices": "Disky",
        "Edit path\nYou can also right click on path buttons":
            "Upravit cestu\nMozete tiez kliknut pravym tlacidlom na tlacidla cesty",
        "Reset to current directory": "Obnovit aktualny adresar",
        "Create Directory": "Vytvorit adresar",
        "File Name:": "Nazov suboru:",
        "Directory Path:": "Cesta k adresar:",
        "The selected file already exists!": "Vybrany subor uz existuje!",
        "Are you sure you want to overwrite it?": "Naozaj ho chcete prepisat?",
        "Confirm": "Potvrdit",
        "B": "B", "KB": "KB", "MB": "MB", "GB": "GB",
        "File name": "Nazov suboru",
        "Date": "Datum",
        "Places": "Miesta",
    },
    "de": {
        "Reset search": "Suche zur\u00fccksetzen",
        "Devices": "Laufwerke",
        "Edit path\nYou can also right click on path buttons":
            "Pfad bearbeiten\nSie k\u00f6nnen auch mit der rechten Maustaste auf Pfad-Schaltfl\u00e4chen klicken",
        "Reset to current directory": "Aktuelles Verzeichnis zur\u00fccksetzen",
        "Create Directory": "Verzeichnis erstellen",
        "File Name:": "Dateiname:",
        "Directory Path:": "Verzeichnispfad:",
        "The selected file already exists!": "Die ausgew\u00e4hlte Datei existiert bereits!",
        "Are you sure you want to overwrite it?": "M\u00f6chten Sie sie wirklich \u00fcberschreiben?",
        "Confirm": "Best\u00e4tigen",
        "B": "B", "KB": "KB", "MB": "MB", "GB": "GB",
        "File name": "Dateiname",
        "Date": "Datum",
        "Places": "Orte",
    },
    "it": {
        "Reset search": "Reimposta ricerca",
        "Devices": "Unit\u00e0",
        "Edit path\nYou can also right click on path buttons":
            "Modifica percorso\nPuoi anche fare clic destro sui pulsanti del percorso",
        "Reset to current directory": "Ripristina directory corrente",
        "Create Directory": "Crea directory",
        "File Name:": "Nome file:",
        "Directory Path:": "Percorso directory:",
        "The selected file already exists!": "Il file selezionato esiste gi\u00e0!",
        "Are you sure you want to overwrite it?": "Sei sicuro di volerlo sovrascrivere?",
        "Confirm": "Conferma",
        "B": "B", "KB": "KB", "MB": "MB", "GB": "GB",
        "File name": "Nome file",
        "Date": "Data",
        "Places": "Luoghi",
    },
    "es": {
        "Reset search": "Restablecer b\u00fasqueda",
        "Devices": "Unidades",
        "Edit path\nYou can also right click on path buttons":
            "Editar ruta\nTambi\u00e9n puede hacer clic derecho en los botones de ruta",
        "Reset to current directory": "Restablecer directorio actual",
        "Create Directory": "Crear directorio",
        "File Name:": "Nombre de archivo:",
        "Directory Path:": "Ruta del directorio:",
        "The selected file already exists!": "\u00a1El archivo seleccionado ya existe!",
        "Are you sure you want to overwrite it?": "\u00bfEst\u00e1 seguro de que desea sobreescribirlo?",
        "Confirm": "Confirmar",
        "B": "B", "KB": "KB", "MB": "MB", "GB": "GB",
        "File name": "Nombre de archivo",
        "Date": "Fecha",
        "Places": "Lugares",
    },
    "nl": {
        "Reset search": "Zoeken resetten",
        "Devices": "Schijven",
        "Edit path\nYou can also right click on path buttons":
            "Pad bewerken\nU kunt ook met de rechtermuisknop op padknoppen klikken",
        "Reset to current directory": "Huidige map terugzetten",
        "Create Directory": "Map aanmaken",
        "File Name:": "Bestandsnaam:",
        "Directory Path:": "Mappad:",
        "The selected file already exists!": "Het geselecteerde bestand bestaat al!",
        "Are you sure you want to overwrite it?": "Weet u zeker dat u het wilt overschrijven?",
        "Confirm": "Bevestigen",
        "B": "B", "KB": "KB", "MB": "MB", "GB": "GB",
        "File name": "Bestandsnaam",
        "Date": "Datum",
        "Places": "Plaatsen",
    },
    "fr": {
        "Reset search": "R\u00e9initialiser la recherche",
        "Devices": "Lecteurs",
        "Edit path\nYou can also right click on path buttons":
            "Modifier le chemin\nVous pouvez aussi faire un clic droit sur les boutons de chemin",
        "Reset to current directory": "R\u00e9initialiser le r\u00e9pertoire courant",
        "Create Directory": "Cr\u00e9er un r\u00e9pertoire",
        "File Name:": "Nom du fichier :",
        "Directory Path:": "Chemin du r\u00e9pertoire :",
        "The selected file already exists!": "Le fichier s\u00e9lectionn\u00e9 existe d\u00e9j\u00e0 !",
        "Are you sure you want to overwrite it?": "Voulez-vous vraiment l'\u00e9craser ?",
        "Confirm": "Confirmer",
        "B": "B", "KB": "KB", "MB": "MB", "GB": "GB",
        "File name": "Nom du fichier",
        "Date": "Date",
        "Places": "Emplacements",
    },
    "pl": {
        "Reset search": "Resetuj wyszukiwanie",
        "Devices": "Dyski",
        "Edit path\nYou can also right click on path buttons":
            "Edytuj \u015bcie\u017ck\u0119\nMo\u017cesz te\u017c klikn\u0105\u0107 prawym przyciskiem myszy na przyciski \u015bcie\u017cki",
        "Reset to current directory": "Przywr\u00f3\u0107 bie\u017c\u0105cy katalog",
        "Create Directory": "Utw\u00f3rz katalog",
        "File Name:": "Nazwa pliku:",
        "Directory Path:": "\u015acie\u017cka katalogu:",
        "The selected file already exists!": "Wybrany plik ju\u017c istnieje!",
        "Are you sure you want to overwrite it?": "Czy na pewno chcesz go nadpisa\u0107?",
        "Confirm": "Potwierd\u017a",
        "B": "B", "KB": "KB", "MB": "MB", "GB": "GB",
        "File name": "Nazwa pliku",
        "Date": "Data",
        "Places": "Miejsca",
    },
    "uk": {
        "Reset search": "\u0421\u043a\u0438\u043d\u0443\u0442\u0438 \u043f\u043e\u0448\u0443\u043a",
        "Devices": "\u0414\u0438\u0441\u043a\u0438",
        "Edit path\nYou can also right click on path buttons":
            "\u0420\u0435\u0434\u0430\u0433\u0443\u0432\u0430\u0442\u0438 \u0448\u043b\u044f\u0445\n\u0412\u0438 \u0442\u0430\u043a\u043e\u0436 \u043c\u043e\u0436\u0435\u0442\u0435 \u043a\u043b\u0430\u0446\u043d\u0443\u0442\u0438 \u043f\u0440\u0430\u0432\u043e\u044e \u043a\u043d\u043e\u043f\u043a\u043e\u044e \u043c\u0438\u0448\u0456 \u043d\u0430 \u043a\u043d\u043e\u043f\u043a\u0430\u0445 \u0448\u043b\u044f\u0445\u0443",
        "Reset to current directory": "\u0412\u0456\u0434\u043d\u043e\u0432\u0438\u0442\u0438 \u043f\u043e\u0442\u043e\u0447\u043d\u0438\u0439 \u043a\u0430\u0442\u0430\u043b\u043e\u0433",
        "Create Directory": "\u0421\u0442\u0432\u043e\u0440\u0438\u0442\u0438 \u043a\u0430\u0442\u0430\u043b\u043e\u0433",
        "File Name:": "\u041d\u0430\u0437\u0432\u0430 \u0444\u0430\u0439\u043b\u0443:",
        "Directory Path:": "\u0428\u043b\u044f\u0445 \u0434\u043e \u043a\u0430\u0442\u0430\u043b\u043e\u0433\u0443:",
        "The selected file already exists!": "\u0412\u0438\u0431\u0440\u0430\u043d\u0438\u0439 \u0444\u0430\u0439\u043b \u0432\u0436\u0435 \u0456\u0441\u043d\u0443\u0454!",
        "Are you sure you want to overwrite it?": "\u0412\u0438 \u0432\u043f\u0435\u0432\u043d\u0435\u043d\u0456, \u0449\u043e \u0445\u043e\u0447\u0435\u0442\u0435 \u0439\u043e\u0433\u043e \u043f\u0435\u0440\u0435\u0437\u0430\u043f\u0438\u0441\u0430\u0442\u0438?",
        "Confirm": "\u041f\u0456\u0434\u0442\u0432\u0435\u0440\u0434\u0438\u0442\u0438",
        "B": "B", "KB": "KB", "MB": "MB", "GB": "GB",
        "File name": "\u041d\u0430\u0437\u0432\u0430 \u0444\u0430\u0439\u043b\u0443",
        "Date": "\u0414\u0430\u0442\u0430",
        "Places": "\u041c\u0456\u0441\u0446\u044f",
    },
    "ja": {
        "Reset search": "\u691c\u7d22\u30ea\u30bb\u30c3\u30c8",
        "Devices": "\u30c9\u30e9\u30a4\u30d6",
        "Edit path\nYou can also right click on path buttons":
            "\u30d1\u30b9\u3092\u7de8\u96c6\n\u30d1\u30b9\u30dc\u30bf\u30f3\u3092\u53f3\u30af\u30ea\u30c3\u30af\u3059\u308b\u3053\u3068\u3082\u3067\u304d\u307e\u3059",
        "Reset to current directory": "\u73fe\u5728\u306e\u30c7\u30a3\u30ec\u30af\u30c8\u30ea\u306b\u30ea\u30bb\u30c3\u30c8",
        "Create Directory": "\u30c7\u30a3\u30ec\u30af\u30c8\u30ea\u3092\u4f5c\u6210",
        "File Name:": "\u30d5\u30a1\u30a4\u30eb\u540d\uff1a",
        "Directory Path:": "\u30c7\u30a3\u30ec\u30af\u30c8\u30ea\u30d1\u30b9\uff1a",
        "The selected file already exists!": "\u9078\u629e\u3057\u305f\u30d5\u30a1\u30a4\u30eb\u306f\u3059\u3067\u306b\u5b58\u5728\u3057\u307e\u3059\uff01",
        "Are you sure you want to overwrite it?": "\u672c\u5f53\u306b\u4e0a\u66f8\u304d\u3057\u307e\u3059\u304b\uff1f",
        "Confirm": "\u78ba\u8a8d",
        "B": "B", "KB": "KB", "MB": "MB", "GB": "GB",
        "File name": "\u30d5\u30a1\u30a4\u30eb\u540d",
        "Date": "\u65e5\u4ed8",
        "Places": "\u5834\u6240",
    },
}


def escape_po(s):
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')


def patch_po(lang, translations):
    po_path = os.path.join(LOCALE_DIR, lang, "LC_MESSAGES", "mz800emu.po")
    if not os.path.exists(po_path):
        print(f"  SKIP {lang}: soubor neexistuje")
        return

    with open(po_path, "r", encoding="utf-8") as f:
        content = f.read()

    changed = 0

    for msgid, msgstr in translations.items():
        esc_id = escape_po(msgid)
        esc_str = escape_po(msgstr)

        if '\n' in msgid:
            # Multiline msgid: hledáme blok s prázdným msgstr
            parts = esc_id.split('\\n')
            id_block = 'msgid ""\n' + '\n'.join(f'"{p}\\n"' for p in parts[:-1]) + f'\n"{parts[-1]}"'
            old_block = id_block + '\nmsgstr ""'
            new_block = id_block + f'\nmsgstr "{esc_str}"'
            if old_block in content:
                content = content.replace(old_block, new_block, 1)
                changed += 1
            # Odstraň fuzzy flag nad tímto blokem
            content = re.sub(r'#, fuzzy\n(' + re.escape(id_block) + r')', r'\1', content)
        else:
            # Jednořádkový msgid — nahraď fuzzy + prázdný nebo špatný msgstr
            # Pattern: optional "#, fuzzy\n" + msgid line + "\n" + msgstr line
            pattern = r'(?:#, fuzzy\n)?(' + re.escape(f'msgid "{esc_id}"') + r'\n)msgstr "[^"]*(?:\n"[^"]*")*"'
            replacement = r'\1' + f'msgstr "{esc_str}"'
            new_content, n = re.subn(pattern, replacement, content)
            if n > 0:
                content = new_content
                changed += n

    with open(po_path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"  {lang}: {changed} překladů aktualizováno")


for lang, trans in TRANSLATIONS.items():
    patch_po(lang, trans)

print("Hotovo.")
