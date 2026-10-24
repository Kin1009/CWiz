# ROP payloads in Wiz and CWiz syntax

A ROP program is a `.cwiz` or `.wz` file with a `#rop` line. CWiz sources use
the C-like syntax shown here; Wiz sources use indentation and are translated to
the same ROP backend. The compiler emits a gadget chain instead of a ROM image,
and the emulator injects it into RAM.

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
[ addr_lo, addr_hi, 0x30 | segment, variant ]
```

The nX-U16 code space is 20 bit: a four-bit code segment plus a 16-bit program
counter. The third byte carries the segment (`0x30 | segment`), so an address
above `0xFFFF` keeps its segment: `printline` at `0x23DC8` is `C8 3D 32 30`,
not `C8 3D 30 30`. The fourth byte is the call variant: `0x30` calls and links,
`0x48` branches without linking (used by the launcher's last word).

| bytes | meaning | example |
|---|---|---|
| `xx xx 3s 30` | gadget call, pop or flow in segment `s` | `34 7B 31 30` = pop `0x17B34` |
| `xx xx 3s 48` | branch without link (launcher jump) | `74 1F 32 48` |

The third byte does not encode whether a gadget is a call, a pop or a flow
gadget; those are all ordinary chain words. Only the fourth byte changes the
call variant.

The class bytes are **derived from the address segment**, not from the gadget
kind. Never add a fixed `0x30300000` to the address: that produces the same
third byte for every gadget and corrupts every address at or above `0x10000`.

Branch targets are stored as `label - 2`: the `sp = er6, pop er8` gadget loads
the PC after pre-incrementing the stack pointer, which is why the community
tables write `adr_of [-2] label`. Both `goto` and `keytable` emit this bias.

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
| `print(pad, line, "text")` | `printline` `0x23DC8` | `XR0 = line, pad, string` |
| `print_small(font, line, "text")` | `smallprint` `0x23DCC` | `XR0 = font, line, string` |
| `line(x1,y1,x2,y2)` | `line_draw` `0x08E62` | `XR0 = x1, y1, x2, y2` |
| `rect(x,y,w,h)` | `line_draw` `0x08E62` | four calls, `x+w` / `y+h` for the far edges |
| `pixel(x,y,color)` | `pixel_draw` `0x091FC` | `XR0 = x, y, color, 0` (0 black, 1 white) |
| `bitmap(x,y,w,h,data)` | `render_bitmap` `0x09848` | `XR0 = x, y, w, h`, then `ER0 = data` |
| `screen_del()` | `buffer_clear` `0x08C60` | — |
| `render()` | `render.ddd4` `0x0947E` | — |
| `setlr()` | `setlr` `0x09FA8` | — |
| `setsfr()` | `setsfr` `0x0A0E0` | — |
| `delay(ticks)` | `delay` `0x09F3C` | `ER0 = ticks`, `0x1F40` ≈ 1 s |
| `waitshift()` | `waitshift` `0x23DDE` | blocks until SHIFT |
| `getkey()` | `getkey` `0x2F5EA` | clears an inline slot, writes KI/KO and leaves the word in ER0 |
| `getscancode()` | `getscancode` `0x1F24E` | blocking variant |
| `getscancode_nodelay()` | `getkey` `0x2F5EA` | same as `getkey` |
| `keytable(k, label, ..., 0, default)` | `cmp_ea` `0x09C20` | compares the ER0 scancode with the table; jumps via `er6 = [ea]` |
| `set_byte(addr, value)` | `[er0]=r2` `0x203D2` | `XR0 = addr, value, 0` |
| `set_word(addr, value)` | `[er0]=er2,rt` `0x139D8` | `XR0 = addr, value` |
| `add_word(addr, delta)` | `[er8]+=er2,pop xr8` `0x09CA0` | `ER2 = delta`, `ER8 = addr` |
| `memcpy(dst, src, len)` | `memcpy_length_rn/8` `0x10F20` | `QR8 = dst, src, len, 0` |
| `strcpy(dst, src)` | `BL strcpy` `0x203CC` | `XR0 = dst, src` |
| `calc_func(ptr, result)` | `calc_func` `0x17922` | `XR0 = token pointer, NUM result` |
| `num_to_str(num, text)` | `num_to_str` `0x279B6` | `XR0 = NUM, destination text` |

`pad` is normally `0x30`; `line` is the row byte (`0x01`, `0x11`, `0x21`, `0x31`
for lines 1..4); `font` is `0x08` (small), `0x0A` (medium) or `0x0E` (large).
Only the large font can render Vietnamese text. `print` uses the large font and
is limited to 17 characters per line.

### Keyboard tables

`getkey()`/`getscancode()` write a two-byte KI/KO word. `keytable` compares
that word with a `cmp_ea` table and jumps to the matching label:

```c
getkey();
keytable(0x0480, up,      // up
         0x0840, down,    // down
         0x0000, loop);   // default (cmp_ea stops at 00 00)
```

The word is little-endian: the community listings write the bytes KI then KO,
so `hex 80 04` is `0x0480`. Arrow keys are `up 0x0480`, `down 0x0840`,
`left 0x0440`, `right 0x0880`; SHIFT is `0x0180`, ALPHA `0x0280`.

`getkey()` emits the community inline-slot sequence. The slot is cleared
before each read because `getkey` leaves it unchanged when no key is down; the
community loops re-copy the payload from backup, but a compiler `goto` does not:

```
setlr           ; a preceding print/render BL may have clobbered LR
pop xr0
<slot>, 0000    ; ER0 = slot, ER2 = 0
[er0]=er2,rt    ; clear the 2-byte KI/KO slot
pop er0
<slot>          ; ER0 = address of the 2-byte KI/KO slot
getkey          ; writes the scancode to [ER0]
setlr           ; establish LR before the final pop
pop er0         ; loads the value getkey just wrote
slot: 00 00     ; inline data consumed by the pop above
```

The final `pop er0` is important: it loads the **scancode value**, not the
slot address. The clear makes a released key read as `0x0000`, so the
`keytable()` default branch runs instead of repeating the last key. Emit
`keytable()` immediately after `getkey()`; any intervening call can overwrite
ER0. `keytable()` then only loads EA with the table address and runs
`cmp_ea`. The table is part of the payload, so its address uses the execution
address (the community `ea = adr_of table`), and its entries store `label - 2`.
`cmp_ea`, `pop ea` and `er6 = [ea]` exist in the 580 table only.
`ea_switchcase` is accepted as another name for `cmp_ea`, and `reset_sfr`
for `setsfr`; both spellings appear in the community chat.

Known limitation: the 880 table has no `cmp_ea`, `pop ea` or `memcpy_length_rn/8`;
`keytable` and `memcpy` are 580-only for now.

## 580 common launcher

The 26 byte *launcher chung* is byte-identical to the one used by the existing
community programs:

```
FD 24 30 30                     trigger + input-area prefix
DA 7B 31 30                     copy-with-length gadget
FE 02                           copy length 0x02FE (766 bytes, default)
E0 E9                           source E9E0 (backup area)
30 D7                           destination D730 (execution area)
2E D7                           execution address - 2
32 89 31 30                     pop 0x8932
30 30                           pad
74 1F 32 48                     sp = er6, pop er8 (branch)
```

The copy length is selected by the RAC length code: `01` = `0x01FE` (510
bytes), `02` = `0x02FE` (766 bytes, default), `03` = `0x03FE` (1022 bytes). The
payload must fit in the selected length.

The launcher copies the payload from the backup area (`E9E0`) to the execution
area (`D730`) and continues there. Code labels, branch targets and the
`keytable` therefore use the execution address `D730`; string literals are
resolved against the backup copy at `E9E0`, which remains valid. The bytes
themselves are injected at `E9E0`. The `pop xr0` / `BL memcpy` launcher is a
different variant and must not be substituted byte-for-byte.

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
#pad 128                        // trailing null buffer size (default 256)
#nopad                          // no trailing null buffer
```

"Build" writes `wiz/<name>.bin` (payload) and prints the launcher as hex in the
log. The standalone `wizc` driver writes the launcher separately as
`<name>.launcher.bin`. "Build & Run" also injects both blocks into the running
model. Use **Copy launcher** in the toolbar to put the launcher hex on the
clipboard.

## Syntax

CWiz ROP syntax includes `int` globals, labels, `goto`, `asm { "gadget" }`,
and the supported API calls. Wiz ROP syntax supports indentation-based function
definitions, labels, and calls. Dynamic `if`/`while` and range loops are
diagnosed instead of silently emitted as unconditional flow. Addresses use
`[HEX]` (and `0xHEX` for plain numbers).

Builtins are mapped to the gadget database (580 and 880):

| call | gadgets tried |
|---|---|
| `screen_del()` / `cls()` | `buffer_clear`, `memzero` |
| `line(x1,y1,x2,y2)` / `draw_line` | `line_draw` |
| `rect(x,y,w,h)` / `draw_rect` | four `line_draw` calls |
| `pixel(x,y,color)` / `draw_pixel` | `pixel_draw` |
| `print(pad, line, "text")` / `print_line` | `pop xr0`, `printline` (880: `line_print`) |
| `print_small(font, line, "text")` | `pop xr0`, `smallprint` |
| `bitmap(x,y,w,h,data)` / `draw_bitmap` | `pop xr0`, `render_bitmap`, `pop er0`, `render` |
| `render()` / `flip()` | `render`, `render.ddd4` |
| `delay(n)` / `sleep` | `pop er0`, `delay` |
| `waitshift()` | `waitshift` |
| `getkey()` / `getscancode()` / `getscancode_nodelay()` | `setlr`, `pop xr0`, clear slot, `pop er0`, inline slot, `getkey` or `getscancode`, `setlr`, `pop er0` |
| `keytable(...)` / `table(...)` | `pop ea`, `cmp_ea`, `er6 = [ea]`, `sp=er6,pop er8` |
| `set_byte(addr,v)` / `poke` | `pop xr0`, `[er0]=r2` |
| `set_word(addr,v)` / `poke_word` | `pop xr0`, `[er0]=er2,rt` |
| `add_word(addr,delta)` / `add16` | `pop er2`, `pop er8`, `[er8]+=er2,pop xr8` |
| `memcpy(dst,src,len)` / `mem_copy` | `pop qr8`, `memcpy_length_rn/8` |
| `strcpy(dst,src)` / `str_copy` | `pop xr0`, `BL strcpy` (880: `strcpy`) |
| `calc_func(ptr,result)` / `calc` | `pop xr0`, `calc_func` |
| `num_to_str(num,text)` | `pop xr0`, `num_to_str` |

Any name present in the gadget/label table can be called directly, and
`asm { "pop xr0" }` emits a raw gadget call.

The gadget tables are embedded in the binary (`compiler/src/rop_data.hpp`),
derived from the CasioC `hdcompiler` lists. Calls with an unregistered argument ABI fail
compilation rather than appending guessed stack data. `keytable()`, `memcpy()`, `add_word()`,
`calc_func()`, and `num_to_str()` currently have verified implementations on the 580 only; the 880
table lacks their required gadgets/ABIs, and the compiler reports those target limitations.
For validation, compare the complete payload byte-for-byte against an equivalent low-level RAC
source; the first differing word usually identifies the ABI, segment, setup, relocation, or
string-pool mismatch.

## Reference

`docs/rop-notes/rop-580-guide.txt` is the community guide for the nX-U16 ROP
chain (register layout, chain encoding, `printline` / `smallprint` /
`render_bitmap` / `line_draw` ABIs, loop techniques, launcher bytes), and
`docs/rop-notes/rop-ez.txt` is the short "easy ROP" cheat sheet. The ABI
details implemented here are recorded above and covered by the compiler tests.
