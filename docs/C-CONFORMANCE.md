# CWiz C conformance matrix

Target: ISO C23 (ISO/IEC 9899:2024) on a freestanding nX-U16/100
(ML620906 / fx-580VN X) or nX-U8/100 (ML620909 / fx-880BTG).  There is no OS,
no threads, no heap owned by the C runtime and no host libc.

Status meanings:

* **Full** — syntax, semantics and tests in the simulator.
* **Semantic subset** — accepted and useful, with a documented restriction.
* **Syntax-only** — parsed, deliberately no runtime effect on this target.
* **N/A (target)** — impossible/meaningless on this freestanding target; the
  reason is recorded.
* **Missing** — not implemented yet; a row may not stay here at terminal.

Every row names the test or experiment that proves it.  This file is updated
every pass.

## Core C11/C17

| Feature | Std | Status | Tests | Notes |
|---|---|---|---|---|
| `int`, `char`, `void`, `bool` declarations | C89/C99 | Full | `test_cwiz.cpp` | all values 16-bit; `char` is 8-bit storage, zero-extended |
| `short`, `signed`, `unsigned`, `long` spellings | C89 | Syntax-only | `test_cwiz.cpp` | accepted and ignored; 16-bit model |
| pointers, `&`, `*`, pointer arithmetic | C89 | Full | `test_cwiz.cpp` | 16-bit addresses; element stride from type |
| arrays, multidimensional arrays | C89 | Full | `test_cwiz.cpp` | flat layout, one index per dimension |
| structs, unions, typedefs | C89 | Semantic subset | `test_cwiz.cpp` | packed/aligned layout; no struct params/returns/assignment by value |
| bitfields | C89 | Syntax-only | `test_conformance.cpp` | declared; member access reports a targeted error |
| anonymous struct/union members | C11 | Full | `test_conformance.cpp` | flattened into the parent layout |
| enums, fixed underlying type | C89/C23 | Semantic subset | `test_cwiz.cpp` | enum is `int`; fixed-underlying syntax pending |
| `const`, `volatile`, `restrict` | C89/C99 | Syntax-only | compiler warnings | qualifiers do not change codegen |
| `static`, `extern`, `register`, `inline` | C89/C99 | Syntax-only | compiler warnings | one translation unit; no linker |
| `_Static_assert` / `static_assert` | C11/C23 | Full | `test_conformance.cpp` | one and two argument forms |
| `_Alignas`, `_Alignof`, `alignas`, `alignof` | C11/C23 | Semantic subset | `test_conformance.cpp` | alignment honoured by the static allocator |
| `_Generic` | C11 | Semantic subset | `test_conformance.cpp` | controlling type is the static CWiz type |
| `_Atomic`, `_Thread_local` | C11 | Syntax-only | `test_conformance.cpp` | parsed; no threads/atomics on target |
| `_Noreturn`, `[[noreturn]]` | C11/C23 | Syntax-only | `test_conformance.cpp` | accepted and ignored |
| designated initializers | C99 | Full | `test_conformance.cpp` | struct `.field` and array `[index]` |
| compound literals | C99 | Semantic subset | `test_conformance.cpp` | scalar and struct, inside functions |
| flexible array members | C99 | Syntax-only | `test_conformance.cpp` | last member accepted, contributes no size; access is reported |
| variadic functions | C89 | Semantic subset | `test_conformance.cpp` | hidden count + up to 16 extra args; no nested variadic frames |
| function pointers | C89 | Semantic subset | `test_conformance.cpp` | variable/typedef declarator, assignment and indirect call; no signature checking |
| `goto` and labels | C89 | Full | `test_cwiz.cpp` | function-scoped labels, forward/backward |
| `do`/`while`, `switch`, `break`, `continue` | C89 | Full | `test_cwiz.cpp` | C fall-through |
| local `const` | C89 | Full | `test_cwiz.cpp` | compile-time constant |
| casts | C89 | Full | `test_cwiz.cpp` | `(char)` truncates; pointer/int casts are 16-bit |
| `sizeof` | C89 | Full | `test_cwiz.cpp` | type, expression, whole array |
| `?:`, compound assignment, `++`/`--` | C89 | Full | `test_cwiz.cpp` | |
| string literals, `\x`, `\u`, `\U` | C89/C99 | Semantic subset | `test_cwiz.cpp` | UTF-8 bytes; `normalizeText` maps to F4 |

## C23

| Feature | Status | Tests | Notes |
|---|---|---|---|
| `nullptr`, `nullptr_t` | Full | `test_conformance.cpp` | null pointer constant |
| `true`, `false`, `bool` keywords | Full | `test_cwiz.cpp` | already keywords/types |
| `constexpr` objects | Semantic subset | `test_conformance.cpp` | compile-time constants, file/local |
| `typeof`, `typeof_unqual` | Semantic subset | `test_conformance.cpp` | `typeof(type)`; expression form rejected with a hint |
| `auto` type inference | Semantic subset | `test_conformance.cpp` | inferred from the initializer |
| `static_assert` with one argument | Full | `test_conformance.cpp` | |
| `[[nodiscard]]`, `[[maybe_unused]]`, `[[deprecated]]`, `[[fallthrough]]`, `[[noreturn]]` | Syntax-only | `test_conformance.cpp` | parsed and ignored |
| `#elifdef`, `#elifndef` | Full | `test_conformance.cpp` | |
| `#embed` | Full | `test_conformance.cpp` | expands a binary file to a byte list |
| digit separators (`1'000'000`) | Full | `test_conformance.cpp` | |
| binary literals (`0b1010`) | Full | `test_conformance.cpp` | |
| `u8` character literals, `char8_t` | Semantic subset | `test_conformance.cpp` | bytes; no Unicode execution encoding |
| `_BitInt(N)` | Semantic subset | `test_conformance.cpp` | N <= 16; N > 16 is a target limitation |
| `__has_include`, `__has_c_attribute` | Full | `test_conformance.cpp` | |
| `unreachable()` | Semantic subset | `test_conformance.cpp` | prelude function parks the CPU |
| `memccpy` | Full | `test_conformance.cpp` | prelude |
| `strdup`, `strndup` | Semantic subset | `test_conformance.cpp` | fixed 512-byte arena, never freed |

## Types and numbers

| Feature | Status | Tests | Notes |
|---|---|---|---|
| 8-bit `char` storage | Semantic subset | `test_cwiz.cpp` | loads zero-extend; no signed char |
| 16-bit `int` | Full | `test_cwiz.cpp` | two's complement |
| 32-bit integer types | N/A (target) | experiment (`sizeof(long) == 2`) | nX-U16 ALU/registers are 16-bit; a 32-bit software layer is not implemented |
| floating point | N/A (target) | experiment (`lex.float`) | no FPU, no soft-float runtime; literals rejected with a targeted error |
| signed/unsigned conversions and promotions | Semantic subset | `test_cwiz.cpp` | all arithmetic is 16-bit two's complement |
| integer constant typing | Semantic subset | `test_cwiz.cpp` | suffix/width words ignored |
| struct layout/alignment | Semantic subset | `test_cwiz.cpp` | natural 2-byte alignment, packed members |
| unions | Full (layout) | `test_cwiz.cpp` | members share offset 0 |
| multi-dimensional arrays | Full | `test_cwiz.cpp` | row decay supported |
| string escapes `\n \t \xNN \uXXXX \UXXXXXXXX` | Semantic subset | `test_cwiz.cpp` | stored as UTF-8; F4 mapping at runtime |

## Freestanding headers

| Header | Status | Tests | Notes |
|---|---|---|---|
| `<stdint.h>` | Full (8/16) | `test_conformance.cpp` | 32-bit types intentionally absent |
| `<stdbool.h>` | Full | `test_conformance.cpp` | `bool` is a keyword |
| `<stddef.h>` | Semantic subset | `test_conformance.cpp` | `size_t`, `NULL`; no `offsetof` yet |
| `<limits.h>` | Full | `test_conformance.cpp` | 16-bit limits |
| `<stdarg.h>` | Semantic subset | `test_conformance.cpp` | `va_list`, `va_start`, `va_arg`, `va_end`, `va_copy`; int-sized arguments |
| `<string.h>` | Semantic subset | `test_conformance.cpp` | memcpy/memset/memcmp/memccpy/strlen/strcmp/strcpy/strcat/strchr |
| `<stdalign.h>` | Full | `test_conformance.cpp` | |
| `<stdnoreturn.h>` | Full | `test_conformance.cpp` | |
| `<stdckdint.h>` | Semantic subset | `test_conformance.cpp` | `ckd_add`/`ckd_sub`; no `ckd_mul` |

## Preprocessor

| Feature | Status | Tests | Notes |
|---|---|---|---|
| object/function-like macros | Full | `test_cpre.cpp` | |
| stringizing `#`, token pasting `##` | Full | `test_conformance.cpp` | |
| `__VA_ARGS__`, `__VA_OPT__` | Full | `test_conformance.cpp` | |
| `#if` integer expressions, `defined` | Full | `test_cpre.cpp` | |
| `#include`, `#pragma once` | Full | `test_cpre.cpp` | quotes only |
| `#elifdef`, `#elifndef` | Full | `test_conformance.cpp` | |
| `#line` | Full | `test_conformance.cpp` | remaps diagnostics |
| `#embed` | Full | `test_conformance.cpp` | |
| `__has_include`, `__has_c_attribute` | Full | `test_conformance.cpp` | |

## Experiments and target limitations

Recorded experiments live in `tests/conformance/` and are run by
`wiztests`.  The current experiments prove:

* floating-point literals are rejected (`lex.float`);
* `sizeof(long)` is 2 (no 32-bit integer model);
* the emitter has no FP or 32-bit ALU opcodes (`__mul16`, `__div16`,
  `ERn` pairs are 16-bit).

## Corpus pass rate

`firmware-re/fx580/cwiz/{auto,named}` (decoder-generated CWiz, canonical
directive/type grammar): **3195/3195 modules compiled, 0 failures, 45.35 s**
on 2026-11-03 via `build/corpus_check --named --out temp/corpus`.  This is a
regression gate for parser/codegen work; corpus files are read-only.
