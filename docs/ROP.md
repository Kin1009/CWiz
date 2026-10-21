# ROP payloads in CWiz syntax

A ROP program is a normal `.cwiz` file with a `#rop` line. The compiler emits a
gadget chain instead of a ROM image, and the emulator injects it into RAM.

```c
#rop
#for 580
#org E9E0        // payload address (default E9E0 on 580, E330 on 880)
#launcher common // "launcher chung" (the default)

int main() {
    setlr();
    setsfr();
    screen_del();
    print(0x30, 0x01, "HELLO ROP");   // pad, line, text
    render();
}
```

Every ROP program starts with `setlr` (establish LR) and `setsfr` (initialize
the calculator's SFR/display state) — a chain that omits this preamble can
inject successfully and then hang when it reaches display code. `screen_del()`
clears the frame buffer and `render()` is mandatory after drawing: without it
nothing appears on the screen.

## How it runs

ROP needs two pieces of memory:

1. **Payload** at `#org` — the gadget chain itself. Inject it at `E9E0` on the
   580 and at `E330` on the 880. `#org` and `#launcher` only apply to a `#rop`
   source; normal Wiz/CWiz programs ignore them.
2. **Launcher** — a short chain that makes the calculator start executing the
   payload. The default is the *launcher chung* (common launcher), the one used
   by most existing 580 programs.

The launcher is typed into the calculator's program input area (`D180` on the
580, `9268` on the 880) and started with **LEFT** then **EXE**.

## Chain encoding

Every gadget is four bytes:

```
[ addr_lo, addr_hi, class_lo, class_hi ]
```

| class | meaning | example |
|---|---|---|
| `30 30` | call | `50 94 30 30` = call `0x09450` (`BL memcpy`) |
| `31 30` | pop | `34 7B 31 30` = pop `0x17B34` (`pop xr0`) |
| `32 30` | flow / stack | `60 0D 32 30` = `sp = er14, pop er14` |
| `33 30` | setlr_pc | `D0 08 33 30` = `setlr_pc` |

The class bytes are **explicit** — they are never derived from the gadget
address. A gadget at `0x23DC8` must still be emitted as `C8 3D 30 30`; adding
the address to a `0x3030xxxx` constant corrupts the class byte for every
address at or above `0x10000` and silently turns calls into pops.

### Registers (nX-U16)

```
QR0 (8 bytes)
├── XR0 (4 bytes)
│   ├── ER0 (2 bytes)  -> R1 (high), R0 (low)
│   └── ER2 (2 bytes)  -> R3 (high), R2 (low)
└── XR4 (4 bytes) ...
```

`Rn` is one byte, `ERn` is two bytes (`R(n)` low, `R(n+1)` high) and `XRn` is
four bytes (`ERn` + `ER(n+2)`). So `pop xr0` consumes **four** bytes: bytes 0-1
land in `ER0`, bytes 2-3 land in `ER2`. `pop er0` / `pop er2` consume two bytes
each.

### Gadget ABIs used by the builtins

| builtin | gadget | registers |
|---|---|---|
| `print(pad, line, "text")` | `printline` `0x23DC8` | `ER0 = (pad << 8) \| line`, `ER2 = string` |
| `print_small(font, line, "text")` | `smallprint` `0x23DCC` | `ER0 = (line << 8) \| font`, `ER2 = string` |
| `line(x1,y1,x2,y2)` | `line_draw` `0x08E62` | `XR0 = x1, y1, x2, y2` |
| `bitmap(x,y,w,h,data)` | `render_bitmap` `0x09848` | `XR0 = x, y, w, h`, then `ER0 = data` |
| `screen_del()` | `buffer_clear` `0x08C60` | — |
| `render()` | `render.ddd4` `0x0947E` | — |
| `setlr()` | `setlr` `0x09FA8` | — |
| `setsfr()` | `setsfr` `0x0A0E0` | — |

`pad` is normally `0x30`; `line` is the row byte (`0x01`, `0x11`, `0x21`, `0x31`
for lines 1..4); `font` is `0x08` (small), `0x0A` (medium) or `0x0E` (large).
Only the large font can render Vietnamese text. `print` uses the large font and
is limited to 17 characters per line.

## 580 common launcher

The 26 byte *launcher chung* is byte-identical to the one used by the existing
community programs:

```
FD 24 30 30                     trigger + input-area prefix
DA 7B 31 30                     copy-with-length gadget
FE 03                           copy length 0x03FE (1022 bytes)
E0 E9                           source E9E0 (backup area)
30 D7                           destination D730 (execution area)
2E D7                           execution address - 2
32 89 31 30                     pop 0x8932
30 30                           class word
74 1F 32 30                     sp = er6, pop er8, pop pc
```

The copy length is selected by the RAC length code: `01` = `0x01FE` (510
bytes), `02` = `0x02FE` (766 bytes, default), `03` = `0x03FE` (1022 bytes). The
payload must fit in the selected length.

The launcher copies the payload from the backup area (`E9E0`) to the execution
area (`D730`) and continues there, so **absolute addresses inside the payload
are resolved against `D730`**, while the bytes themselves are injected at
`E9E0`. The `pop xr0` / `BL memcpy` launcher is a different variant and must
not be substituted byte-for-byte.

### 880 common launcher

```
FD 26                           trigger
2E E3                           payload address - 2 (E330 - 2)
C0 75 32 30                     sp gadget
```

### Simple launcher

`#launcher simple` (or `#launcher <address>`, for compatibility) emits the
10-byte stub instead. Without an explicit address the launcher goes to the
program input area (`D180` on the 580, `9268` on the 880):

```
78 5C 31 30   pop er14
<payload-2>   little-endian
60 0D 32 30   sp = er14, pop er14
```

After the stub, SP points exactly at the payload.

### Options

```
#launcher common                 // defaults: RAC medium launcher, #org, D730 (580)
#launcher common short           // 01 short, 02 medium, 03 long
#launcher common long E9E0 D730  // length, backup, execution address
#launcher simple                 // 10-byte stub
#launcher 8154                   // stub placed at an explicit address (must be RAM)
```

"Build" writes `wiz/<name>.bin` (payload) and prints the launcher as hex in the
log. The standalone `wizc` driver writes the launcher separately as
`<name>.launcher.bin`. "Build & Run" also injects both blocks into the running
model. Use **Copy launcher** in the toolbar to put the launcher hex on the
clipboard.

## Syntax

Everything is CWiz syntax: `int` globals, `if`/`else`, `while`, `for`, `goto`,
labels, `asm { "gadget" }`, function calls. Addresses use `[HEX]` (and `0xHEX`
for plain numbers).

Builtins are mapped to the gadget database (580 and 880):

| call | gadgets tried |
|---|---|
| `screen_del()` / `cls()` | `buffer_clear`, `memzero` |
| `line(x1,y1,x2,y2)` | `line_draw` |
| `pixel(x,y)` | `pixel_draw` |
| `print(pad, line, "text")` | `pop xr0`, `printline` |
| `print_small(font, line, "text")` | `pop xr0`, `smallprint` |
| `bitmap(x,y,w,h,data)` | `pop xr0`, `render_bitmap` |
| `render()` / `flip()` | `render`, `render.ddd4` |
| `delay(n)` | `delay` |
| `key()` | `getkey`, `getscancode` |
| `mem_copy` / `mem_set` / `str_copy` … | `memcpy`, `memset`, `strcpy` |

Any name present in the gadget/label table can be called directly, and
`asm { "pop xr0" }` emits a raw gadget call.

The gadget tables are embedded in the binary (`compiler/src/rop_data.hpp`),
derived from the CasioC `hdcompiler` lists. To validate a change, compile an
equivalent low-level RAC source and compare the complete payload byte-for-byte;
the first differing word usually identifies a wrong class byte, a missing
LR/SFR setup, a wrong gadget endpoint, a backup/runtime relocation error, or a
missing string NUL.

## Reference

`docs/rop-notes/rop-580-guide.txt` is the community guide for the nX-U16 ROP
chain (register layout, chain encoding, `printline` / `smallprint` /
`render_bitmap` / `line_draw` ABIs, loop techniques, launcher bytes) and
`docs/rop-notes/rop-ez.txt` is the short "easy ROP" cheat sheet. These are the
ground truth for the ABI implemented here.
