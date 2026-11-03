# Chooser UI

A two-variant menu chooser for the CWiz OS.

## Files

- `chooser.cwiz` — the reusable runtime: glyph rendering, trimming, labels,
  pages, scroller, key handling.
- `menus/demo.cwiz` — generated 13-item demo (one long item for trimming).
- `menus/demo4.cwiz` — generated 4-item demo.
- `demo.cwiz`, `demo4.cwiz` — standalone `main()` wrappers.
- `../tools/gen_chooser.py` — menu generator.

## API

A menu module includes `chooser.cwiz`, defines a `rom8` blob of NUL-terminated
items, copies it into `chooser_blob`, sets `chooser_count` and
`chooser_variant`, then calls `chooser_run()`:

```c
int r = chooser_run();   // selected index, or -1 on AC
```

Generate the whole module:

```sh
python3 tools/gen_chooser.py --name main \
    --items "Calculate,Complex,Base-N,Matrix,Vector,Statistics" \
    --out ui/menus/main.cwiz
```

Then `main_chooser(0)` for the big variant and `main_chooser(1)` for small.

## Variants

| | Big (`variant=0`) | Small (`variant=1`) |
| --- | --- | --- |
| Font | CW 11×13 (`_cw_font`) | 6×8 (`_font`) |
| Page | 4 lines | 6 rows × 2 columns (left 1–6, right 7–C) |
| Keys | 1–4 only | 1–9, A (`K_NEG`), B (`K_DEG`), C (`K_INV`) |
| Line limit | 15 chars; over → 12 + `...` | 14 chars; over → 11 + `...` |
| Label | `1.`–`4.` | `1.`–`9.`, `A.`–`C.` |
| Text start | x=22 | x=12 (left), x=108 (right) |

Both variants: Up/Down pages, AC cancels (`-1`), a valid key returns the
global item index immediately. The 2px scroller is drawn at x=190–191 and only
when there is more than one page.

## Build

```sh
make ui-demo     # builds build/chooser-demo.bin from ui/demo.cwiz
```

The demo currently opens the small variant; change `demo_chooser(0)` for big.

## Notes

- `chooser_blob` is 512 bytes of RAM; keep menus under 500 bytes of text.
- Drawing uses `_hspan` runs (not per-pixel `pixel`) so page turns are fast.
- The right column's 14-char maximum reaches x=192; the 2px scroller may
  overlap the last two pixels of an exactly-14-char right entry.
