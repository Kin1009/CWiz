#!/usr/bin/env python3
"""Regenerate src/vn_f4.hpp and src/font_cw_vn.hpp for Vietnamese on CW Display.

    python3 compiler/tools/gen_font_cw_vn.py compiler/fonts/cw-mono.ttf \
        compiler/fonts/vn_f4.txt

`vn_f4.txt` is the authoritative F4 key table (0xF4, column 0x10..0x95) and
was extracted from the firmware font map distributed with the emulator.  The
bitmaps come from `compiler/fonts/cw-mono.ttf`, the CW Mono reference face:
it is the same 11x13 pixel design as the ASCII table in `font_cw.hpp`, and it
contains all 134 Vietnamese glyphs named by `vn_f4.txt`.

The generator rasterises every glyph at the native cell size (11 columns, 13
rows, 10 columns of ink plus the same gutter as the ASCII face).  Coordinates
are sampled at pixel centres, using the face's 2048-unit em square mapped to
13 rows, so no scaling is applied to the LCD output.

Requires fontTools (`python3 -m pip install fonttools`).  The generated headers
are checked in, so a normal build does not need fontTools.
"""
import pathlib
import sys

try:
    from fontTools.pens.recordingPen import RecordingPen
    from fontTools.ttLib import TTFont
except ImportError:  # pragma: no cover - only used by the generator
    print("fontTools is required: python3 -m pip install fonttools", file=sys.stderr)
    raise SystemExit(2)

W, H = 11, 13
FIRST, LAST = 0x10, 0x95
ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = pathlib.Path(__file__).resolve().parents[1] / "src"
MAX_CODE = LAST


def read_map(path):
    entries = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, cp = line.split()
        code = int(key[2:], 16)
        entries.append((code, int(cp, 16)))
    entries.sort()
    codes = [code for code, _ in entries]
    if codes != list(range(FIRST, LAST + 1)):
        raise SystemExit("vn_f4.txt must cover F410..F495 without gaps")
    seen = set()
    for _, cp in entries:
        if cp in seen:
            raise SystemExit(f"duplicate codepoint U+{cp:04X} in vn_f4.txt")
        seen.add(cp)
    return entries


def flatten(contours):
    """Return closed polygons; cw-mono is rectilinear but stay robust."""
    out = []
    for contour in contours:
        if len(contour) >= 3:
            out.append(contour)
    return out


def read_contours(glyph_set, glyph_name):
    pen = RecordingPen()
    glyph_set[glyph_name].draw(pen)
    contours, current, start = [], [], None
    for op, args in pen.value:
        if op == "moveTo":
            if current:
                contours.append(current)
            current = [args[0]]
            start = args[0]
        elif op == "lineTo":
            current.append(args[0])
        elif op == "qCurveTo":
            points = [p for p in args if p is not None]
            if not points:
                continue
            previous = current[-1]
            for i in range(len(points) - 1):
                control = points[i]
                end = points[i + 1]
                for step in range(1, 9):
                    t = step / 8.0
                    mt = 1.0 - t
                    current.append(
                        (
                            mt * mt * previous[0] + 2 * mt * t * control[0] + t * t * end[0],
                            mt * mt * previous[1] + 2 * mt * t * control[1] + t * t * end[1],
                        )
                    )
                previous = end
        elif op == "curveTo":
            previous = current[-1]
            p1, p2, p3 = args
            for step in range(1, 9):
                t = step / 8.0
                mt = 1.0 - t
                current.append(
                    (
                        mt**3 * previous[0]
                        + 3 * mt * mt * t * p1[0]
                        + 3 * mt * t * t * p2[0]
                        + t**3 * p3[0],
                        mt**3 * previous[1]
                        + 3 * mt * mt * t * p1[1]
                        + 3 * mt * t * t * p2[1]
                        + t**3 * p3[1],
                    )
                )
            current.append(p3)
        elif op in ("closePath", "endPath"):
            if op == "closePath" and start is not None:
                current.append(start)
            if current:
                contours.append(current)
            current = []
            start = None
    if current:
        contours.append(current)
    return flatten(contours)


def inside(contours, x, y):
    odd = False
    for points in contours:
        for i, (x1, y1) in enumerate(points):
            x2, y2 = points[(i + 1) % len(points)]
            if (y1 > y) != (y2 > y):
                cross = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
                if x < cross:
                    odd = not odd
    return odd


def raster(font, glyph_set, glyph_name):
    upem = font["head"].unitsPerEm
    ascent = font["hhea"].ascent
    upp = upem / H
    contours = read_contours(glyph_set, glyph_name)
    rows = []
    for r in range(H):
        y = ascent - (r + 0.5) * upp
        value = 0
        for k in range(W):
            x = (k - 0.5) * upp
            value = (value << 1) | (1 if inside(contours, x, y) else 0)
        rows.append(value)
    return rows


def write_vn_header(entries):
    ordered = sorted(entries, key=lambda item: item[1])
    lines = [
        "#pragma once",
        "",
        "// vn_f4.hpp - ClassWiz Vietnamese F4 key encoding (U+???? -> 0xF4, code).",
        "// Source: firmware VN_F4 font map; see compiler/fonts/vn_f4.txt.",
        "// Do not edit by hand; run compiler/tools/gen_font_cw_vn.py to regenerate.",
        "//",
        "// A Vietnamese glyph is stored in program text as two bytes: 0xF4 followed",
        "// by `code` (0x10..0x95).  ASCII bytes keep their usual meaning.",
        "",
        "#include <cstdint>",
        "",
        "struct VnF4Entry {",
        "    uint32_t cp;",
        "    uint8_t code;",
        "};",
        "",
        f"static const int VN_F4_FIRST_CODE = {FIRST};",
        f"static const int VN_F4_LAST_CODE = {LAST};",
        f"static const int VN_F4_COUNT = {len(ordered)};",
        "",
        "// Sorted by codepoint so callers can binary-search it.",
        "static const VnF4Entry VN_F4_TABLE[VN_F4_COUNT] = {",
    ]
    for code, cp in ordered:
        lines.append(f"    {{0x{cp:04X}, 0x{code:02X}}},")
    lines += [
        "};",
        "",
        "inline const VnF4Entry* vn_f4_find(uint32_t cp) {",
        "    int lo = 0;",
        "    int hi = VN_F4_COUNT - 1;",
        "    while (lo <= hi) {",
        "        int mid = (lo + hi) / 2;",
        "        if (VN_F4_TABLE[mid].cp == cp) return &VN_F4_TABLE[mid];",
        "        if (VN_F4_TABLE[mid].cp < cp) lo = mid + 1;",
        "        else hi = mid - 1;",
        "    }",
        "    return nullptr;",
        "}",
        "",
        "// Returns the 0x10..0x95 column byte, or 0 when `cp` has no F4 glyph.",
        "inline uint8_t vn_f4_code(uint32_t cp) {",
        "    const VnF4Entry* e = vn_f4_find(cp);",
        "    return e ? e->code : 0;",
        "}",
        "",
        "// Returns the codepoint for a column byte, or 0 when the column is unused.",
        "inline uint32_t vn_f4_cp_for_code(uint8_t code) {",
        "    for (int i = 0; i < VN_F4_COUNT; i++)",
        "        if (VN_F4_TABLE[i].code == code) return VN_F4_TABLE[i].cp;",
        "    return 0;",
        "}",
        "",
    ]
    (SRC / "vn_f4.hpp").write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_font_header(entries, rows_by_code):
    data = []
    for code, _ in entries:
        data.extend(rows_by_code[code])
    lines = [
        "#pragma once",
        "",
        "// font_cw_vn.hpp - ClassWiz CW Display Vietnamese glyphs (F4 10..95).",
        "// Source: compiler/fonts/cw-mono.ttf, the reference CW Mono face.",
        "// Bitmaps are native 11x13 cells: 11 px advance, 12 px cap height,",
        "// one descender row.  Bit 10 is the leftmost pixel.",
        "// Do not edit by hand; run compiler/tools/gen_font_cw_vn.py to regenerate.",
        "",
        f"static const int FONT_CW_VN_W = {W};",
        f"static const int FONT_CW_VN_H = {H};",
        f"static const int FONT_CW_VN_FIRST = {FIRST};",
        f"static const int FONT_CW_VN_LAST = {LAST};",
        f"static const int FONT_CW_VN_COUNT = {len(entries)};",
        f"static const int FONT_CW_VN_ROWS = {len(data)};",
        "",
        "static const unsigned short FONT_CW_VN[FONT_CW_VN_ROWS] = {",
    ]
    for code, cp in entries:
        start = (code - FIRST) * H
        chunk = ", ".join(f"0x{data[start + r]:03X}" for r in range(H))
        lines.append(f"    {chunk},  // F4{code:02X} U+{cp:04X}")
    lines += ["};", ""]
    (SRC / "font_cw_vn.hpp").write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    font_path = pathlib.Path(sys.argv[1])
    map_path = pathlib.Path(sys.argv[2])
    entries = read_map(map_path)
    font = TTFont(font_path)
    cmap = font.getBestCmap()
    glyph_set = font.getGlyphSet()
    hmtx = font["hmtx"]

    missing = [(code, cp) for code, cp in entries if cp not in cmap]
    if missing:
        for code, cp in missing:
            print(f"missing U+{cp:04X} for F4{code:02X}", file=sys.stderr)
        return 1
    advances = sorted({hmtx[cmap[cp]][0] for _, cp in entries})
    if len(advances) != 1:
        print(f"expected a monospaced VN set, got advances {advances}", file=sys.stderr)
        return 1

    rows_by_code = {}
    for code, cp in entries:
        rows = raster(font, glyph_set, cmap[cp])
        if not any(rows):
            print(f"U+{cp:04X} (F4{code:02X}) rasterised to an empty glyph", file=sys.stderr)
            return 1
        rows_by_code[code] = rows

    write_vn_header(entries)
    write_font_header(entries, rows_by_code)
    print(f"wrote {SRC / 'vn_f4.hpp'}")
    print(f"wrote {SRC / 'font_cw_vn.hpp'} "
          f"({len(entries) * H} words, {len(entries) * H * 2} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
