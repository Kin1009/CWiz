# CWiz OS — fx-580VN X

A standalone CWiz operating ROM for the fx-580VN X. The compiled payload
replaces the boot area of an isolated 256 KB model image; the stock model and
ROM are untouched. It boots to the launcher. Hold **SHIFT+7** during boot to
enter diagnostics.

## Build

```sh
make build   # compile the OS, generate assets, package the model
make run     # build CasioEmuMsvc if needed and launch the model
```

The Makefile resolves the compiler and emulator as sibling projects
(`../compiler`, `../emulator`) from the workspace root.

## Launcher

- 4×2 grid of the 12 stock apps, composed at run time from 24 sprites
  (`launcher/menu_icons.cwiz`): one unselected and one selected 48×24 sprite
  per app. Adding an app only needs two more sprites and a label entry.
- `1`–`9`, `A` (`(-)`), `B` (`deg`), `C` (`x^-1`) launch apps directly.
- Arrow keys move; at a row edge, left/right continues on the previous/next
  row. `EXE` opens the highlighted app. `AC` does not enter diagnostics.
- Calculate and Complex are implemented; the rest show “App not installed”.
- The bottom label is drawn with `print_cw` at `y=51`; the captured separator
  row is erased.

## Diagnostics

Entered only with **SHIFT+7** during boot. Exit paths return to main (halt);
they do not jump into the launcher.

- Main screen: random `x+y`/`x-y` question (result 1–5) at the top-left,
  `correct/total` hex counters at the top-right, `Press AC` at the bottom.
  Press `1`–`5` to answer. A correct answer shows `TEST OK`, waits about a
  second, then halts. A wrong answer halts without `TEST OK`. Counters live in
  RAM (`0xDCF0`, `0xDCF1`) and are repaired to `00/00` if `correct > total`.
- `6` opens the small-font menu: Display, Version & SUM, Key test, Clear RAM,
  AC back.
- `9` runs every screen in order.
- **Version & SUM** (big font): model (`EX-580`), `V.<letter> <major>.<minor>.<build>`,
  `SUM <hex> OK|NG`, `Press AC`. `SHIFT+AC` opens the serial screen.
- **Serial** (big font): `[Serial number]` centered, the 12-character serial
  centered between lines 2 and 3, `Press AC`. The bytes live at `DBE0..DBEB`.
- **Display**: automatic checkerboard, inverse checkerboard, all ink, all
  clear.
- **Clear RAM**: `SHIFT` confirms a full `0xD000..0xFFFF` wipe (runtime
  `wipe` builtin uses registers only and halts).

### SUM coverage

The SUM is a 16-bit byte sum over the 3,456-byte icon table and both font
tables (~6.7 KiB). The builder and the calculator read the same `rom8`/`rom`
tables, so the value is stable across boots and reports `OK`.

A larger absolute-address checksum (for example `peek(0x0000..0x7FFF)`) is not
stable on the unbanked CW target: addresses outside the accessible ROM window
fall through to RAM, so the result changes every boot. Covering more of the
image needs ROM-bank switching in the compiler/runtime.

## Memory integrity and init

`system.cwiz::init()` runs before the serial store. It checks
`DBD0..DBDF` for `0F 0E ... 01 00`. If the pattern is missing (garbage RAM),
it rewrites the marker, calls `recover()` (wipe everything except
`DBD0..DBDF`, then `D000..D16B` last), and jumps to the stock reset stub
`0:A1CE`. The serial moved to `DBE0..DBEB` so it no longer collides with the
integrity span.

## Chooser status bar

The chooser uses the real status icons (`setStatus(UP/DOWN, HIGH/LOW)` plus
`renderStatus()`): `UP` lights when an earlier page exists, `DOWN` when a later
page exists. Content starts at y=2 so row 0 stays free for the status bar. The
2px scroller is at x=190–191. See `ui/README.md`.

## Formats

- Serial: `MMMBBBBBNNNN` (no hyphens) — 3-char model (`058`), 5-hex build
  number, 4-hex random.
- Version: `<major>.<minor>.<build>`, with a hash-derived letter `A`–`Z`
  (`SUM % 26`).
- Model codes: `580 → EX`, `880 → CW`, `880 PLUS → CX` (EX / CW / CWII
  generations).

## Layout

- `main.cwiz` — boot entry and the SHIFT+7 chord.
- `launcher/` — sprite compositor, sprite table, app shortcuts.
- `diagnostics/` — diagnostics menu, checksum, memory/key tests.
- `apps/` — Calculate and Complex.
- `tools/` — serial/version generation and sprite extraction.
- `assets/reference-menu/` — the 16 stock menu captures.
- `docs/` — design notes.
