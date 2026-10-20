# ROP payloads in CWiz syntax

A ROP program is a normal `.cwiz` file with a `#rop` line. The compiler emits a
gadget chain instead of a ROM image, and the emulator injects it into RAM.

```c
#rop
#for 580
#org E9E0        // payload address (default E9E0 on 580, E330 on 880)
#launcher common // "launcher chung" (the default)

int main() {
    screen_del();
    print(0x30, 0x01, "HELLO ROP");   // x, line, text
    render();
}
```

The ROP backend supplies the exact hdcompiler/RAC startup when the source does
not already contain it: `setlr` followed by `setsfr`. `setsfr` initializes the
calculator's SFR/display state. A chain that omits this preamble can inject
successfully and then hang when it reaches display code.

## How it runs

ROP needs two pieces of memory:

1. **Payload** at `#org` — the gadget chain itself. Inject it at `E9E0` on the
   580 and at `E330` on the 880. `#org` and `#launcher` only apply to a `#rop`
   source; normal Wiz/CWiz programs ignore them.
2. **Launcher** — a short chain that makes the calculator start executing the
   payload. The default is the *launcher chung* (common launcher), the one used
   by most existing 580 programs.

### 580 common launcher

```
FD 24 30 30                     trigger + input-area prefix
D0 08 33 30                     launcher setlr_pc
34 7B 31 30                     pop xr0
30 D7 E0 E9                     destination D730, source E9E0
51 94 30 30                     BL memcpy
FE 02                           RAC medium copy-size code
42 07 31 30                     pop er14
2E D7                           execution address - 2
60 0D 32 30                     sp = er14, pop er14
```

The launcher copies the payload from `E9E0` to `D730` and continues there. The
compiler resolves code control flow against `D730`, but data pointers in the
payload remain at the backup address (`E9E0`). Every gadget call keeps RAC's
`30 30` call-class tag, even after the payload is copied. This distinction is essential:
the copied code runs at `D730`, while its string pool is still physically at
`E9E0` unless the chain copies the data separately.

The launcher bytes above are the RAC/hdcompiler pattern used by the working
`basic_draw` examples. The older `pop qr0`/`0x8932` launcher is a different
variant and must not be substituted byte-for-byte.

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
`<name>.launcher.bin`. "Build & Run" also injects both blocks into the running model. Use
**Copy launcher** in the toolbar to put the launcher hex on the clipboard.

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
| `print(x, line, "text")` | `pop xr0`, `line_print` |
| `render()` / `flip()` | `render`, `render.ddd4` |
| `delay(n)` | `delay` |
| `key()` | `getkey`, `getscancode` |
| `mem_copy` / `mem_set` / `str_copy` … | `memcpy`, `memset`, `strcpy` |

Any name present in the gadget/label table can be called directly, and
`asm { "pop xr0" }` emits a raw gadget call.

The gadget tables are embedded in the binary (`compiler/src/rop_data.hpp`),
derived from the CasioC `hdcompiler` lists. To validate a change, compile an
equivalent low-level RAC source and compare the complete payload byte-for-byte;
the first differing word usually identifies a missing LR/SFR setup, a wrong
gadget endpoint, a backup/runtime relocation error, or a missing string NUL.
