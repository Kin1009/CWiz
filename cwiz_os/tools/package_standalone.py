#!/usr/bin/env python3
"""Package the CWiz boot payload as a standalone fx-580 ROM/model.

The compiled program is placed at offset 0 of the stock image, so it owns the
bank-0 boot area.  Everything above the program must stay byte-identical to the
stock ROM unless a later section documents that it is safe to overlay.
"""

import argparse
import shutil
from pathlib import Path


ROM_SIZE = 0x40000
CHECKSUM_OFFSET = 0x3FFF6

# Bank-0 stock layout, taken from firmware-re/fx580:
#   docs/data-reconstruction.md  4 KiB message/format table at 0:710A
#   docs/keys.md                 default key map, 64 words at 0:7B0E
#   docs/rop-keymap.md           alternate key map, 64 words at 0:83DA
#   analysis/functions_v2.jsonl  first bank-0 function reachable from the
#                                reset stub 0:A1CE / stock main 2:93D8 is
#                                0:8BDE; every 0:8xxx..0:A1xx routine the stock
#                                reset path calls starts at or above it.
#
# (start, end, description, protected).  The lowest protected start is the
# hard payload limit; unprotected regions are reported when overlaid.
#
# The default key map is the first fixed table that the stock reset path
# (0:A1CE -> 2:93D8 -> getkeycode, docs/rop-keymap.md) reads and that a
# standalone image must therefore not overwrite.  The earlier "29436 bytes"
# figure was not a ROM boundary: 0x72FC falls in the middle of the
# message/format table, so it neither protected the key map nor described a
# real limit.  The message table itself is below that key map and is partially
# overlaid; the report below records the exact range.
STOCK_REGIONS = [
    (0x0000, 0x4000, "stock boot vectors and bank-0 boot code", False),
    (0x710A, 0x810A, "message/format table (docs/data-reconstruction.md)", False),
    (0x7B0E, 0x7B8E, "default key map, 64 words (docs/keys.md)", True),
    (0x83DA, 0x845A, "alternate key map, 64 words (docs/rop-keymap.md)", True),
    (0x8BDE, 0xA1CE, "bank-0 routines reachable from the reset stub (0:A1CE)", True),
    (0xA1CE, 0xA232, "stock reset stub (called by reset/recover)", True),
]
PAYLOAD_BYTES = min(start for start, _, _, protected in STOCK_REGIONS if protected)  # 0x7B0E


def fx580_checksum(image: bytearray) -> int:
    # Mirrors CasioEmuMsvc's CWX checksum: subtract 16-bit little-endian words.
    total = 0
    for start, end in ((0, 0xFC00), (0x10000, CHECKSUM_OFFSET)):
        for offset in range(start, end, 2):
            total = (total - int.from_bytes(image[offset : offset + 2], "little")) & 0xFFFF
    return total


def clobbered_regions(program_bytes: int):
    """Return (start, end, description, protected) for every stock region the
    program overwrites, clipped to the program's extent."""
    out = []
    for start, end, description, protected in STOCK_REGIONS:
        if start >= program_bytes:
            continue
        out.append((start, min(end, program_bytes), description, protected))
    return out


def print_clobber_report(program_bytes: int) -> None:
    ranges = clobbered_regions(program_bytes)
    if not ranges:
        print("Clobbered stock ranges: none")
        return
    print("Clobbered stock ranges:")
    for start, end, description, protected in ranges:
        marker = "PROTECTED" if protected else "by design"
        print(
            f"  {start:04X}..{end - 1:04X}  ({end - start:5d} B)  "
            f"[{marker}] {description}"
        )


def check_budget(program_bytes: int) -> None:
    if program_bytes <= PAYLOAD_BYTES:
        return
    overflow = program_bytes - PAYLOAD_BYTES
    lines = [
        f"payload overflow: {program_bytes} bytes (0x{program_bytes:X}) exceeds the "
        f"{PAYLOAD_BYTES}-byte (0x{PAYLOAD_BYTES:X}) budget by {overflow} bytes "
        f"(0x{overflow:X})",
        "the first protected stock region starts at 0x"
        f"{min(start for start, _, _, protected in STOCK_REGIONS if protected):04X}",
        "clobbered regions:",
    ]
    for start, end, description, protected in clobbered_regions(program_bytes):
        marker = "PROTECTED" if protected else "by design"
        lines.append(
            f"  {start:04X}..{end - 1:04X}  ({end - start} B)  [{marker}] {description}"
        )
    raise SystemExit("\n".join(lines))


def write_model_image(model: Path, image: bytes, ram_template: Path | None) -> bool:
    # The packaged config.bin names rom.bin; keep the legacy wiz/ copy too so
    # older model configs keep working.  Both files are the same patched image.
    (model / "rom.bin").write_bytes(image)
    wiz_dir = model / "wiz"
    wiz_dir.mkdir(exist_ok=True)
    (wiz_dir / "diagnostic-menu.bin").write_bytes(image)

    # The CWiz OS treats DBD0..DBDF ("0F 0E ... 01 00") as the RAM-initialised
    # canary and jumps to the stock reset stub when it is missing.  A cold
    # emulator RAM image has no canary, so a fresh model would boot the stock
    # firmware once.  Seed the model with the stock model's battery-backed RAM
    # (which has the canary) so the first `make run` boots CWiz.
    target_ram = model / "ram.dmp"
    if ram_template and ram_template.is_file():
        if ram_template.resolve() != target_ram.resolve():
            shutil.copy2(ram_template, target_ram)
        return True
    (model / "ram.dmp").unlink(missing_ok=True)
    return False


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True, type=Path)
    parser.add_argument("--base-rom", required=True, type=Path)
    parser.add_argument("--rom-out", required=True, type=Path)
    parser.add_argument("--model-template", required=True, type=Path)
    parser.add_argument("--model-out", required=True, type=Path)
    parser.add_argument(
        "--ram-template",
        type=Path,
        help="battery-backed RAM image to seed (default: ram.dmp beside --base-rom)",
    )
    args = parser.parse_args()

    program = args.program.read_bytes()
    base = args.base_rom.read_bytes()
    if len(base) != ROM_SIZE:
        raise SystemExit(f"base ROM must be {ROM_SIZE} bytes, got {len(base)}")
    if not program or len(program) >= CHECKSUM_OFFSET:
        raise SystemExit("CWiz boot payload is empty or overlaps the ROM metadata/checksum")

    check_budget(len(program))
    print(
        f"Payload budget: {len(program)} / {PAYLOAD_BYTES} bytes "
        f"(0x{len(program):X} / 0x{PAYLOAD_BYTES:X}), "
        f"{PAYLOAD_BYTES - len(program)} bytes free"
    )
    print_clobber_report(len(program))

    image = bytearray(base)
    image[: len(program)] = program
    checksum = fx580_checksum(image)
    image[CHECKSUM_OFFSET : CHECKSUM_OFFSET + 2] = checksum.to_bytes(2, "little")

    args.rom_out.parent.mkdir(parents=True, exist_ok=True)
    args.rom_out.write_bytes(image)

    model = args.model_out
    model.mkdir(parents=True, exist_ok=True)
    for name in ("config.bin", "interface.png"):
        shutil.copy2(args.model_template / name, model / name)
    ram_template = args.ram_template or (args.base_rom.parent / "ram.dmp")
    seeded_ram = write_model_image(model, image, ram_template)

    print(f"Standalone fx-580 ROM: {args.rom_out} ({len(image)} bytes)")
    print(f"Embedded CWiz boot payload: {len(program)} bytes")
    print(f"ROM checksum: {checksum:04X}")
    print(f"Emulator model: {model}")
    print(f"  rom.bin: {len(image)} bytes")
    print(f"  wiz/diagnostic-menu.bin: {len(image)} bytes")
    if seeded_ram:
        print(f"  ram.dmp: seeded from {ram_template}")
    else:
        print(
            "  ram.dmp: not seeded (no stock RAM image); the first boot will "
            "run the CWiz OS recovery path"
        )


if __name__ == "__main__":
    main()
