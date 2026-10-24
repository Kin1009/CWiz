# LCD status icons (hardware notes)

Notes from hardware inspection. The per-target constant addresses below follow the listed icon
order, skipping `00` slots in the all-on captures.

## fx-580VN X

The status row starts at `0xF800`. Icon order:

`SHIFT ALPHA M VAR_STO MATH_INP DEG RAD GRA FIX SCI ENG IMAG ANGL SIMP LEFT DOWN UP RIGHT PAUSE SUN_CHRG`

All-icons-on capture (hex bytes):

`01 01 01 01 00 01 01 01 01 01 01 01 01 01 00 01 01 01 01 01 00 01 01`

In this all-on capture, `01` means icon on; `00` marks an unused slot, not an icon-off value.
The icon constants map, in the listed order, to the nonzero byte positions; unused positions are
skipped. They are addresses in the SB1 layout, and `setStatus` updates the corresponding SB2 byte.

## fx-880BTG

Icon order:

`SHIFT MATH_INP DEG RAD GRA FIX SCI ENG IMAG ANGL SIMP VERIFY LEFT DOWN UP RIGHT PAUSE SUN_CHRG`

All-icons-on capture beginning at `0xF800` (hex bytes):

`00 01 00 01 01 01 01 01 01 00 01 01 01 01 01 00 01 01 01 01 00 01 01 00`

`01` means icon on; `00` marks an unused slot in this all-on capture. The icon constants map, in
the listed order, to the nonzero byte positions; unused positions are skipped. They are addresses
in the SB1 layout, and `setStatus` updates the corresponding SB2 byte. The 580 and 880 have
different icon sets and layouts.

## API contract

`memoryRead`/`memoryWrite` are implemented as 1- or 2-byte little-endian scalar operations;
`HIGH` is 1 and `LOW` is 0. For arbitrary-length byte ranges, use repeated calls for now.
`setStatus` changes SB2 only; `renderStatus()` flushes the 23-byte status region without copying
the rest of the screen. `render()` remains the full-screen flush.

Example:

```c
setStatus(SHIFT, HIGH);
renderStatus();
```
