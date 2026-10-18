# tools

Reference material used when bringing up the toolchain. There is no code here any more: the
one-off generator/checker scripts that used to live in this folder were folded into the C++
sources (see below), and the vendored original documents are kept for provenance.

## `originals/`

| file | what it is |
| --- | --- |
| `ML620906.DCL` | The official Lapis **ML620Q906** register definition file (symbolic SFR / bit names). |
| `ML620Q906.asm` | The official **ML620Q906** boot/startup listing, written with symbolic SFR names and `LEA`. |

These are the primary sources for `compiler/template_ML620906.asm`. The shipped template is the
same startup sequence, rewritten in the flat style of `compiler/template_ML620909.asm`: every
symbolic register name resolved to a raw address via the DCL, and `LEA` expanded by hand.

Four names used by the 906 startup are **not defined in the DCL we have**; they were inferred and
are flagged in the header of `compiler/template_ML620906.asm`:

| symbol | address | evidence |
| --- | --- | --- |
| `XTM0` | `0F00AH.0` | `FCON.0 = LOSCEN` in the DCL; the 909 boot does the same thing as `ST 81H -> 0F00AH`. |
| `XTM1` | `0F00AH.1` | `FCON.1 = LOSCM` in the DCL; the 909 boot clears `0F00AH.1` in the same spot. |
| `KIMASK` | `0F042H` | `libcw.h KeyboardInMask = 0xF042` (the DCL calls it `KIMOD`); the 909 boot writes `0` there. |
| `KOMASK` | `0F044H` | `libcw.h KeyboardOutMask = 0xF044` (the DCL calls it `KOCON`); the 909 boot writes `80H,0FFH` there. |

## What happened to the old scripts

* **`gen_template906.py`** — translated `originals/ML620Q906.asm` into
  `compiler/template_ML620906.asm`. The output is checked in; the table above records the only
  judgement calls it made.
* **`check_core_tables.py`** — cross-checked the *nX-U8/100* and *nX-U16/100* instruction manuals
  (Chapter 4 appendix) and confirmed that both cores use **one and the same 16-bit opcode
  encoding**; only the flag columns differ in the manuals. The C++ assembler
  (`compiler/src/asm.cpp`) relies on that equivalence, taking a `core` selector purely to pick the
  right operand/stack width.
* **`patch_menu.py`** — became the `patch_menu` driver,
  `compiler/src/main_patch_menu.cpp`.
* **`package_extract.py`** — its `config.bin` reader became `compiler/src/modelinfo.hpp`.

The CasioEmuMsvc emulator itself lives in `emulator/` (macOS-only, not part of the compiler); see
`emulator/BUILD-MACOS.md`.
