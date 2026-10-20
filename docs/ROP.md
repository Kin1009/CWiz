# ROP payloads in CWiz syntax

A ROP program is a normal `.cwiz` file with a `#rop` line. The compiler emits a
gadget chain instead of a ROM image, and the emulator injects it into RAM.

```c
#rop
#for 580
#org E9E0        // payload address (default E9E0)
#launcher 8154   // launcher address (default 8154, the overflow entry)

int main() {
    screen_del();
    print(0x30, 0x01, "HELLO ROP");   // x, line, text
    render();
}
```

## How it runs

ROP needs two pieces of memory:

1. **Payload** at `#org` — the gadget chain itself.
2. **Launcher** at `#launcher` — a 10-byte stub that moves SP to the payload
   and continues the chain there:

   ```
   78 5C 31 30   pop er14
   <payload-2>   little-endian
   60 0D 32 30   sp = er14, pop er14
   ```

   After the stub, SP points exactly at the payload, so the gadget chain runs
   from `#org`.

"Build & Run" writes both blocks and sets PC to the launcher. Build only writes
`wiz/<name>.bin` (payload) and prints the launcher address in the log.

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
derived from the CasioC `hdcompiler` lists.
