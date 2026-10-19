#!/usr/bin/env python3
"""Regenerate src/builtin_rom.cpp from a fx-580VN X firmware image.

    python3 compiler/tools/embed_rom.py [rom.bin]

The default input is emulator/models/fx-580VN X/rom.bin.  Keeping the image in
the compiler binary means `wizc app.cwiz` can patch a #cwapp program without a
separate ROM file next to the source.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_ROM = ROOT / "emulator" / "models" / "fx-580VN X" / "rom.bin"
OUT_CPP = pathlib.Path(__file__).resolve().parents[1] / "src" / "builtin_rom.cpp"


def main() -> int:
    rom_path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_ROM
    rom = rom_path.read_bytes()
    if len(rom) != 262144:
        print(f"{rom_path}: expected a 262144-byte fx-580VN X image, got {len(rom)}", file=sys.stderr)
        return 1

    lines = [
        "// builtin_rom.cpp - generated from emulator/models/fx-580VN X/rom.bin.",
        "// Do not edit by hand; run compiler/tools/embed_rom.py to regenerate.",
        '#include "builtin_rom.hpp"',
        "",
        "namespace wiz {",
        "",
        "const unsigned char kFx580VnRom[] = {",
    ]
    for i in range(0, len(rom), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in rom[i : i + 16])
        lines.append(f"    {chunk},")
    lines += [
        "};",
        "",
        "const std::size_t kFx580VnRomSize = sizeof(kFx580VnRom);",
        "",
        "}  // namespace wiz",
        "",
    ]
    OUT_CPP.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT_CPP} ({len(rom)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
