#!/usr/bin/env python3
"""Build the 24 app-icon sprites for the fx-580VN X launcher.

Twelve apps are captured in the 16 stock menu states.  For each app we emit
two 48x24 sprites: unselected (from the per-page majority image) and selected
(from the capture where that app is highlighted, with the rounded inverse box
already baked in).  New apps only need two more sprites in this list; the
launcher composes the grid dynamically from the sprite table.

Screen layout (4 columns x 2 visible rows):
    top row y=2   (24 px)
    bottom row y=26 (24 px)
    separator y=48
"""

from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
CAPTURES = PROJECT / "assets" / "reference-menu"
OUTPUT = PROJECT / "launcher" / "menu_icons.cwiz"
FRAME_SIZE = 0x800
STRIDE = 32
WIDTH = 192
HEIGHT = 64
CELL_W = 48
CELL_H = 24
CELL_BYTES = CELL_W // 8 * CELL_H  # 144

PAGE1 = [
    "1-Calculate-page1.png",
    "2-Complex-page1.png",
    "3-Base-N-page1.png",
    "4-Matrix-page1.png",
    "5-Vector-page1.png",
    "6-Statistics-page1.png",
    "7-Distribution-page1.png",
    "8-Table-page1.png",
]
PAGE2 = [
    "5-Vector-page2.png",
    "6-Statistics-page2.png",
    "7-Distribution-page2.png",
    "8-Table-page2.png",
    "9-Equation-Func-page2.png",
    "A-Inequality-page2.png",
    "B-Verify-page2.png",
    "C-Ratio-page2.png",
]
APPS = [
    "1 Calculate",
    "2 Complex",
    "3 Base-N",
    "4 Matrix",
    "5 Vector",
    "6 Statistics",
    "7 Distribution",
    "8 Table",
    "9 Equation/Func",
    "A Inequality",
    "B Verify",
    "C Ratio",
]


def load_framebuffer(path: Path) -> bytes:
    from PIL import Image

    image = Image.open(path).convert("1").resize((WIDTH, HEIGHT))
    pixels = image.load()
    data = bytearray(FRAME_SIZE)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if pixels[x, y]:
                data[y * STRIDE + x // 8] |= 0x80 >> (x & 7)
    # The label band is drawn at run time.
    for y in range(49, HEIGHT):
        for x in range(WIDTH):
            data[y * STRIDE + x // 8] &= ~(0x80 >> (x & 7))
    return bytes(data)


def pixel(data: bytes, x: int, y: int) -> int:
    return (data[y * STRIDE + x // 8] >> (7 - (x & 7))) & 1


def majority(frames: list[bytes]) -> bytes:
    out = bytearray(FRAME_SIZE)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            ones = sum(pixel(frame, x, y) for frame in frames)
            if ones * 2 > len(frames):
                out[y * STRIDE + x // 8] |= 0x80 >> (x & 7)
    return bytes(out)


def selected_cell(base: bytes, frame: bytes) -> tuple[int, int] | None:
    xs: list[int] = []
    ys: list[int] = []
    for y in range(0, 48):
        for x in range(WIDTH):
            if pixel(base, x, y) != pixel(frame, x, y):
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return min(ys) // 24, min(xs) // 48


def sprite(frame: bytes, x0: int, y0: int) -> bytes:
    out = bytearray(CELL_BYTES)
    for row in range(CELL_H):
        for x in range(CELL_W):
            if pixel(frame, x0 + x, y0 + row):
                out[row * 6 + x // 8] |= 0x80 >> (x & 7)
    return bytes(out)


def check(name: str, files: list[str]) -> list[bytes]:
    frames = [load_framebuffer(CAPTURES / f) for f in files]
    base = majority(frames)
    for index, (f, frame) in enumerate(zip(files, frames)):
        expected = (index // 4, index % 4)
        detected = selected_cell(base, frame)
        if detected != expected:
            raise SystemExit(f"{f}: selected cell {detected} != expected {expected} ({name})")
    return frames


def _numbers(path: Path, name: str) -> list[int]:
    text = path.read_text(encoding="utf-8")
    body = text.split(name + "[", 1)[1].split("]", 1)[1]
    body = body.split("{", 1)[1].split("}", 1)[0]
    body = "\n".join(line.split("//")[0] for line in body.splitlines())
    return [int(x, 0) for x in body.replace(",", " ").split()]


def font_bytes_sum() -> int:
    root = PROJECT.parent
    cw = root / "compiler" / "src" / "font_cw.hpp"
    f6 = root / "compiler" / "src" / "font6x8.hpp"
    if not cw.exists() or not f6.exists():
        return 0
    total = 0
    for value in _numbers(cw, "FONT_CW"):
        total = (total + (value & 0xFF) + ((value >> 8) & 0xFF)) & 0xFFFF
    for value in _numbers(f6, "FONT6X8"):
        total = (total + (value & 0xFF)) & 0xFFFF
    return total


def main() -> None:
    page1 = check("page1", PAGE1)
    page2 = check("page2", PAGE2)
    base1 = majority(page1)
    base2 = majority(page2)

    sprites: list[bytes] = []
    for app in range(12):
        if app < 8:
            selected = page1[app]
            plain = base1
            row = 2 if app < 4 else 26
            col = app % 4
        else:
            selected = page2[app - 4]
            plain = base2
            row = 26
            col = (app - 8) % 4
        sprites.append(sprite(plain, col * CELL_W, row))
        sprites.append(sprite(selected, col * CELL_W, row))

    lines = [
        "// Generated by tools/build_menu_bitmaps.py.",
        "// 12 apps x 2 variants (0 = unselected, 1 = selected), 48x24, 144 bytes each.",
        f"rom8 menu_icons[{len(sprites) * CELL_BYTES}] = {{",
    ]
    for index, data in enumerate(sprites):
        app = index // 2
        sel = index % 2
        lines.append(f"    // {app:02d} {APPS[app]} ({'selected' if sel else 'plain'})")
        for offset in range(0, CELL_BYTES, 16):
            lines.append("    " + ", ".join(str(v) for v in data[offset:offset + 16]) + ",")
    lines.append("};")
    total = sum(sum(sprite) for sprite in sprites) & 0xFFFF
    total = (total + font_bytes_sum()) & 0xFFFF
    lines.append("")
    lines.append("// 16-bit sum of every sprite byte (same region the runtime checks).")
    lines.append("rom8 menu_icons_sum[2] = {")
    lines.append(f"    {total & 0xFF}, {total >> 8},")
    lines.append("};")
    OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {OUTPUT} ({len(sprites)} sprites, {len(sprites) * CELL_BYTES} bytes)")


if __name__ == "__main__":
    main()
