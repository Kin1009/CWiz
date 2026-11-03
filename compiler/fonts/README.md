# Fonts

`cw-mono.ttf` and `cw-text.ttf` are the reference TrueType faces used by the
original tooling.  The built-in 6x8 bitmap lives in
`compiler/src/font6x8.hpp`.

## ClassWiz CW Display

`compiler/src/font_cw.hpp` is the native ClassWiz CW Display 12 px face used by
`print_cw()` / `text_cw()` / `num_cw()` and the one-character helpers
(`char_cw()` in Wiz, `glyph_cw()` in CWiz).  It is rasterised unchanged at
1x from the UFO in
[Wenti-D/ClasswizDisplayFont](https://github.com/Wenti-D/ClasswizDisplayFont):
the design uses 10 font units per LCD pixel, so an 11 px advance is `110` units
and the 12 px cap height is `120` units.  Do not scale it to the old 6x8 cell.
The generated table covers printable ASCII (`32..126`).  The UFO's CJK glyphs
are not included; Vietnamese is a separate table, below.

Regenerate the header with:

```console
$ python3 compiler/tools/gen_font_cw.py path/to/ClassWizCWDisplay-Regular.ufo
```

## Vietnamese CW Display glyphs

`compiler/src/font_cw_vn.hpp` holds the 134 Vietnamese glyphs used by the
firmware's F4 key encoding (`0xF4, 0x10..0x95`).  They are rasterised from
`cw-mono.ttf`, the reference CW Mono face, using the same 11x13 native cell as
the ASCII table.  The codepoint -> F4 column mapping is `vn_f4.txt`, the
single source of truth also used by `rop.cpp`, `cwiz.cpp` and `wiz.cpp`.
`compiler/src/vn_f4.hpp` is generated from it for compiler-side lookups.

Regenerate both headers with:

```console
$ python3 compiler/tools/gen_font_cw_vn.py compiler/fonts/cw-mono.ttf \
      compiler/fonts/vn_f4.txt
```

The generator needs `fonttools`; normal builds use the checked-in headers.
The Vietnamese table is emitted into a program only when an F4 glyph or
`normalizeText()` is referenced.

The font is licensed under the SIL Open Font License 1.1; see
`ClassWizCWDisplay-OFL.txt`.  The generated header is a derived bitmap, not the
original font software.
