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
The generated table covers printable ASCII (`32..126`), matching Wiz's byte-string
model; the UFO's Vietnamese/CJK glyphs are not included yet.

Regenerate the header with:

```console
$ python3 compiler/tools/gen_font_cw.py path/to/ClassWizCWDisplay-Regular.ufo
```

The font is licensed under the SIL Open Font License 1.1; see
`ClassWizCWDisplay-OFL.txt`.  The generated header is a derived bitmap, not the
original font software.
