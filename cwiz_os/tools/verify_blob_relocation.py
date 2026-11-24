#!/usr/bin/env python3
"""Verify a relocation performed by relocate_blob.py.

Checks, for every entry in the JSON relocation map:

  1. the ORIGINAL instruction at the recorded address really is the claimed
     absolute B/BL (or MOV-pair ROM-data reference) and its old target/data
     address matches the map;
  2. the RELOCATED instruction at the same address decodes to the same mnemonic
     with the new bank/offset, and the new target lands on a valid instruction
     instruction boundary of the relocated image (the target maps back to a
     valid boundary of the image).
  3. NO byte outside the map's declared patch set differs between the original
     and relocated images (this catches accidental corruption of data tables,
     RAM/SFR constants or unrelated instructions).

Exit status is 0 on success, 1 on any failure.  With ``--json`` a machine-readable
report is printed instead of the human summary.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_DECODER_DIR = REPO / "firmware-re" / "fx580" / "tools"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from relocate_blob import (  # noqa: E402
    decode_image,
    is_absolute_branch,
    load_decoder,
    mov_reg,
    parse_mov_imm,
)


def verify(orig: bytes, reloc: bytes, rmap: dict, *,
           decoder_dir: Path | None = None) -> dict:
    nxu16 = load_decoder(decoder_dir or DEFAULT_DECODER_DIR)
    problems: list[str] = []
    if len(orig) != len(reloc):
        problems.append(f"size {len(orig)} != {len(reloc)}")
        return {"ok": False, "problems": problems}
    if rmap.get("orig_size") != len(orig):
        problems.append(
            f"map orig_size {rmap.get('orig_size')} != {len(orig)}"
        )

    orig_i = decode_image(orig, nxu16)
    reloc_i = decode_image(reloc, nxu16)
    orig_b, reloc_b = set(orig_i), set(reloc_i)

    bank = (rmap.get("base", 0) >> 16) & 0xF
    delta = rmap.get("base", 0) & 0xFFFF
    if bank != rmap.get("target_bank"):
        problems.append(f"map bank {rmap.get('target_bank')} != base bank {bank}")
    if delta != rmap.get("offset_delta"):
        problems.append(
            f"map offset delta {rmap.get('offset_delta')} != base offset {delta}"
        )

    expected_changed: set[int] = set()
    checked = {"branch": 0, "data_ref": 0}
    boundary_hits = 0

    for ent in rmap.get("relocations", []):
        kind = ent.get("kind")
        a = ent.get("addr")
        if not isinstance(a, int) or a not in orig_b or a not in reloc_b:
            problems.append(f"entry {ent}: address is not an instruction boundary")
            continue
        oi, ri = orig_i[a], reloc_i[a]

        if kind == "branch":
            checked["branch"] += 1
            if not is_absolute_branch(orig, a, oi):
                problems.append(f"0x{a:X}: original is not an absolute B/BL")
                continue
            if not is_absolute_branch(reloc, a, ri):
                problems.append(f"0x{a:X}: relocated is not an absolute B/BL")
                continue
            if oi.mnemonic != ri.mnemonic:
                problems.append(
                    f"0x{a:X}: mnemonic changed {oi.mnemonic} -> {ri.mnemonic}"
                )
            old_off = orig[a + 2] | (orig[a + 3] << 8)
            new_bank = reloc[a + 1] & 0x0F
            new_off = reloc[a + 2] | (reloc[a + 3] << 8)
            if old_off != ent.get("old_offset") or old_off != ent.get("old_target"):
                problems.append(
                    f"0x{a:X}: old target 0x{old_off:X} != map {ent.get('old_offset')}"
                )
            if new_bank != bank or new_off != old_off + delta:
                problems.append(
                    f"0x{a:X}: new target {new_bank}:{new_off:04X} != "
                    f"{bank}:{old_off + delta:04X}"
                )
            if new_off != ent.get("new_offset") or (bank << 16 | new_off) != ent.get("new_target"):
                problems.append(f"0x{a:X}: map new target does not match patched bytes")
            if old_off not in orig_b:
                problems.append(f"0x{a:X}: OLD target 0x{old_off:X} is not a boundary")
            # Target fields are bank-relative *runtime* addresses.  The blob is
            # loaded at 0x0E40 inside bank 3, so a patched target ``new_off``
            # fetches blob file offset ``new_off - delta``.  Instruction bytes
            # did not move, so that file offset must be a boundary of the image.
            dest_file_off = new_off - delta
            if dest_file_off != old_off:
                problems.append(
                    f"0x{a:X}: patched target maps to file offset 0x{dest_file_off:X}, "
                    f"not the original 0x{old_off:X}"
                )
            if dest_file_off not in reloc_b:
                problems.append(
                    f"0x{a:X}: NEW target 0x{new_off:X} maps to 0x{dest_file_off:X}, "
                    "which is not an instruction boundary"
                )
            else:
                boundary_hits += 1
            # byte0 (B/BL opcode) must be unchanged; bytes 1..3 are the patch.
            if orig[a] != reloc[a]:
                problems.append(f"0x{a:X}: branch opcode byte changed")
            expected_changed.update((a + 1, a + 2, a + 3))

        elif kind == "data_ref":
            checked["data_ref"] += 1
            b = ent.get("hi_addr")
            if not isinstance(b, int) or b not in orig_b or b not in reloc_b:
                problems.append(f"0x{a:X}: data pair hi address is not a boundary")
                continue
            o_lo, o_hi = parse_mov_imm(oi), parse_mov_imm(orig_i[b])
            r_lo, r_hi = parse_mov_imm(ri), parse_mov_imm(reloc_i[b])
            r0, r1 = mov_reg(oi), mov_reg(orig_i[b])
            if None in (o_lo, o_hi, r_lo, r_hi, r0, r1):
                problems.append(f"0x{a:X}: original is not a MOV Rn/Rn+1 pair")
                continue
            old = o_lo | (o_hi << 8)
            new = r_lo | (r_hi << 8)
            if old != ent.get("old_addr"):
                problems.append(f"0x{a:X}: old data addr 0x{old:X} != map")
            if new != old + delta or new != ent.get("new_addr"):
                problems.append(
                    f"0x{a:X}: new data addr 0x{new:X} != 0x{old + delta:X} / map"
                )
            ds = rmap.get("data_start", 0)
            if not (ds + delta <= new < len(reloc) + delta):
                problems.append(f"0x{a:X}: data target 0x{new:X} outside relocated data")
            expected_changed.update((a, b))
        else:
            problems.append(f"entry {ent}: unknown kind {kind!r}")

    # Byte-level completeness: nothing changed outside the declared patches.
    actual_changed = {i for i in range(len(orig)) if orig[i] != reloc[i]}
    extra = actual_changed - expected_changed
    missing = expected_changed - actual_changed
    if extra:
        sample = ", ".join(f"0x{x:X}" for x in sorted(extra)[:16])
        problems.append(
            f"{len(extra)} bytes changed outside the relocation map: {sample}"
        )
    if missing:
        sample = ", ".join(f"0x{x:X}" for x in sorted(missing)[:16])
        problems.append(
            f"{len(missing)} declared patch bytes were not changed: {sample}"
        )

    declared_total = rmap.get("counts", {}).get("total")
    if declared_total is not None and declared_total != sum(checked.values()):
        problems.append(
            f"map declares {declared_total} relocations, verified {sum(checked.values())}"
        )

    return {
        "ok": not problems,
        "problems": problems,
        "checked": checked,
        "boundary_hits": boundary_hits,
        "bytes_changed": len(actual_changed),
        "expected_changed": len(expected_changed),
        "orig_size": len(orig),
        "reloc_size": len(reloc),
        "base": rmap.get("base"),
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--blob", type=Path, required=True, help="original compiled-at-0 blob")
    ap.add_argument("--relocated", type=Path, required=True, help="relocated blob")
    ap.add_argument("--map", type=Path, required=True, help="relocate_blob.py JSON map")
    ap.add_argument("--decoder-dir", type=Path, default=DEFAULT_DECODER_DIR)
    ap.add_argument("--json", action="store_true", help="emit a JSON report")
    args = ap.parse_args()

    orig = args.blob.read_bytes()
    reloc = args.relocated.read_bytes()
    rmap = json.loads(args.map.read_text(encoding="utf-8"))
    report = verify(orig, reloc, rmap, decoder_dir=args.decoder_dir)

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        c = report["checked"]
        print(
            f"Relocation verification: "
            f"{'PASS' if report['ok'] else 'FAIL'} — "
            f"{c['branch']} branches, {c['data_ref']} data refs, "
            f"{report['boundary_hits']} target boundaries confirmed, "
            f"{report['bytes_changed']} bytes changed "
            f"(expected {report['expected_changed']})."
        )
        for p in report["problems"][:40]:
            print("  FAIL:", p)
    raise SystemExit(0 if report["ok"] else 1)


if __name__ == "__main__":
    main()
