# Example programs — organized library

Every category has parallel `.wz` / `.cwiz` pairs where appropriate.
All examples use 8-px line spacing and stay inside 192 x 64 px.

## Categories

| Category | File | Description |
|----------|------|-------------|
| **hello** | `hello/01_hello.wz` / `.cwiz` | Smallest visible program (`clear`, `text`, `wait_key`). |
| **text** | `text/02_text_demo.wz` / `.cwiz` | Text output with the native 6x8 CW font (`print`, `text`, `num`). |
| **shapes** | `shapes/03_shapes.wz` / `.cwiz` | Drawing primitives (`rect`, `line`, `circle`, `disc`, `fillrect`, `hline`, `vline`). Labels sit on 8-px lines (`y` = 8, 16, 24, 32, 40, 48, 56). |
| **input** | `input/04_keys.wz` / `.cwiz` | Arrow-key box + `key()` echo using `print`; double-buffered (`buffered`/`flip`). |
| **arrays** | `arrays/05_arrays.wz` / `.cwiz` | RAM array + `rom8` / `rom int`; bar chart with 8-px text grid. |
| **math** | `math/06_math.wz` / `.cwiz` | Arithmetic, clamped ranges (`clamp`), `print` for numbers. |
| **cwapp** | `cwapp/07_cwapp.cwiz` | `#cwapp`, `#slot 2`, `#for 580`, `exit()`; builds to a full patched 580 ROM `.bin` (does not overwrite embedded stock ROM). |
| **580/880** | `580_880/08_target.wz` / `.cwiz` | Target-independent; works unchanged for 580 (`192x63`, 1 bp) or 880 (`192x64`, 2 bp). |
| **rop** | `41_rop_hello.cwiz` … `46_rop_loop.cwiz` | ROP gadget-chain payloads for the fx-580VN X (see the ROP section below). |

## Print note

- `print(x, y, ...)` draws the native 6x8 CW font (`FONT6X8`) with a 6 px advance.
- `text()` / `num()` use the same font.
- Unicode/`string` values are printed through the same routine.

## Build quick-reference

```console
# Wiz (.wz)
$ WIZ_ROOT=$PWD ./build/wizc examples/hello/01_hello.wz
# CWiz (.cwiz)
$ WIZ_ROOT=$PWD ./build/wizc examples/hello/01_hello.cwiz
# cwapp (writes full 256 KB patched ROM to source-dir .bin)
$ WIZ_ROOT=$PWD ./build/wizc examples/cwapp/07_cwapp.cwiz
```

All examples stay inside the 192 x 64 pixel screen. No external dependencies.

## ROP examples (gadget-chain payloads)

`41_rop_hello.cwiz` … `46_rop_loop.cwiz` are **ROP** payloads.  Instead of a
ROM image the compiler emits a chain of nX-U16/U8 gadgets that is injected
into the calculator's RAM.  They are compiled by the `#rop` backend and use
the same CWiz syntax as every other example.

| File | What it shows |
|---|---|
| `41_rop_hello.cwiz` | The smallest payload: `setlr` / `setsfr` / `screen_del`, one `print()` on line 1, `render()`. |
| `42_rop_line.cwiz` | `line(0, 0, 191, 62)` - a diagonal line plus a label. |
| `43_rop_common.cwiz` | Two `print()` calls, one per line (`linepos` `0x01` and `0x11`). |
| `44_rop_text.cwiz` | Font sizes: `print()` (large) and `print_small()` with font `0x08` and `0x0A`. |
| `45_rop_bitmap.cwiz` | `bitmap(10, 10, 8, 8, "ff818181818181ff")` - an 8x8 hollow square. |
| `46_rop_loop.cwiz` | An endless `while (1)` loop: clear, draw, render, repeat. |

### Directives

| Directive | Meaning |
|---|---|
| `#rop` | Selects the ROP (gadget chain) backend. |
| `#for 580` / `#for 880` | Target model; picks the gadget table. |
| `#org E9E0` | Address the payload is injected at (580: `E9E0`, 880: `E330`). |
| `#launcher common` | The community "launcher chung" (default). |
| `#launcher simple` | The 10-byte `pop er14` / `sp = er14` stub. |
| `#launcher <address>` | The simple stub placed at an explicit RAM address. |
| `#launcher common <len> <backup> <main>` | Launcher size code (`short` / `medium` / `long`), backup address, execution address. |

### API

| Call | Gadget | Notes |
|---|---|---|
| `setlr()` | `setlr` `0x09FA8` | Establish LR - required at the start of every chain. |
| `setsfr()` | `setsfr` `0x0A0E0` | Initialize the display / keyboard state. |
| `screen_del()` | `buffer_clear` `0x08C60` | Clear the screen buffer. |
| `print(pad, line, "text")` | `printline` `0x23DC8` | `pad` is normally `0x30`; always the large font, max 17 characters, the only font that renders Vietnamese text. |
| `print_small(font, line, "text")` | `smallprint` `0x23DCC` | `font` = `0x08` small, `0x0A` medium, `0x0E` large. |
| `line(x1, y1, x2, y2)` | `line_draw` `0x08E62` | Screen is 192 x 64 px, `x` = 0..191, `y` = 0..63. |
| `bitmap(x, y, w, h, "data")` | `render_bitmap` `0x09848` | Hex data, one byte = 8 horizontal pixels, `w * h / 8` bytes. |
| `render()` | `render.ddd4` `0x0947E` | Mandatory after drawing or printing. |

Text rows (`line` / `linepos`):

| Line | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| `linepos` | `0x01` | `0x11` | `0x21` | `0x31` |

In a string `~` means a space.  The launch sequence of every program is
`setlr(); setsfr(); screen_del();` and `render()` is required to show
anything.

### Running a ROP example

1. Build the file in the **Wiz** tab of CasioEmuMsvc; the `#rop` directive
   selects the gadget-chain backend, so "Build" produces a payload binary
   instead of a ROM image.
2. Inject the payload at the `#org` address - `E9E0` on the 580, `E330` on the
   880.
3. Type the common launcher into the calculator's program input area -
   `D180` on the 580, `9268` on the 880 - and start it with **LEFT** then
   **EXE**.

The common launcher copies the payload from the backup area (`E9E0`) to the
execution area (`D730` on the 580) and runs it there, so absolute addresses
inside the payload are resolved against the execution address.

The ABI details, the launcher bytes and the community reference material live
in `docs/ROP.md` and `docs/rop-notes/`.
