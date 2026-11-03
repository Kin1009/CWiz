#!/usr/bin/env python3
"""Package the CWiz boot payload as a standalone fx-580 ROM/model."""

import argparse
import shutil
from pathlib import Path


ROM_SIZE = 0x40000
CHECKSUM_OFFSET = 0x3FFF6


def fx580_checksum(image: bytearray) -> int:
    # Mirrors CasioEmuMsvc's CWX checksum: subtract 16-bit little-endian words.
    total = 0
    for start, end in ((0, 0xFC00), (0x10000, CHECKSUM_OFFSET)):
        for offset in range(start, end, 2):
            total = (total - int.from_bytes(image[offset : offset + 2], "little")) & 0xFFFF
    return total


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True, type=Path)
    parser.add_argument("--base-rom", required=True, type=Path)
    parser.add_argument("--rom-out", required=True, type=Path)
    parser.add_argument("--model-template", required=True, type=Path)
    parser.add_argument("--model-out", required=True, type=Path)
    args = parser.parse_args()

    program = args.program.read_bytes()
    base = args.base_rom.read_bytes()
    if len(base) != ROM_SIZE:
        raise SystemExit(f"base ROM must be {ROM_SIZE} bytes, got {len(base)}")
    if not program or len(program) >= CHECKSUM_OFFSET:
        raise SystemExit("CWiz boot payload is empty or overlaps the ROM metadata/checksum")

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
    (model / "wiz").mkdir(exist_ok=True)
    (model / "wiz" / "diagnostic-menu.bin").write_bytes(image)
    (model / "ram.dmp").unlink(missing_ok=True)

    print(f"Standalone fx-580 ROM: {args.rom_out} ({len(image)} bytes)")
    print(f"Embedded CWiz boot payload: {len(program)} bytes")
    print(f"ROM checksum: {checksum:04X}")
    print(f"Emulator model: {model}")


if __name__ == "__main__":
    main()
