#!/usr/bin/env python3
"""
i18n_translate.py — automatizovaný překladový skript pro mz800emu
Používá Claude API (Anthropic SDK) pro překlad .pot/.po souborů.

Použití:
    python3 tools/i18n_translate.py src/locale/mz800emu.pot cs
    python3 tools/i18n_translate.py src/locale/mz800emu.pot de

Závislosti:
    pip install anthropic

Proměnné prostředí:
    ANTHROPIC_API_KEY — API klíč pro Claude API
"""

import sys
import os
import re
import json
import time
from pathlib import Path

# ============================================================================
# Jazykové informace
# ============================================================================

LANGUAGES = {
    "cs": {"name": "Čeština", "english": "Czech",
           "plural_forms": 'nplurals=3; plural=(n==1) ? 0 : (n>=2 && n<=4) ? 1 : 2;'},
    "sk": {"name": "Slovenčina", "english": "Slovak",
           "plural_forms": 'nplurals=3; plural=(n==1) ? 0 : (n>=2 && n<=4) ? 1 : 2;'},
    "ja": {"name": "日本語", "english": "Japanese",
           "plural_forms": 'nplurals=1; plural=0;'},
    "de": {"name": "Deutsch", "english": "German",
           "plural_forms": 'nplurals=2; plural=(n != 1);'},
    "it": {"name": "Italiano", "english": "Italian",
           "plural_forms": 'nplurals=2; plural=(n != 1);'},
    "es": {"name": "Español", "english": "Spanish",
           "plural_forms": 'nplurals=2; plural=(n != 1);'},
    "nl": {"name": "Nederlands", "english": "Dutch",
           "plural_forms": 'nplurals=2; plural=(n != 1);'},
    "fr": {"name": "Français", "english": "French",
           "plural_forms": 'nplurals=2; plural=(n > 1);'},
    "pl": {"name": "Polski", "english": "Polish",
           "plural_forms": 'nplurals=3; plural=(n==1) ? 0 : (n%10>=2 && n%10<=4 && (n%100<12 || n%100>14)) ? 1 : 2;'},
    "uk": {"name": "Українська", "english": "Ukrainian",
           "plural_forms": 'nplurals=3; plural=(n==1) ? 0 : (n>=2 && n<=4) ? 1 : 2;'},
}

# velikost dávky pro API volání
BATCH_SIZE = 30

# odhad tokenů per string (vstup + výstup)
TOKENS_PER_STRING = 100

# cena per 1M tokenů (Claude Sonnet 4)
PRICE_PER_1M_INPUT = 3.0
PRICE_PER_1M_OUTPUT = 15.0

# ============================================================================
# Jednoduchý PO/POT parser (bez závislosti na polib)
# ============================================================================

class POEntry:
    """Jeden záznam v PO souboru."""
    def __init__(self):
        self.comments = []          # # komentáře
        self.references = []        # #: reference
        self.flags = []             # #, flags
        self.translator_comment = []  # #. translator comment
        self.msgctxt = ""
        self.msgid = ""
        self.msgstr = ""
        self.is_fuzzy = False
        self.is_obsolete = False

    @property
    def translated(self):
        return bool(self.msgstr) and not self.is_fuzzy


class POFile:
    """Jednoduchý parser pro .po/.pot soubory."""
    def __init__(self):
        self.header = ""
        self.entries = []

    @staticmethod
    def parse(filepath):
        """Parsuje PO/POT soubor."""
        pf = POFile()
        with open(filepath, "r", encoding="utf-8") as f:
            content = f.read()

        # Rozdělení na bloky (oddělené prázdnými řádky)
        blocks = re.split(r'\n\n+', content.strip())

        for block in blocks:
            entry = POEntry()
            lines = block.split('\n')

            # Parsování řádků
            current_key = None
            for line in lines:
                if line.startswith('#, '):
                    flags = line[3:].split(', ')
                    entry.flags = flags
                    entry.is_fuzzy = 'fuzzy' in flags
                elif line.startswith('#: '):
                    entry.references.append(line[3:])
                elif line.startswith('#. '):
                    entry.translator_comment.append(line[3:])
                elif line.startswith('#~ '):
                    entry.is_obsolete = True
                elif line.startswith('# '):
                    entry.comments.append(line[2:])
                elif line.startswith('#'):
                    entry.comments.append(line[1:])
                elif line.startswith('msgctxt '):
                    current_key = 'msgctxt'
                    entry.msgctxt = _unquote(line[8:])
                elif line.startswith('msgid '):
                    current_key = 'msgid'
                    entry.msgid = _unquote(line[6:])
                elif line.startswith('msgstr '):
                    current_key = 'msgstr'
                    entry.msgstr = _unquote(line[7:])
                elif line.startswith('"') and current_key:
                    val = _unquote(line)
                    if current_key == 'msgctxt':
                        entry.msgctxt += val
                    elif current_key == 'msgid':
                        entry.msgid += val
                    elif current_key == 'msgstr':
                        entry.msgstr += val

            # Přidání záznamu
            if entry.msgid == "" and entry.msgstr:
                # Header záznam
                pf.header = entry.msgstr
            elif entry.msgid and not entry.is_obsolete:
                pf.entries.append(entry)

        return pf

    def save(self, filepath, lang_code, lang_info):
        """Zapíše PO soubor."""
        with open(filepath, "w", encoding="utf-8") as f:
            # Header
            f.write(_make_header(lang_code, lang_info))
            f.write("\n")

            for entry in self.entries:
                # Komentáře
                for c in entry.translator_comment:
                    f.write(f"#. {c}\n")
                for r in entry.references:
                    f.write(f"#: {r}\n")
                if entry.flags:
                    f.write(f"#, {', '.join(entry.flags)}\n")

                # Kontext
                if entry.msgctxt:
                    f.write(f"msgctxt {_quote(entry.msgctxt)}\n")

                # Zdroj a překlad
                f.write(f"msgid {_quote(entry.msgid)}\n")
                f.write(f"msgstr {_quote(entry.msgstr)}\n")
                f.write("\n")


def _unquote(s):
    """Odstraní uvozovky z PO řetězce."""
    s = s.strip()
    if s.startswith('"') and s.endswith('"'):
        s = s[1:-1]
    # Unescape
    s = s.replace('\\n', '\n')
    s = s.replace('\\t', '\t')
    s = s.replace('\\"', '"')
    s = s.replace('\\\\', '\\')
    return s


def _quote(s):
    """Obalí řetězec uvozovkami pro PO formát."""
    s = s.replace('\\', '\\\\')
    s = s.replace('"', '\\"')
    s = s.replace('\t', '\\t')

    # Víceřádkové stringy
    if '\n' in s:
        parts = s.split('\n')
        result = '""\n'
        for i, part in enumerate(parts):
            if i < len(parts) - 1:
                result += f'"{part}\\n"\n'
            elif part:  # poslední neprázdný
                result += f'"{part}"\n'
        return result.rstrip('\n')

    return f'"{s}"'


def _make_header(lang_code, lang_info):
    """Vytvoří PO header."""
    return f'''msgid ""
msgstr ""
"Project-Id-Version: mz800emu 1.0\\n"
"Report-Msgid-Bugs-To: \\n"
"POT-Creation-Date: 2026-03-11\\n"
"PO-Revision-Date: 2026-03-11\\n"
"Last-Translator: Claude AI <noreply@anthropic.com>\\n"
"Language-Team: {lang_info["english"]}\\n"
"Language: {lang_code}\\n"
"MIME-Version: 1.0\\n"
"Content-Type: text/plain; charset=UTF-8\\n"
"Content-Transfer-Encoding: 8bit\\n"
"Plural-Forms: {lang_info["plural_forms"]}\\n"
'''


# ============================================================================
# Validace
# ============================================================================

def get_format_specifiers(s):
    """Extrahuje formátovací specifikátory z řetězce."""
    return re.findall(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[diouxXeEfFgGaAcspn%]', s)


def validate_translation(msgid, msgstr):
    """Ověří, že překlad zachovává formátovací specifikátory."""
    if not msgstr:
        return True  # prázdný překlad je OK (zůstane anglicky)

    orig_specs = get_format_specifiers(msgid)
    trans_specs = get_format_specifiers(msgstr)

    if orig_specs != trans_specs:
        return False
    return True


# ============================================================================
# Claude API překlad
# ============================================================================

def create_translation_prompt(strings, lang_code, lang_info):
    """Vytvoří prompt pro Claude API."""
    lang_name = lang_info["english"]

    # Speciální instrukce pro japonštinu
    extra = ""
    if lang_code == "ja":
        extra = """
- Technické termíny piš katakana (např. スナップショット pro snapshot), ne kanji
- ROM, RAM, CPU, VRAM, EXVRAM zůstávají v latince"""

    prompt = f"""You are a professional software translator.
You are translating UI strings for an 8-bit computer emulator (Sharp MZ-800/MZ-700/MZ-1500).

Rules:
1. Preserve format specifiers (%s, %d, %u, %.2f, %%, %lu, etc.) EXACTLY as they are
2. Preserve & characters for keyboard accelerators (if present)
3. For buttons and menus: use concise, clear translations
4. For error messages: accurate, user-friendly translation
5. Technical terms (ROM, RAM, CPU, snapshot, emulator, VRAM, EXVRAM, CMT, FDC, PSG) — do NOT translate
6. "Quick Save/Load" — use the established term in {lang_name}
7. Respond ONLY as a JSON array: [{{"msgid": "...", "msgstr": "..."}}]
8. Do NOT add any explanation or markdown formatting — just the JSON array{extra}

Translate the following UI strings to {lang_name}:

"""
    for s in strings:
        prompt += f'  "{s}"\n'

    return prompt


def translate_batch(client, strings, lang_code, lang_info):
    """Přeloží dávku stringů přes Claude API."""
    prompt = create_translation_prompt(strings, lang_code, lang_info)

    response = client.messages.create(
        model="claude-sonnet-4-20250514",
        max_tokens=4096,
        messages=[{"role": "user", "content": prompt}]
    )

    # Parsování JSON odpovědi
    text = response.content[0].text.strip()

    # Odstranění případného markdown wrapperu
    if text.startswith("```"):
        text = re.sub(r'^```(?:json)?\n?', '', text)
        text = re.sub(r'\n?```$', '', text)
        text = text.strip()

    try:
        translations = json.loads(text)
    except json.JSONDecodeError as e:
        print(f"\n  CHYBA: nepodařilo se parsovat JSON odpověď: {e}")
        print(f"  Odpověď: {text[:200]}...")
        return {}

    # Převod na slovník msgid -> msgstr
    result = {}
    for item in translations:
        msgid = item.get("msgid", "")
        msgstr = item.get("msgstr", "")
        if msgid and msgstr:
            result[msgid] = msgstr

    return result


# ============================================================================
# Hlavní logika
# ============================================================================

def estimate_cost(num_strings):
    """Odhadne cenu API volání."""
    num_batches = (num_strings + BATCH_SIZE - 1) // BATCH_SIZE
    # Odhad: ~50 tokenů vstup per string, ~30 tokenů výstup per string
    input_tokens = num_strings * 50 + num_batches * 200  # systémový prompt
    output_tokens = num_strings * 30
    cost = (input_tokens / 1_000_000 * PRICE_PER_1M_INPUT +
            output_tokens / 1_000_000 * PRICE_PER_1M_OUTPUT)
    return num_batches, cost


def main():
    if len(sys.argv) < 3:
        print("Použití: python3 tools/i18n_translate.py <pot_soubor> <jazyk>")
        print(f"Podporované jazyky: {', '.join(LANGUAGES.keys())}")
        sys.exit(1)

    pot_path = sys.argv[1]
    lang_code = sys.argv[2]

    if lang_code not in LANGUAGES:
        print(f"CHYBA: neznámý jazyk '{lang_code}'")
        print(f"Podporované: {', '.join(LANGUAGES.keys())}")
        sys.exit(1)

    lang_info = LANGUAGES[lang_code]

    if not os.path.exists(pot_path):
        print(f"CHYBA: soubor '{pot_path}' neexistuje")
        sys.exit(1)

    # Parsování .pot souboru
    pot = POFile.parse(pot_path)
    total_strings = len(pot.entries)

    # Zjistit existující .po soubor
    locale_dir = os.path.dirname(pot_path)
    po_path = os.path.join(locale_dir, lang_code, "LC_MESSAGES",
                           os.path.basename(pot_path).replace(".pot", ".po"))

    already_translated = 0
    fuzzy_count = 0
    untranslated_count = 0

    if os.path.exists(po_path):
        existing_po = POFile.parse(po_path)
        # Vytvořit slovník existujících překladů
        existing = {}
        for e in existing_po.entries:
            if e.translated:
                existing[e.msgid] = e.msgstr
                already_translated += 1
            elif e.is_fuzzy:
                fuzzy_count += 1
            else:
                untranslated_count += 1

        # Aplikovat existující překlady na .pot záznamy
        for entry in pot.entries:
            if entry.msgid in existing:
                entry.msgstr = existing[entry.msgid]
                entry.is_fuzzy = False
    else:
        untranslated_count = total_strings

    # Spočítání stringů k překladu
    to_translate = [e for e in pot.entries if not e.translated]
    num_to_translate = len(to_translate)

    # Zobrazení souhrnu a žádost o schválení
    num_batches, estimated_cost = estimate_cost(num_to_translate)

    print(f"\n=== Analýza překladu: {lang_code} ({lang_info['name']}) ===\n")
    print(f"  Celkem stringů v .pot:    {total_strings}")
    print(f"  Již přeložených:          {already_translated}")
    print(f"  Fuzzy (ke kontrole):      {fuzzy_count}")
    print(f"  Nepřeložených:            {untranslated_count}")
    print(f"  {'-' * 33}")
    print(f"  K překladu:               {num_to_translate}  (fuzzy + nepřeložené)")
    print(f"  Odhadované API volání:    {num_batches}  (dávky po {BATCH_SIZE})")
    print(f"  Odhadovaná cena:          ~${estimated_cost:.2f}")

    if num_to_translate == 0:
        print(f"\n  Překlad {lang_code} je kompletní — nic k překladu.")
        return

    # Varování při prvním překladu
    if already_translated == 0 and num_to_translate == total_strings:
        print(f"\n  *** PRVNÍ PŘEKLAD — bude přeloženo všech {total_strings} stringů! ***")

    # Interaktivní schválení — POVINNÉ
    try:
        answer = input(f"\n  Pokračovat? [y/N] ").strip().lower()
    except EOFError:
        print("\n  CHYBA: neinteraktivní režim není povolen")
        sys.exit(1)

    if answer != 'y':
        print("  Zrušeno.")
        return

    # Kontrola API klíče — až po schválení
    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if not api_key:
        print("\nCHYBA: proměnná prostředí ANTHROPIC_API_KEY není nastavena")
        print("  export ANTHROPIC_API_KEY='sk-ant-...'")
        sys.exit(1)

    # Import anthropic SDK
    try:
        import anthropic
    except ImportError:
        print("\nCHYBA: balíček 'anthropic' není nainstalován")
        print("  pip install anthropic")
        sys.exit(1)

    client = anthropic.Anthropic(api_key=api_key)

    # Překládání po dávkách
    strings_to_translate = [e.msgid for e in to_translate]
    translations = {}
    total_batches = (len(strings_to_translate) + BATCH_SIZE - 1) // BATCH_SIZE

    for i in range(0, len(strings_to_translate), BATCH_SIZE):
        batch = strings_to_translate[i:i + BATCH_SIZE]
        batch_num = i // BATCH_SIZE + 1
        print(f"\n  Dávka {batch_num}/{total_batches} ({len(batch)} stringů)...", end="", flush=True)

        try:
            result = translate_batch(client, batch, lang_code, lang_info)
            translations.update(result)
            print(f" OK ({len(result)} přeloženo)")
        except Exception as e:
            print(f" CHYBA: {e}")
            continue

        # Rate limiting — pauza mezi dávkami
        if i + BATCH_SIZE < len(strings_to_translate):
            time.sleep(1)

    # Aplikace překladů
    translated_count = 0
    validation_errors = 0

    for entry in pot.entries:
        if entry.msgid in translations:
            msgstr = translations[entry.msgid]

            # Validace formátovacích specifikátorů
            if validate_translation(entry.msgid, msgstr):
                entry.msgstr = msgstr
                entry.is_fuzzy = False
                entry.flags = [f for f in entry.flags if f != 'fuzzy']
                translated_count += 1
            else:
                orig_specs = get_format_specifiers(entry.msgid)
                trans_specs = get_format_specifiers(msgstr)
                print(f"\n  VALIDACE SELHALA: '{entry.msgid}'")
                print(f"    Originál: {orig_specs}")
                print(f"    Překlad:  {trans_specs} -> '{msgstr}'")
                validation_errors += 1

    # Uložení .po souboru
    os.makedirs(os.path.dirname(po_path), exist_ok=True)
    pot.save(po_path, lang_code, lang_info)

    # Souhrn
    total_translated = sum(1 for e in pot.entries if e.translated)

    print(f"\n{'=' * 40}")
    print(f"  Přeloženo:           {translated_count} stringů")
    print(f"  Validační chyby:     {validation_errors}")
    print(f"  Celkem přeloženo:    {total_translated}/{total_strings}")
    print(f"  Uloženo:             {po_path}")
    print(f"{'=' * 40}\n")


if __name__ == "__main__":
    main()
