#!/usr/bin/env python3
"""Bank-aware relocator for a CWiz blob that was compiled at origin 0.

The recovered-firmware blob placed by ``cwiz_os`` is built with
``wizc --target cw``: every absolute ``B``/``BL`` target is a bank-0 offset and
every ROM-data constant is a bank-0 16-bit address.  Copying that image to
``3:0E40`` therefore produces a *preserved artifact*, not executable code: the
first absolute branch would jump back to bank 0.

This tool rewrites those two classes of absolute address so the image becomes
self-consistent at a new bank/offset:

  * absolute ``B``/``BL`` (opcode ``0xF000``/``0xF001`` + one long-immediate
    word): the bank nibble in the opcode becomes ``base >> 16`` and the 16-bit
    offset gains ``base & 0xFFFF``;
  * ``MOV Rn,#lo`` / ``MOV R(n+1),#hi`` pairs that materialise a 16-bit ROM data
    address: the combined address gains the same offset delta.

Instruction boundaries are recovered with the *exact* nX-U16/100 decoder from
``firmware-re/fx580/tools/nxu16.py`` (generated from the emulator opcode
table).  A linear decode of the compiled image covers it exactly, so a byte
pattern is only patched when it sits on a real instruction boundary and decodes
as the instruction we expect.  RAM/SFR addresses (0xD000..0xFFFF) and numeric
constants are never touched.

Scope / honest limitation: the compiler's ``cwapp`` target emits ``DSRI #3``
immediately before each ROM-data load because the emulator's DSR applies to one
instruction only.  This tool cannot insert bytes without moving every later
instruction, so it fixes data *offsets* but does not add bank-select prefixes.
Functions that read ROM data therefore still need a real compiler/linker change
(see ``docs/blob-relocation.md``).  Leaf functions that touch only registers,
RAM and SFRs are fully relocatable and executable.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

# Reuse the read-only authoritative decoder (same repo).  parents[2] is the
# repository root (/Volumes/Code/Wiz) for cwiz_os/tools/relocate_blob.py.
REPO = Path(__file__).resolve().parents[2]
DEFAULT_DECODER_DIR = REPO / "firmware-re" / "fx580" / "tools"


def load_decoder(decoder_dir: Path):
    """Import the exact nX-U16 decoder, which outlives any single build."""
    d = decoder_dir.resolve()
    if str(d) not in sys.path:
        sys.path.insert(0, str(d))
    try:
        import nxu16  # type: ignore
    except ImportError as exc:  # pragma: no cover - environment failure
        raise SystemExit(
            f"cannot import nxu16.py from {d}; pass --decoder-dir. ({exc})"
        ) from exc
    return nxu16


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def decode_image(blob: bytes, nxu16):
    """Linear decode of an image compiled at origin 0.

    Returns ``{addr: Insn}``.  Refuses an image the decoder cannot cover exactly
    (a partial last instruction means the input is not a compiler output).
    """
    insns: dict[int, object] = {}
    addr = 0
    n = len(blob)
    while addr < n:
        if addr + 2 > n:
            raise SystemExit(
                f"incomplete instruction at 0x{addr:X}: blob ends at 0x{n:X}"
            )
        ins = nxu16.decode(blob, addr)
        if addr + ins.size > n:
            raise SystemExit(
                f"instruction at 0x{addr:X} size {ins.size} overruns blob end 0x{n:X}"
            )
        insns[addr] = ins
        addr += ins.size
    return insns


def is_absolute_branch(blob: bytes, addr: int, ins) -> bool:
    """True for a genuine absolute B/BL at ``addr``.

    Encoding (asm.cpp, cwmode.cpp, emulator CPU.cpp): first word
    ``0xF000 | (bank<<8) | (1 for BL)`` stored little-endian, then the 16-bit
    offset word.  byte0 is therefore 0 (B) or 1 (BL); byte1 is 0xF0|bank.
    Requiring byte0 in {0,1} rejects data words such as 0xF870, which the
    generic decoder would otherwise render as a bogus ``B 8:xxxx``.
    """
    if ins.size != 4 or ins.handler not in ("B", "BL") or not (ins.hint & 0x20):
        return False
    if addr + 4 > len(blob):
        return False
    return blob[addr] in (0x00, 0x01) and 0xF0 <= blob[addr + 1] <= 0xFF


def parse_mov_imm(ins) -> int | None:
    """Immediate of ``MOV Rn,#xxH``; None when the operand is not an 8-bit imm."""
    import re

    if ins.handler != "MOV":  # nxu16 renders MOV16 as "MOV" but handler differs
        return None
    m = re.search(r"#([0-9A-Fa-f]+)H", ins.operands)
    return int(m.group(1), 16) if m else None


def mov_reg(ins) -> int | None:
    """Destination register number of ``MOV Rn,#imm``."""
    import re

    m = re.match(r"R(\d+)\b", ins.operands)
    return int(m.group(1)) if m else None


def detect_data_start(blob: bytes, insns: dict) -> int:
    """Find the first ROM-data address in the image.

    The compiler emits every 16-bit ROM address as ``MOV Rn,#lo`` /
    ``MOV R(n+1),#hi``.  Small constants (0, 0xFF, 0x142, ...) and genuine data
    addresses form two clusters; the widest gap between consecutive pair values
    separates them.  For the shipped blob that boundary is 0x6EE8, the first
    string table.  An explicit ``--data-start`` overrides this.
    """
    addrs = sorted(insns)
    values: set[int] = set()
    for k, a in enumerate(addrs[:-1]):
        i = insns[a]
        b = addrs[k + 1]
        if b != a + i.size:
            continue
        j = insns[b]
        lo, hi = parse_mov_imm(i), parse_mov_imm(j)
        r0, r1 = mov_reg(i), mov_reg(j)
        if (lo is None or hi is None or r0 is None or r1 is None
                or r1 != r0 + 1 or r0 % 2):
            continue
        v = lo | (hi << 8)
        if 0 <= v < len(blob):
            values.add(v)
    sv = sorted(values)
    if len(sv) < 2:
        raise SystemExit(
            "cannot auto-detect ROM data start (no 16-bit address pairs); "
            "pass --data-start explicitly"
        )
    # Largest gap between consecutive values; take its upper end as data start.
    best_gap, boundary = -1, sv[-1]
    for x, y in zip(sv, sv[1:]):
        if y - x > best_gap:
            best_gap, boundary = y - x, y
    if boundary < len(blob) // 2:
        raise SystemExit(
            f"auto-detected data start 0x{boundary:X} is below half the blob; "
            "pass --data-start explicitly"
        )
    return boundary


def relocate(blob: bytes, blob_base: int, *, decoder_dir: Path | None = None,
             data_start: int | None = None) -> tuple[bytes, dict]:
    """Return (relocated_bytes, relocation_map) for a compiled-at-0 blob."""
    nxu16 = load_decoder(decoder_dir or DEFAULT_DECODER_DIR)
    n = len(blob)
    if n < 4:
        raise SystemExit("blob is too small to be a compiled CWiz image")

    bank = (blob_base >> 16) & 0xF
    delta = blob_base & 0xFFFF
    if delta == 0 and bank == 0:
        raise SystemExit("blob base 0:0000 needs no relocation")

    insns = decode_image(blob, nxu16)
    addrs = sorted(insns)
    boundaries = set(addrs)
    data_start = detect_data_start(blob, insns) if data_start is None else data_start
    if not 0 <= data_start < n:
        raise SystemExit(f"--data-start 0x{data_start:X} is outside the blob")
    if delta + n - 1 > 0xFFFF:
        raise SystemExit(
            f"blob base 0x{blob_base:X} would push offset 0x{n - 1:X} past 0xFFFF"
        )

    out = bytearray(blob)
    entries: list[dict] = []
    n_b = n_bl = n_data = 0
    skipped_patterns = 0       # data bytes misdecoded as bank!=0 branches
    skipped_external = 0       # genuine branches to stock ROM (stay bank 0)

    # --- absolute B / BL ---------------------------------------------------
    for a in addrs:
        ins = insns[a]
        if not is_absolute_branch(blob, a, ins):
            continue
        old_bank = blob[a + 1] & 0x0F
        old_off = blob[a + 2] | (blob[a + 3] << 8)
        if old_bank != 0:
            # A wizc --target cw image never emits a bank!=0 absolute B/BL:
            # with origin 0 every code address is 0:xxxx.  Patterns with a
            # non-zero nibble are font/table bytes that the linear decoder
            # renders as a bogus branch.  Leave them untouched.
            skipped_patterns += 1
            continue
        if old_off >= n:
            # External target (e.g. __reset -> B 0:A1CE, the stock reset stub).
            # It belongs to the stock ROM, not the blob, so it must NOT move.
            skipped_external += 1
            continue
        if old_off not in boundaries:
            raise SystemExit(
                f"0x{a:X}: branch target 0x{old_off:X} is not an instruction "
                "boundary; refusing to corrupt data"
            )
        new_off = old_off + delta
        if new_off not in {x + delta for x in boundaries}:
            raise SystemExit(f"0x{a:X}: relocated target 0x{new_off:X} is invalid")
        out[a + 1] = 0xF0 | bank
        out[a + 2] = new_off & 0xFF
        out[a + 3] = (new_off >> 8) & 0xFF
        mnem = "BL" if blob[a] == 0x01 else "B"
        if mnem == "BL":
            n_bl += 1
        else:
            n_b += 1
        entries.append({
            "kind": "branch",
            "mnemonic": mnem,
            "addr": a,
            "old_target": old_off,
            "new_target": (bank << 16) | new_off,
            "old_offset": old_off,
            "new_offset": new_off,
            "old_bank": 0,
            "new_bank": bank,
            "patched_bytes": [a + 1, a + 2, a + 3],
        })

    # --- 16-bit ROM data addresses built from two MOV immediates ------------
    for k, a in enumerate(addrs[:-1]):
        b = addrs[k + 1]
        i, j = insns[a], insns[b]
        if b != a + i.size:
            continue
        lo, hi = parse_mov_imm(i), parse_mov_imm(j)
        r0, r1 = mov_reg(i), mov_reg(j)
        if (lo is None or hi is None or r0 is None or r1 is None
                or r1 != r0 + 1 or r0 % 2):
            continue
        old = lo | (hi << 8)
        if not (data_start <= old < n):
            continue  # numeric constant / code offset, not a ROM data address
        new = old + delta
        if not (data_start + delta <= new < n + delta):
            raise SystemExit(f"0x{a:X}: relocated data address 0x{new:X} is invalid")
        out[a] = new & 0xFF
        out[b] = (new >> 8) & 0xFF
        n_data += 1
        entries.append({
            "kind": "data_ref",
            "addr": a,
            "hi_addr": b,
            "reg": f"ER{r0}",
            "old_addr": old,
            "new_addr": new,
            "patched_bytes": [a, b],
        })

    entries.sort(key=lambda e: e["addr"])
    reloc = bytes(out)
    result = {
        "tool": "relocate_blob.py",
        "orig_size": n,
        "base": blob_base,
        "target_bank": bank,
        "offset_delta": delta,
        "data_start": data_start,
        "orig_sha256": sha256(blob),
        "relocated_sha256": sha256(reloc),
        "counts": {
            "B": n_b, "BL": n_bl, "branches": n_b + n_bl,
            "data_refs": n_data, "total": len(entries),
            "skipped_data_branch_patterns": skipped_patterns,
            "skipped_external_branches": skipped_external,
        },
        "note": ("16-bit data offsets patched; DSRI #3 prefixes for ROM-data "
                 "loads are NOT inserted (see docs/blob-relocation.md)"),
        "relocations": entries,
    }
    return reloc, result


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--blob", type=Path, required=True,
                    help="compiled-at-origin-0 blob (wizc --target cw)")
    ap.add_argument("--blob-base", type=lambda s: int(s, 0), default=0x30E40,
                    help="linear load address (default 0x30E40 = bank 3:0E40)")
    ap.add_argument("--out", type=Path, required=True, help="relocated blob output")
    ap.add_argument("--map-out", type=Path, help="JSON relocation map output")
    ap.add_argument("--data-start", type=lambda s: int(s, 0),
                    help="first ROM-data address in the original blob (auto-detected if omitted)")
    ap.add_argument("--decoder-dir", type=Path, default=DEFAULT_DECODER_DIR,
                    help=f"directory containing nxu16.py (default {DEFAULT_DECODER_DIR})")
    args = ap.parse_args()

    blob = args.blob.read_bytes()
    reloc, result = relocate(
        blob, args.blob_base, decoder_dir=args.decoder_dir, data_start=args.data_start
    )
    if len(reloc) != len(blob):
        raise SystemExit("internal error: relocation changed the blob size")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(reloc)
    map_path = args.map_out or args.out.with_suffix(args.out.suffix + ".map.json")
    map_path.parent.mkdir(parents=True, exist_ok=True)
    map_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    c = result["counts"]
    print(
        f"Relocated {len(blob)} bytes from 0:0000 to "
        f"{result['target_bank']}:{result['offset_delta']:04X} "
        f"(linear 0x{args.blob_base:X})."
    )
    print(
        f"  branches: {c['branches']} ({c['B']} B, {c['BL']} BL); "
        f"ROM data addresses: {c['data_refs']}; total: {c['total']}"
    )
    print(f"  data start detected: 0x{result['data_start']:X}")
    print(f"  SHA-256 origin : {result['orig_sha256']}")
    print(f"  SHA-256 relocated: {result['relocated_sha256']}")
    print(f"  relocated blob: {args.out}")
    print(f"  relocation map: {map_path}")


if __name__ == "__main__":
    main()
