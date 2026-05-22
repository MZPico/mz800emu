#!/usr/bin/env python3
"""
MZF binary -> C array generator.

Vytvori MGR<name>_MZF.[ch] par ze vstupniho .mzf souboru ve formatu,
ktery pouziva emulator mz800emu pro embedded MGR data v
src/emulator/hw-generic/unicard/.

Output:
  - <symbol>_MZF.c - GPL header + #include <stdint.h> + const uint8_t array
  - <symbol>_MZF.h - GPL header + include guard + size #define + extern decl

Format konstanty:
    const uint8_t c_UNICARD_<symbol>_MZF[] = { 0xNN, 0xNN, ... };
    #define UNICARD_<symbol>_MZF_SIZE <bytes>

Layout:
  - 16 bajtu na radek
  - 38 mezer indentace pred prvnim 0xNN na radku
  - lower-case hex (0xab, ne 0xAB)
  - posledni radek bez koncove carky
  - CRLF line endings (konzistentni s existujicimi MGR*_MZF.c soubory)

Pouziti:
    tools/mzf_to_c.py <input.mzf> <symbol> <output_dir>

Priklad:
    tools/mzf_to_c.py /path/to/mgr800.mzf MGR800_V211B \\
                       src/emulator/hw-generic/unicard/
    # vytvori MGR800_V211B_MZF.c a MGR800_V211B_MZF.h v cilovem adresari
"""
import os
import sys


LICENSE_HEADER = """/*
 * File:   {filename}
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 *
 * ----------------------------- License -------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 */
"""

INDENT = " " * 38
BYTES_PER_ROW = 16


def render_c(symbol, data):
    """Vrati obsah .c souboru pro dany symbol a binarni data."""
    filename = f"{symbol}_MZF.c"
    lines = [LICENSE_HEADER.format(filename=filename), ""]
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("")
    lines.append("")
    lines.append("")
    lines.append(f"const uint8_t c_UNICARD_{symbol}_MZF [] = {{")

    total = len(data)
    for offset in range(0, total, BYTES_PER_ROW):
        chunk = data[offset:offset + BYTES_PER_ROW]
        hex_bytes = ", ".join(f"0x{b:02x}" for b in chunk)
        comma = "," if offset + BYTES_PER_ROW < total else ""
        lines.append(f"{INDENT}{hex_bytes}{comma}")

    lines.append("};")
    lines.append("")
    return "\r\n".join(lines)


def render_h(symbol, size):
    """Vrati obsah .h souboru pro dany symbol a velikost dat."""
    filename = f"{symbol}_MZF.h"
    guard = f"{symbol}_MZF_H"
    lines = [LICENSE_HEADER.format(filename=filename), ""]
    lines.append("")
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append('extern "C" {')
    lines.append("#endif")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define UNICARD_{symbol}_MZF_SIZE {size}")
    lines.append("")
    lines.append(f"    extern const uint8_t c_UNICARD_{symbol}_MZF[];")
    lines.append("")
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append("}")
    lines.append("#endif")
    lines.append("")
    lines.append(f"#endif /* {guard} */")
    lines.append("")
    return "\r\n".join(lines)


def main():
    if len(sys.argv) != 4:
        sys.stderr.write(__doc__)
        sys.exit(2)
    src = sys.argv[1]
    symbol = sys.argv[2]
    out_dir = sys.argv[3]

    with open(src, "rb") as f:
        data = f.read()
    size = len(data)

    c_path = os.path.join(out_dir, f"{symbol}_MZF.c")
    h_path = os.path.join(out_dir, f"{symbol}_MZF.h")

    # render_c a render_h vraci string s mixem LF (z LICENSE_HEADER
    # literalu) a CRLF (z join na lines listu). Normalizujeme nejdrive
    # vsechno na LF a pak globalne na CRLF aby byl celym soubor
    # konzistentni.
    def to_crlf(s):
        return s.replace("\r\n", "\n").replace("\n", "\r\n")

    with open(c_path, "wb") as f:
        f.write(to_crlf(render_c(symbol, data)).encode("ascii"))
    with open(h_path, "wb") as f:
        f.write(to_crlf(render_h(symbol, size)).encode("ascii"))

    print(f"Generated {c_path} ({size} bytes, {(size + BYTES_PER_ROW - 1) // BYTES_PER_ROW} rows)")
    print(f"Generated {h_path}")


if __name__ == "__main__":
    main()
