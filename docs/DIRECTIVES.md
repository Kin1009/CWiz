# Canonical directive and custom-type grammar

This is the authoritative table consumed by `wizc`, `wizlint`, the ROP
backend and the firmware decompiler.  The implementation lives in
`compiler/src/directives.cpp`; the preprocessor (`cpre.cpp`) and the ROP
pre-pass (`rop.cpp`) share its table and argument parsers.

## Number grammar

One grammar everywhere:

```
number  := "0x" hexdigit+ | digit+
```

* Decimal and `0x`-prefixed hexadecimal are accepted.
* Trailing characters are an error: `#slot 2x`, `#pad 12x`, `#org 0x` all fail
  with `preproc.directive-argument`.
* Canonical spelling: addresses are uppercase hex with `0x` (`0xE9E0`);
  sizes, counts and slots are decimal (`256`, `2`).
* Compatibility aliases (accepted; `wizlint` flags most of them as
  `preproc/noncanonical-directive`, see [Known lint gaps](#known-lint-gaps)):
  bare hexadecimal such as `#org E9E0` and `[...]`-wrapped addresses used by
  older ROP notes.  New sources must use `0xE9E0`.

## String grammar

```
string  := '"' char* '"'
```

Used by `#include` and `#binary`.  Project files use `#include "file"`.  Angle
includes are accepted only for the built-in freestanding headers
(`<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<limits.h>`, `<string.h>`,
`<stdalign.h>`, `<stdnoreturn.h>`, `<stdarg.h>`, `<stdckdint.h>`); any other
angle path is rejected with `preproc.include-not-found`.  There is no host
system include path.

## Directive table

| Directive | Argument | Context | Ordering / notes |
|---|---|---|---|
| `#if EXPR` | integer constant expression | CWiz preprocessor | must be closed by `#endif` |
| `#ifdef NAME` | identifier | CWiz preprocessor | |
| `#ifndef NAME` | identifier | CWiz preprocessor | |
| `#elif EXPR` | expression | CWiz preprocessor | inside `#if` |
| `#elifdef NAME` | identifier | CWiz preprocessor | C23; inside `#if` |
| `#elifndef NAME` | identifier | CWiz preprocessor | C23; inside `#if` |
| `#else` | none | CWiz preprocessor | once per conditional |
| `#endif` | none | CWiz preprocessor | matches `#if` |
| `#define NAME ...` | macro body | CWiz preprocessor | object/function-like; `__VA_OPT__` supported |
| `#undef NAME` | identifier | CWiz preprocessor | |
| `#pragma once` | text | CWiz preprocessor | `once` is the supported pragma |
| `#error text` | text | CWiz preprocessor | stops compilation |
| `#include "file"` / `#include <stdint.h>` | string | CWiz preprocessor | project file relative to the including file; angle form only for built-in freestanding headers |
| `#for TARGET[, TARGET]` | target list | CWiz preprocessor | canonical targets `580`, `880`; `all` in sections |
| `#cwapp` | none | CWiz preprocessor | must follow `#for 580` |
| `#slot N` | 1..12 | CWiz preprocessor | after `#cwapp`, at most once |
| `#binary "file.bin"` | quoted string | CWiz preprocessor, `fwnx` target | full firmware image |
| `#org ADDR` | address | `#cwapp` metadata, or ROP | ROP payload origin; for `#cwapp` the patch slot chooses the real address, the value is recorded in `CwizPreprocessInfo` |
| `#rop` | none | ROP | selects the ROP backend; must be the first directive |
| `#org ADDR` | address | ROP | payload execution address (default `0xE9E0` on 580, `0xE330` on 880) |
| `#pad N` | size | ROP | trailing null buffer, default 256 |
| `#nopad` | none | ROP | no trailing null buffer |
| `#launcher common [short\|medium\|long] [backup] [main]` | launcher | ROP | canonical launcher; `shared`/`chung` are compatibility aliases |
| `#launcher simple [address]` | launcher | ROP | simple stub; a bare address (`#launcher 0x8154`) is a compatibility alias |

Compatibility aliases are accepted so existing sources keep building. `wizlint`
reports most of them as `preproc/noncanonical-directive` with the canonical
spelling; the two exceptions are listed under [Known lint gaps](#known-lint-gaps).
The current aliases are:

| Alias | Canonical |
|---|---|
| `#for 580vnx`, `#for fx-580vn`, `#for cw` | `#for 580` |
| `#for 880btg`, `#for cwii` | `#for 880` |
| `#org E9E0` (bare hex), `#org [E9E0]` | `#org 0xE9E0` |
| `#launcher common shared\|chung` | `#launcher common` |
| `#launcher simple stub` | `#launcher simple` |
| `#launcher 0x8154` | `#launcher simple 0x8154` |
| `#pad 0x100` | `#pad 256` |
| `#slot 0x2` | `#slot 2` |
| `#include <unknown.h>` | rejected (no system include path) |

## Custom types: `rom`, `rom8`, `__normalized__`

`rom` and `rom8` are **storage-class qualifiers for file-scope arrays/tables**.
They are not scalar qualifiers, not local variables, and not function
qualifiers.

| Language | Canonical form | Meaning |
|---|---|---|
| CWiz | `rom int name[N] = { ... };` | N 16-bit words in ROM |
| CWiz | `rom8 char name[N] = "..." ;` | N bytes in ROM; string literals get a terminating NUL |
| Wiz | `rom name = [ ... ]` | 16-bit word table in ROM |
| Wiz | `rom8 name = "..."` | byte string/table in ROM |

Rules (one error family: `sema.type-placement` / `preproc.attribute-placement`):

* `rom` elements are 16-bit (`int`); `rom8` elements are bytes (`char`).
  `rom char`, `rom8 int`, `rom int*`, `rom8 struct S` are rejected.
* A declaration with `rom`/`rom8` must be an array with a size or
  initializer.  Scalars and function parameters are rejected.
* `rom`/`rom8` are file-scope only.  Use `peek`/`_rpeek` or a pointer for
  runtime access.
* `__normalized__` is a CWiz declaration **attribute** for a `char`/`rom8 char`
  array initialized from a string literal.  Canonical placement is first:

  ```c
  __normalized__ rom8 char title[20] = "Tiếng Việt";
  ```

  `__normalized__ char buf[N] = "..."` is also accepted (RAM buffer).  It is
  rejected on `rom int`, non-char arrays, scalars, and functions with
  `preproc.attribute-placement`.

## Known lint gaps

Verified against `compiler/src/lint.cpp` and `build/wizlint` on 2026-11-03.
These are documentation-visible because the alias table above promises lint
coverage; the compiler is unaffected and accepts every alias listed there.

| Gap | Effect |
|---|---|
| `#org`, `#pad`, `#launcher` aliases are never reported | `#org E9E0`, `#org [E9E0]`, `#pad 0x100` and `#launcher chung` produced no `preproc/noncanonical-directive` in the sources tested (a `#cwapp` source and a `#rop` payload), while `wizc` built all of them. Only `#for` aliases reach the checker. |
| `#for cw` / `#for cwii` are reported as `preproc/unknown-target` | `targets.cpp` accepts both as aliases and `wizc` compiles them, but the lint header check only knows `580`, `880`, `cwapp` and `all`, so it emits a false error instead of a canonical-spelling hint. |

Until these are fixed, treat `wizlint` as advisory for ROP directive spelling and
rely on the canonical forms in this file when writing new sources.

See [C-CONFORMANCE.md](C-CONFORMANCE.md) for the C language matrix and
[WIZ-COMPILER.md](WIZ-COMPILER.md) for the implementation map.
