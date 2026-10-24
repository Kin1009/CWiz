#!/usr/bin/env python3
"""Regenerate src/font_cw.hpp from the ClassWiz CW Display UFO.

    python3 compiler/tools/gen_font_cw.py path/to/ClassWizCWDisplay-Regular.ufo

The UFO is the native pixel design from Wenti-D/ClasswizDisplayFont.  Its
coordinates are 10 units per display pixel, so the glyphs are rasterised at
1x (no scaling): 11 px advance, 12 px cap height and one descender row.  Each
11x13 bitmap is stored as 13 words, bit 10 = leftmost pixel.

The source font has no ASCII backslash or grave.  Those two cells are filled
from the font's own slash (mirrored) and the grave accent in `agrave`; every
other glyph comes straight from the UFO.  The font is SIL OFL 1.1; see
compiler/fonts/ClassWizCWDisplay-OFL.txt.
"""
import pathlib
import sys
import xml.etree.ElementTree as ET

FIRST, LAST = 32, 126
W, H = 11, 13
ASCENT = 120
UNITS_PER_PIXEL = 10

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = pathlib.Path(__file__).resolve().parents[1] / "src" / "font_cw.hpp"


def read_glyph(path):
    tree = ET.parse(path).getroot()
    node = tree.find("advance")
    advance = int(float(node.get("width"))) if node is not None else None
    contours = []
    for contour in tree.findall("./outline/contour"):
        pts = [(float(p.get("x")), float(p.get("y"))) for p in contour.findall("point")]
        if pts:
            contours.append(pts)
    return advance, contours


def glyphs(ufo):
    found = {}
    for path in (ufo / "glyphs").glob("*.glif"):
        tree = ET.parse(path).getroot()
        advance, contours = read_glyph(path)
        if advance is None:
            continue
        for uni in tree.findall("unicode"):
            cp = int(uni.get("hex"), 16)
            if FIRST <= cp <= LAST and cp not in found:
                found[cp] = (advance, contours)
    return found


def inside(contours, x, y):
    """Even-odd point-in-polygon test for rectilinear UFO contours."""
    odd = False
    for pts in contours:
        for i, (x1, y1) in enumerate(pts):
            x2, y2 = pts[(i + 1) % len(pts)]
            if (y1 > y) != (y2 > y):
                cross = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
                if x < cross:
                    odd = not odd
    return odd


def raster(contours):
    rows = []
    for r in range(H):
        y = ASCENT - UNITS_PER_PIXEL * r - UNITS_PER_PIXEL // 2
        value = 0
        for k in range(W):
            x = UNITS_PER_PIXEL * k + UNITS_PER_PIXEL // 2
            value = (value << 1) | (1 if inside(contours, x, y) else 0)
        rows.append(value)
    return rows


def mirror(rows):
    return [int(format(v, f"0{W}b")[::-1], 2) for v in rows]


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    ufo = pathlib.Path(sys.argv[1])
    found = glyphs(ufo)
    missing = [cp for cp in range(FIRST, LAST + 1) if cp not in found]
    unexpected = [cp for cp in missing if cp not in (92, 96)]
    if unexpected:
        print(f"missing glyphs: {unexpected}", file=sys.stderr)
        return 1
    advances = sorted({adv for adv, _ in found.values()})
    if advances != [W * UNITS_PER_PIXEL]:
        print(f"expected a {W * UNITS_PER_PIXEL}-unit monospaced font, got {advances}",
              file=sys.stderr)
        return 1

    # The grave accent contour in agrave is the standalone ASCII grave.
    _, agrave = read_glyph(ufo / "glyphs" / "agrave.glif")
    grave = [c for c in agrave if min(p[1] for p in c) >= 100]
    if not grave:
        print("agrave has no grave accent contour", file=sys.stderr)
        return 1

    data = []
    for cp in range(FIRST, LAST + 1):
        if cp == 92:  # backslash: mirror the native slash
            rows = mirror(raster(found[ord("/")][1]))
        elif cp == 96:  # grave: use the accent from agrave
            rows = raster(grave)
        else:
            rows = raster(found[cp][1])
        data.extend(rows)

    lines = [
        "#pragma once",
        "",
        "// font_cw.hpp - ClassWiz CW Display ASCII 32..126 at native 1x.",
        "// Source: https://github.com/Wenti-D/ClasswizDisplayFont",
        "// Copyright (c) 2023-2026 Windy Deng and CASIO Computer Co., Ltd.",
        "// SIL Open Font License 1.1; see compiler/fonts/ClassWizCWDisplay-OFL.txt.",
        "// Do not edit by hand; run compiler/tools/gen_font_cw.py to regenerate.",
        "//",
        "// The UFO uses 10 units per display pixel.  Each glyph is 11 px wide, with",
        "// a 12 px cap height and one descender row above the baseline (13 rows).",
        "// Rows are stored top to bottom, bit 10 is the leftmost pixel.",
        "",
        f"static const int FONT_CW_W = {W};",
        f"static const int FONT_CW_H = {H};",
        f"static const int FONT_CW_FIRST = {FIRST};",
        f"static const int FONT_CW_LAST = {LAST};",
        f"static const int FONT_CW_COUNT = {LAST - FIRST + 1};",
        f"static const int FONT_CW_ROWS = {len(data)};",
        "",
        "static const unsigned short FONT_CW[FONT_CW_ROWS] = {",
    ]
    for i in range(FIRST, LAST + 1):
        start = (i - FIRST) * H
        chunk = ", ".join(f"0x{data[start + r]:03X}" for r in range(H))
        note = ""
        if i == 92:
            note = "  // backslash (mirrored slash)"
        elif i == 96:
            note = "  // grave (from agrave accent)"
        lines.append(f"    {chunk},{note}")
    lines += ["};", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT} ({len(data)} words, {len(data) * 2} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
