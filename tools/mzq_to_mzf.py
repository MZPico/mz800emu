#!/usr/bin/env python3
"""
MZQ -> MZF extractor.

Vytahne MZF data z MZQ (QD image) wrapperu. Pouziva se pro
extrakci embedded MZQ konstant v emu zpet do MZF formy, aby
se s nima dalo dal pracovat jako s normalnimi MZF soubory.

DULEZITE: MZQ MZF_HEAD struct (qdisk.h) a standard Sharp MZ MZF header
maji RUZNY LAYOUT po bajtu 17 (= za fname terminatorem):

  MZQ MZF_HEAD struct                Standard Sharp MZ MZF
  ftype(1) + fname(16) + fname_end(1)   ftype(1) + fname[17]  (= shared)
  + unused1(2)                          (chybi)
  + size(2) + start(2) + exec(2)        size(2) + start(2) + exec(2)
  + description(38)                     comment(104)

MZQ ma navic 2-byte 'unused1' slot mezi terminatorem a size.
Extrakce MUSI tento slot vynechat a pole size/start/exec posunut
o 2 byty vlevo, jinak by se size/start/exec ulozil do "comment"
pozic v MZF a klient (loader na MZ) by precetl nesmysl.

Pouziti:
    tools/mzq_to_mzf.py input.mzq output.mzf

Algoritmus (standard MZF byty <- MZQ byty):
  MZF [0..17]  <- MZQ [15..32]   (ftype + fname + terminator, 18 B)
  MZF [18..19] <- MZQ [35..36]   (mzf_size LE - PRESKOK unused 33..34)
  MZF [20..21] <- MZQ [37..38]   (mzf_start LE)
  MZF [22..23] <- MZQ [39..40]   (mzf_exec LE)
  MZF [24..61] <- MZQ [41..78]   (comment prvnich 38 B z description[38])
  MZF [62..127] = 0x00           (padding na 128B header, zbytek comment
                                   ztracen - description v MZQ ma jen 38 B)
  MZF [128..]  <- MZQ [89..89+N]  (body, N = data_size z MZQ bytu 87..88)
"""
import sys


def mzq_to_mzf(mzq_data: bytes) -> bytes:
    if len(mzq_data) < 89:
        raise ValueError(f"MZQ too small: {len(mzq_data)} bytes")

    # Validace QD_HEADER start_sign
    if mzq_data[0:4] != b"\x00\x16\x16\xa5":
        raise ValueError(f"MZQ: bad QD start sign {mzq_data[0:4].hex()}")

    # Validace MZF_HEAD block start_sign + sign
    if mzq_data[8:12] != b"\x00\x16\x16\xa5":
        raise ValueError(f"MZQ: bad MZF_HEAD start sign {mzq_data[8:12].hex()}")
    if mzq_data[12] != 0x00:
        raise ValueError(f"MZQ: bad mzf_header_sign 0x{mzq_data[12]:02x}, expected 0x00")
    head_size = int.from_bytes(mzq_data[13:15], "little")
    if head_size != 64:
        raise ValueError(f"MZQ: unexpected MZF header size {head_size}, expected 64")

    # Validace MZF_BODY block (offset 82)
    if mzq_data[82:86] != b"\x00\x16\x16\xa5":
        raise ValueError(f"MZQ: bad MZF_BODY start sign {mzq_data[82:86].hex()}")
    if mzq_data[86] != 0x05:
        raise ValueError(f"MZQ: bad mzf_body_sign 0x{mzq_data[86]:02x}, expected 0x05")
    body_size = int.from_bytes(mzq_data[87:89], "little")
    if 89 + body_size + 3 != len(mzq_data):
        # informativni warning, ne fatal (CRC nebo padding na konci se muze lisit)
        sys.stderr.write(
            f"WARNING: declared body size {body_size} + overhead != file size "
            f"{len(mzq_data)} (expected {89+body_size+3})\n"
        )

    # Sestavit standard Sharp MZ MZF: 128 B header + body
    mzf = bytearray(128 + body_size)

    # MZF[0..17] <- MZQ[15..32] (ftype + fname + terminator)
    mzf[0:18] = mzq_data[15:33]
    # MZF[18..19] <- MZQ[35..36] (size) - skip unused1 na MZQ[33..34]
    mzf[18:20] = mzq_data[35:37]
    # MZF[20..21] <- MZQ[37..38] (start)
    mzf[20:22] = mzq_data[37:39]
    # MZF[22..23] <- MZQ[39..40] (exec)
    mzf[22:24] = mzq_data[39:41]
    # MZF[24..61] <- MZQ[41..78] (description[38] -> comment first 38 B)
    mzf[24:62] = mzq_data[41:79]
    # MZF[62..127] zustane 0x00 (padding na 128B header)

    # MZF body
    mzf[128:128 + body_size] = mzq_data[89:89 + body_size]

    return bytes(mzf)


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        sys.exit(2)
    src = sys.argv[1]
    dst = sys.argv[2]

    with open(src, "rb") as f:
        mzq_data = f.read()

    mzf_data = mzq_to_mzf(mzq_data)

    with open(dst, "wb") as f:
        f.write(mzf_data)

    print(f"Extracted {src} ({len(mzq_data)} B MZQ) -> {dst} ({len(mzf_data)} B MZF)")
    print(f"  MZF header: 128 B (z toho prvnich 64 B z MZQ, zbytek padding 0x00)")
    print(f"  MZF body:   {len(mzf_data) - 128} B")


if __name__ == "__main__":
    main()
