# cwapp RAM exclusions — machine-checkable summary

> Authoritative source: `compiler/src/targets.cpp`, `TARGET_CWAPP`
> (`var_base = 0xE820`, `var_limit = 0xEE00`, 13 inclusive `reserved_ram`
> entries). This file is the concise, checkable copy used by the verifiers in
> `cwiz_os/tools/`. The full rationale is in
> [../../docs/CWAPP.md](../../docs/CWAPP.md#cwapp-ram-map-and-exclusions).

CWiz is the C-subset language of wizc: pins are written as
`[[at 0xE824]] int x;`, never as Wiz `rom x = [...]`. The addresses below are
flat 16-bit RAM addresses; every range is inclusive.

## The 13 entries

| # | Range | Bytes | Class |
| --- | --- | ---: | --- |
| 1 | `0xD000..0xD16B` | 364 | forbidden |
| 2 | `0xDBD0..0xDBDF` | 16 | forbidden |
| 3 | `0xDBE0..0xDBE7` | 8 | forbidden |
| 4 | `0xDDD3..0xE5D2` | 2048 | forbidden |
| 5 | `0xEDE0..0xEFFF` | 544 | forbidden |
| 6 | `0xF000..0xF04F` | 80 | forbidden |
| 7 | `0xF220..0xF224` | 5 | forbidden |
| 8 | `0xF800..0xFFFF` | 2048 | forbidden |
| 9 | `0xF0C3` | 1 | immutable |
| 10 | `0xF0D1` | 1 | immutable |
| 11 | `0xF310` | 1 | immutable |
| 12 | `0xF100..0xF21F` | 288 | guide-unmapped |
| 13 | `0xF311..0xF7FF` | 1263 | guide-unmapped |

Byte invariant (asserted by `self_check()` on every verifier run):

```
sum(entries 1..8)   = 5113 forbidden bytes
sum(entries 9..11)  =    3 immutable addresses
remaining           = 7172 allowed bytes
total               = 5113 + 3 + 7172 = 12288 = 0x3000  (0xD000..0xFFFF)
```

Entries 12 and 13 are counted inside the 7172 allowed bytes: they are
hardware-addressable but excluded conservatively because peek/poke faults
there. The 13 entries are mutually non-overlapping and all lie in RAM.

## Allowed windows

| Use | Window |
| --- | --- |
| App stack (grows down from `0xDBCE`) | `0xD16C..0xDBCF` |
| Globals / `[[at]]` pins | `0xE820..0xEDDF` |
| Dynamic heap (watermark..high) | `..0xEDE0` |
| Exit save area | `0xE5D3..0xE7FF` |

## Checks

```console
$ python3 cwiz_os/tools/verify_cwapp_exclusions.py --self-check-only build/app.asm
cwapp exclusion table OK: 5113 forbidden + 3 immutable + 7172 allowed = 12288 bytes
$ python3 cwiz_os/tools/verify_cwapp_exclusions.py build/app.asm
cwapp RAM exclusion verification OK: ... 0 forbidden, 0 immutable, 0 guide-unmapped hits
$ python3 cwiz_os/tools/verify_ram_layout.py --target cwapp \
      --source cwiz_os/main.cwiz --compiler build/wizc
RAM layout OK (cwapp): ... no reserved-span overlap
```

The first tool parses the allocator's decimal `L/ST/LD/STD Rn|ERn, <address>`
operands from the assembly listing (or the `.bin`'s companion `.asm`, or a
symbol/address report) and exits nonzero on any hit. Template SFR accesses
(`st r0, 0F041H`) and deliberate `poke()` constants are hardware writes, not
allocator placements, and are intentionally not flagged. The second tool
shares the same 13-entry table by import, so the two cannot drift.
