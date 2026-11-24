# Unsigned 16-bit integers in CWiz (CWiz extension)

**Status: real unsigned support in the CWiz C subset only.** The Wiz front end
(the Python-syntax `.wz` language) is a single signed 16-bit cell model and is
**unchanged**. There is no upstream Wiz feature to port: `unsigned int`,
`unsigned char`, `u`/`U` literals, unsigned comparisons, unsigned `/`/`%` and
logical `>>` are all CWiz extensions, written in ordinary C-subset syntax.

The cell stays 16 bits wide. This is *not* a 32-bit integer type: an
`unsigned int` is an unsigned **view** of the same 16-bit storage as `int`.

## Types and storage

| Type | Range | Storage | `sizeof` |
|---|---|---|---|
| `unsigned int` (also spelled `unsigned`) | 0 .. 65535 | 16-bit cell | 2 |
| `unsigned char` | 0 .. 255 | 8-bit byte | 1 |
| `int` (signed, unchanged) | −32768 .. 32767 | 16-bit cell | 2 |
| `char` (signed view, unchanged) | 0 .. 255 on load (zero-extended) | 8-bit byte | 1 |

The `<stdint.h>` typedefs `uint8_t`, `uint16_t`, `uint_least16_t`,
`uint_fast16_t`, `uintptr_t` and `uintmax_t` now have real unsigned semantics;
`<limits.h>` defines `UINT_MAX`, `USHRT_MAX` and `UINT16_MAX` as 65535.

Arithmetic on `unsigned int` wraps modulo 65536; `unsigned char` wraps modulo
256:

```c
unsigned int u = 65535;
u = u + 1;        // u == 0
unsigned char c = 255;
c = c + 1;        // c == 0
```

## Literal typing

| Literal | Type / value |
|---|---|
| unsuffixed 0 .. 32767 | signed `int` |
| unsuffixed 32768 .. 65535 (decimal, `0x` hex, `0b` binary) | unsigned, e.g. `0x8000` is 32768, `0xFFFF` is 65535 |
| `u` / `U` suffix | forces unsigned, even for a small value (`1u` is unsigned) |
| lone `l` / `L` suffix (e.g. `65535L`) | ignored |
| `u` combined with `l`/`L` (e.g. `65535UL`) | unsigned |
| value > 0xFFFF (e.g. `70000`, `0x10000`) | **hard error** `lex.literal-out-of-range`, never a silent wrap |

An unsuffixed high literal is unsigned because a 16-bit signed `int` cannot
represent it. Signed-only programs are byte-identical to the toolchain's
previous behavior.

## Operators

With **at least one unsigned operand**, the usual C conversions apply on the
16-bit cell — the signed operand is converted to unsigned and **unsigned wins**:

| Operator | Signed operands (unchanged) | Any unsigned operand |
|---|---|---|
| `< <= > >=` | signed comparison (`JLTS/JGTS/JLES/JGES`) | 0..65535 magnitude comparison (`JLT/JGE/JGT/JLE`) |
| `/ %` | truncating division, remainder takes dividend's sign (`__div16`) | unsigned divide/remainder (`__udiv16`); divide-by-zero yields 0 |
| `>>` | arithmetic, sign-preserving (`__sra16`) | **logical, zero-fill** (`__shr16`) |
| `+ - * & | ^ <<` | wrap modulo 2¹⁶ | wrap modulo 2¹⁶ (same bits) |
| `== !=` | bit equality | bit equality |

Examples:

```c
int R;
unsigned int u = 65535;
R = u > 0;          // 1  (as signed, -1 > 0 would be 0)
R = -1 > 1U;        // 1  (-1 converts to 65535, which is > 1)
R = 40000U / 3;     // 13333
R = 40000U % 7;     // 2
R = 0x8000U >> 1;   // 16384 (logical; signed -32768 >> 1 would be -16384)
```

## Casts

Casts reinterpret the same 16 bits; no bits change:

```c
(unsigned int)-1 == 65535;   // true
(int)65535u == -1;           // true
```

`unsigned char` promotes to the 16-bit unsigned view with a zero high byte
(no sign extension), so a byte value `0xFE` promotes to 254, not 65534.

## Arrays, structs and ROM tables

The element/field type carries its signedness, so unsigned semantics survive
storage and indexing:

```c
unsigned int a[3];
a[0] = 65535;
int r1 = (a[0] > 100);                       // 1

struct S { unsigned int u; int s; };
struct S p;
p.u = 65535; p.s = 1;
int r2 = (p.u > p.s);                        // 1

rom int t[2] = {0xFFFF, 1000};
int r3 = ((unsigned int)t[0] > t[1]);        // 1
```

The compiler core currently restricts ROM table declarations to `int`
elements; a cast on the loaded word gives it the unsigned 0..65535 view.

## Diagnostics and lint

* Compiler: a literal above 0xFFFF is a hard error with code
  `lex.literal-out-of-range` (printed as `error: ... [lex.literal-out-of-range]`).
* `wizlint`: the compiler error arrives as rule `compile` with that `code`.
  CWiz sources also get **advisory** (Warning/Note, never Error) rules:
  `type/unsigned-negative-initializer`, `type/unsigned-compare-zero`,
  `type/unsigned-unary-minus`, `type/signed-unsigned-mix`,
  `type/unsigned-qualifier-conflict`, `type/unsigned-literal-implicit` and
  `type/unsigned-right-shift`. Run `wizlint --rules` to list them. These rules
  never reject a program the compiler accepts; `.wz` sources never emit them.

## Implementation notes (toolchain maintainers)

* The simulator needed no new opcode: its `cond()` unsigned conditions
  (carry/Z based), its carry/borrow flag maths and its hardware `DIV` were
  already correct. Unsigned `%` and logical `>>` are runtime helpers
  (`__udiv16`, `__shr16`) built from those existing instructions. Unsigned
  literals at or above 0x8000 are materialized with two immediate byte loads,
  because the 16-bit immediate move sign-extends a 7-bit field. Zero extension
  of an unsigned byte is likewise a `MOV <high byte>, #0` in generated code.
* Assembler mnemonics are reused, not extended: unsigned branches are the
  existing `JLT/JGE/JGT/JLE` (there is no `JLO/JHI/JCS`).
* The linter tracks the compiler's canonical internal type strings and uses
  the existing `LintDiagnostic::code` field; no `Diag::warning()` /
  `Diag::note()` signature changed.

## Example and tests

A complete, runnable program with expected screen output is
`examples/cwiz/unsigned_int.cwiz`. The conformance suite entry is
`tests/test_unsigned.cpp`: it covers literal boundaries, wraparound,
`/`, `%`, logical `>>`, mixed conversions, casts, `sizeof`, arrays, struct
fields, `rom` tables, the range error, signed-regression guards, assembler /
simulator flag contracts and the CWiz-only lint rules.
