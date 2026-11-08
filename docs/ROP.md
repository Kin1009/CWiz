# ROP payloads in Wiz and CWiz syntax

A ROP program is a `.cwiz` or `.wz` file with a `#rop` line. CWiz sources use
the C-like syntax shown here; Wiz sources use indentation and are translated to
the same ROP backend. `wizc` emits RAC source and compiles it with the external
[RAC-Compiler](#rac-compiler-required-external); the result is a gadget chain
instead of a ROM image, and the emulator injects it into RAM.

```c
#rop
#for 580
#org 0xE9E0        // payload address (default E9E0 on 580, E330 on 880)
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
580, `9268` on the 880) and started with **EXE** (`=`). On the 580, LEFT is an
ordinary arrow key: it moves the input cursor and is handled by the stock
calc-mode handler (`2:E234`), so "LEFT then EXE" can evaluate the wrong input
line. LEFT+EXE is only meaningful for the 880/program-list variant.

## Chain encoding

Every gadget is four bytes. RAC's `call` writes the address as a plain 32-bit
little-endian value:

```
[ addr_lo, addr_hi, segment, 00 ]      payload (D730/E330)
[ addr_lo, addr_hi, 0x30|segment, 30 ] launcher (input area D180/9268)
```

The launcher uses the calculator's input-area token class (`0x30`); the payload
does not need it. The nX-U16 code space is 20 bit: a four-bit code segment plus
a 16-bit program counter. A gadget above `0xFFFF` keeps its segment, so
`printline` at `0x23DC8` is `C8 3D 02 00` in the payload (`C8 3D 32 30` when
typed as a launcher token).

| bytes | meaning | example |
|---|---|---|
| `xx xx 0s 00` | payload gadget call in segment `s` | `34 7B 01 00` = pop `0x17B34` |
| `xx xx 3s 30` | input-area token call in segment `s` | `34 7B 31 30` = pop `0x17B34` |
| `xx xx 3s 48` | branch without link (old launcher form) | `74 1F 32 48` |

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
| `wait_shift()` | `waitshift` `0x23DDE` | alias of `waitshift()` |
| `waitshift_striped()` | `waitshift_striped` `0x23DDA` | striped SHIFT wait |
| `getkey()` | `getkey` `0x2F5EA` | **ROP entry** (`BL ENTER`); writes the KI/KO word to `[ER0]`; the following `pop er0` loads it |
| `getkeycode()` | `getkey` `0x2F5EA` | same raw read; `0x2F5E8` is the callable (`PUSH LR`) entry, not for chains |
| `getscancode()` | `getscancode` `0x1F24E` | blocking variant; **ROP entry** (`BL ENTER`); `0x1F24C` is the callable (`PUSH LR`) entry, registered as `getscancode_call` for diagnostics |
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

`getkey()` follows the compiler's inline-slot pattern:

The compiler emits a runtime clear before every read (both 580 and 880):

```
setlr
pop xr0                 ; ER0 = slot address, ER2 = 0
<slot>, 0000
[er0]=er2,rt            ; clear the 2-byte KI/KO slot
pop er0                 ; ER0 = slot address
<slot>
getkey                  ; 2:F5EA (ROP entry; 2:F5E8 is the PUSH LR call entry)
                        ; writes the raw KI/KO word to [ER0]
setlr
pop er0                 ; ER0 = the word just written
slot: 00 00             ; inline data; tests assert these bytes
```

The slot is emitted as `00 00`, not the older community `0x3030` marker.
`getkey`/`getkeycode` leave `[ER0]` unchanged when no key is pressed, so the
run-time clear is required; otherwise a compiler loop re-dispatches the last
key forever (see `firmware-re/fx580/proposals/rop-getkey.md`). Give the table
a `(0x0000, loop)` default entry. `keytable()` then does
`pop ea`/`cmp_ea`/`er6=[ea]` and emits `setlr` before `sp=er6,pop er8`. The
community form also disables interrupts with `setlr_pc; di,rt` first; the
compiler keeps `setlr`-only until a real-device test confirms the interrupt
state (TODO, do not change without hardware evidence).
Tables use the raw KI/KO words: `up 0x0480`, `down 0x0840`,
`left 0x0440`, `right 0x0880`. The matching FCxx IDs are `FC1C=up`,
`FC1D=down`, `FC1E=right`, `FC1F=left`; the old ROP notes had this
transposed (see [`firmware-re/fx580/docs/rop-keymap.md`](../firmware-re/fx580/docs/rop-keymap.md)).

### `key()` in a ROP payload

The cwapp `key()` builtin is not an OS routine: it strobes `F046` and reads
`F040` directly (the same loop as `runtime.asm __key`).  ROP `key()` appends
that routine to the payload and chain-calls it, so it needs no `D150` buffer,
timer, interrupt or OS state.  It returns the 0..63 scan index in `ER0`, or
`-1` when no key is down; `getkey()`/`getscancode()` remain available when the
raw KI/KO word is wanted.  The chain word is
`[routine_lo, routine_hi, 0x30 | segment, 0x30]`, where the routine lives at
`home + payload_offset` (for the 580 common launcher, `home = 0xD730`).

Caveat: the `er6 = [ea]` step is `0x1C64A`, which also writes
`ER14, R13..R6` to `D000..D009` and clobbers `R6..R13`/`ER14`. Do not keep
live ROP variables or the scancode argument block at `D000..D009` across a
`keytable` dispatch; the compiler's `var_base` is `D000`.

Known limitation: the 880 table has no `cmp_ea`, `pop ea` or `memcpy_length_rn/8`;
`keytable` and `memcpy` are 580-only for now.

## 580 common launcher

`#launcher common` is emitted as RAC source and compiled by RAC. It is the
launcher used by the community examples:

```
FD 24 30 30                     trigger + input-area prefix
A8 9F 30 30                     setlr
E0 A0 30 30                     setsfr
34 7B 31 30                     pop xr0
<dst> <src>                     pr_org(main), pr_backup(main)
51 94 30 30                     call 09451 (memcpy)
FE <code>                       RAC length code 01/02/03
42 07 31 30                     pop er14
<main - 2>                      execution address - 2
60 0D 32 30                     sp = er14, pop er14
```

The copy length is selected by the RAC length code: `01` = `0x01FE` (510
bytes), `02` = `0x02FE` (766 bytes), `03` = `0x03FE` (1022 bytes). The emitter
picks the smallest code that fits the payload.

The launcher copies the payload from the backup area (`E9E0`) to the execution
area (`D730`) and continues there. Code labels, branch targets and the
`keytable` therefore use the execution address `D730`; string literals are
resolved against the backup copy at `E9E0`, which remains valid.

### 880 common launcher

The 880 payload executes in place (`E330`), so the launcher stays the 8-byte
stub: `FD 26`, `pr_backup(main) - 2`, `C0 75 32 30`. The source is still RAC
source (`hex fd 26`, `eval(...)`, `call 3275C0`) and RAC emits the bytes.

### Simple launcher

`#launcher simple` (or `#launcher <address>`, for compatibility) emits the
RAC stub instead. Without an explicit address the launcher goes to the program
input area (`D180` on the 580, `9268` on the 880):

```
pop er14
eval(pr_org(main) - 2)
sp = er14, pop er14
```

After the stub, SP points exactly at the payload.

### Options

```
#launcher common                 // RAC launcher (length code chosen to fit)
#launcher simple                 // RAC pop-er14 stub
#launcher 8154                   // stub placed at an explicit address (must be RAM)
#launcher common E9E0 D730       // explicit backup/main addresses
#pad 128                        // trailing null buffer size (default 256)
#nopad                          // no trailing null buffer
```

"Build" writes `wiz/<name>.bin` (payload) and `wiz/<name>.asm` (listing) and
prints the launcher as hex in the log. The standalone `wizc` driver writes the
launcher separately as `<name>.launcher.bin`. "Build & Run" also injects both
blocks into the running model. Use **Copy launcher** in the toolbar to put the
launcher hex on the clipboard.

## RAC-Compiler (required, external)

RAC-Compiler ([luongvantam/RAC-Compiler](https://github.com/luongvantam/RAC-Compiler))
is the **only** ROP backend. It is a separate GPL-3.0 program: `wizc` runs
`rac.py` as a separate process and never links or copies its code. The licence
boundary is the process boundary.

ROP compilation therefore needs a RAC checkout:

```sh
tools/fetch_rac.sh                  # clone into tools/rac-compiler (gitignored)
wizc hello.cwiz                     # RSC -> RAC -> payload + launcher
wizc --emit-rsc hello.cwiz          # write the RAC source only
wizc --rac /path/to/rac.py hello.cwiz
RAC_COMPILER=/path/to/rac.py wizc hello.cwiz
```

`wizc` writes the RAC-compiled payload at its backup address (`E9E0` on the
580, `E330` on the 880) to `<name>.bin`, and the RAC `@section.launcher` to
`<name>.launcher.bin` (`D180`/`9268`). It also keeps `<name>.rsc` next to
them. There is **no fallback to a native emitter**: if RAC is missing, Python
is missing, or the compiler reports an error, `wizc` stops with a diagnostic
and writes no image.

`--backend rac` is accepted for existing scripts and is the only value.
`--rac-launcher` is accepted but always on: the launcher now always comes from
`@section.launcher`.

### Launcher shapes

`#launcher` selects the RSC launcher:

* `#launcher common` (default) — the community/RAC launcher: trigger
  `FD 24 30 30`, `setlr`, `setsfr`, `xr0 = pr_org(main), pr_backup(main)`,
  `call 09451`, `hex fe 0N` (510/766/1022 bytes), then
  `er14 = eval(pr_org(main) - 2)` and `sp = er14, pop er14`.
* `#launcher simple` — the 10-byte `pop er14` stub.
* `#launcher <address>` — the stub at an explicit input-area address.

The addresses are RAC section symbols, so the RAC compiler resolves the copy
source, destination and jump target itself.

### Emitting RAC source

`--emit-rsc` writes the source that RAC compiles. For ordinary CWiz code the
emitter uses RAC idioms: `@section.main at … backup …`, `lbl`, `setlr`,
`setsfr`, `buffer_clear`, `line_print`, `render.ddd4`, `xr0 = … , adr(text)`,
`call <address>`, string literals, key tables as word/label pairs and
`eval(adr(label) - 2)` branch targets.

Some programs are already byte-verified as hand-written RAC. A `.cwiz` source
may pass a complete RAC document through unchanged:

```c
#rop
#for 580
#org 0xE9E0
#launcher common

#raw-rsc {
@section.main at 0xd730 backup 0xe9e0
lbl start
    setlr_pc
    setsfr
...
@section.launcher at 0xd180
hex fd 24 30 30
setlr
...
}
```

The compiler validates `#org` (the `@section.main` backup address) and the
`#launcher` address, then hands the text to RAC verbatim. This is how
`examples/rop/arrow_keys.cwiz` is kept identical to the hardware-verified
program: 232 payload bytes at `E9E0` (`d0 08 03 00 e0 a0 00 00 …`) and the
36-byte launcher at `D180`:

```
fd 24 30 30 a8 9f 30 30 e0 a0 30 30 34 7b 31 30
30 d7 e0 e9 51 94 30 30 fe 01 42 07 31 30 2e d7
60 0d 32 30
```

`wiztests` pins both blocks by SHA-256:

| block | bytes | SHA-256 |
|---|---|---|
| payload | 232 | `3472dd6dd095bd3305faf0563501b0c34d474eccdbc3972457409290f8f5da56` |
| launcher | 36 | `d47950caf9b6e48d5a8e7097ad3eefc411baa5b31751cf2e4a97d3b2d9be95d0` |

## Running from the emulator

"Build & Run" for a ROP program always goes through RAC. It writes
`<name>.bin`, `<name>.asm` and `<name>.rsc`, pauses the model, writes the
payload and the RAC launcher, verifies both blocks, and resumes. Without RAC it
shows an actionable error (`tools/fetch_rac.sh`, or `RAC_COMPILER`) and does
not build.

A Finder-launched app cannot rely on the working directory. RAC is found in
this order: `$RAC_COMPILER`, `tools/rac-compiler/rac.py` relative to the
working directory (and its parents), `$WIZ_ROOT/tools/rac-compiler/rac.py`, then
a bundle copy under the Wiz data directory (`<data>/tools/rac-compiler/rac.py`).
Shipping `tools/rac-compiler` as `Resources/wiz/tools/rac-compiler` in the app
makes the backend available without environment variables.

A launcher placed in the program input area (`D180` on the 580, `9268` on the
880) is **firmware data, not CPU code**: the calculator starts it with its
program/expression interpreter when you press **EXE**. Setting PC to that
address would execute the ROM at the same address instead, so the debugger does
not do that; it verifies both blocks and tells you to press EXE. Launchers
placed outside the input area are transferred to with PC (word address =
`base / 2`).

The `.rsc` source is written in the community ROP style, so it can be read
next to the hand written examples:

```
@section.main at 0xd730 backup 0xe9e0

lbl main
    setlr                   # 09FA8
    setsfr                  # 0A0E0
    buffer_clear            # 08C60
    pop xr0                 # 17B34
    hex 01 30
    eval(adr(str_1)+dist.main) # EAFA
    printline               # 23DC8
    render.ddd4             # 0947E
    pop er6                 # 13AB0
    eval(adr(keys) - 2)     # D806
    sp = er6,pop er8        # 21F74
lbl keys
    pop er0                 # 12602
    adr(keyslot)            # D81A
    setlr                   # 09FA8
    getkey                  # 2F5EA
    pop er0                 # 12602
lbl keyslot
    hex 00 00
...
lbl keytable_1
    hex 80 04               # KEY_UP
    eval(adr(up) - 2)       # D78E
lbl str_1
    # "PRESS A KEY"
    hex 50 52 45 53 53 20 41 20
```

`lbl` marks a label, gadget words keep their mnemonic (with the ROM address in
the comment), and plain bytes stay `hex`. A data word that is an address is
printed as `adr(label)` (code target), `adr(label) - 2` (a target stored for
`sp = er6, pop er8`, which pre-increments the stack) or
`eval(adr(label)+dist.main)` (a pointer into the injected copy). Key table
entries stay raw bytes with the community name as a comment (`hex 80 04 # KEY_UP`),
so the listing keeps the same bytes whatever the assembler calls the key, and
the string pool and null buffer get `lbl str_N` / `lbl pad`. The whole file is a source: comments are `#`. The resolved
numbers and the compiler notes follow the listing after `# ---- notes ----`.

## Syntax

CWiz ROP syntax includes `int` globals, labels, `goto`, `asm { "gadget" }`,
and the supported API calls. Wiz ROP syntax supports indentation-based function
definitions, labels, and calls. Dynamic `if`/`while` and range loops are
diagnosed instead of silently emitted as unconditional flow. Addresses are
written `0x…` (`#org 0xE9E0`). Bare hex (`#org E9E0`) and bracket forms
(`#org [E9E0]`) are still accepted so older community sources keep building, but
they are compatibility aliases — new payloads must use `0x`. See
[DIRECTIVES.md](DIRECTIVES.md) for the shared number grammar and the spellings
`wizlint` currently detects.

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
