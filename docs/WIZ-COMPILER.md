# Wiz / CWiz compiler internals

This document describes how the C++ compiler is put together, where every
diagnostic comes from, how suggestions are computed, which test covers what,
and what "full C support" would still require.  See
[ARCHITECTURE.md](ARCHITECTURE.md) for the language tour and
[WIZ-UPSTREAM.md](WIZ-UPSTREAM.md) for the comparison with the Python
reference compiler.

## 1. Pipeline

```
                    .wz                                  .cwiz
                     │                                     │
             wizimport (upstream only)              preprocess_cwiz_info()
                     │                          #define/#include/#if/#for/
                     ▼                          #cwapp/#slot/#org/#binary
             select_sections()  ◄──── for <target> sections ────┘
                     │                                     │
                     ▼                                     ▼
             compile_to_asm()                        compile_cwiz()
              (wiz.cpp)                              (cwiz.cpp)
              lex → parse → desugar                 lex → parse → sema
              → call graph → codegen                → allocate → codegen
                     │                                     │
                     └──────────────┬──────────────────────┘
                                    ▼   assembly text + variable map
                          build_source() / build_cwiz()
                             make_boot(target)
                           + program + runtime_for(target)
                                    ▼
                          Assembler::assemble()  (two passes)
                                    ▼
                             to_bin()  →  .bin / .asm
                                    │
                 ┌──────────────────┼──────────────────┐
                 ▼                  ▼                  ▼
              U8 simulator      CasioEmuMsvc      firmware patch
                                              (#cwapp → built-in ROM)
```

`#rop` sources bypass this and go to `build_rop()` (`rop.cpp`), which parses
the same C-like syntax into a gadget chain and emits a payload plus launcher.

Both front ends share only the target table, runtime, boot template and
assembler.  Wiz and CWiz have independent lexers, parsers and code
generators.

## 2. Diagnostics architecture

### Where errors are produced

| Stage | File | Error type | Examples |
|---|---|---|---|
| CWiz preprocessor | `cpre.cpp` | `CwizError` | `#include` not found, bad `#if`, unknown directive |
| Wiz lexer/parser | `wiz.cpp` | `WizError` | tabs for indentation, bad `range()`, `return` outside `def` |
| CWiz lexer/parser | `cwiz.cpp` | `CwizError` | unterminated string/comment, missing `;`, unmatched `}` |
| CWiz semantic analysis | `cwiz.cpp` (`Analyzer`) | `CwizError` | undefined identifier, void misuse, wrong arity, duplicate case |
| CWiz code generation | `cwiz.cpp` (`Codegen`) | `CwizError` | out of RAM, internal invariant failures |
| Wiz code generation | `wiz.cpp` | `WizError` | unknown function, out of RAM, break outside loop |
| ROP parser/emitter | `rop.cpp` | `std::runtime_error` / `RopResult::error` | unsupported gadget, dynamic condition, duplicate/undefined label |
| Assembler | `asm.cpp` | `AsmError` | bad expression, branch out of range |
| Driver | `main_wizc.cpp` | `Diag` | cannot read/write file, patch failures |

Every `CompileError` carries:

```cpp
int line, col, end_col;
std::string code;   // stable machine-readable category, e.g. sema.duplicate-global
std::string hint;   // actionable "help:" line
```

`CwizError` and `WizError` are thin subclasses that pass these fields through.
The driver catches `CompileError` in `compile_one()`/`print_preprocessed()`,
re-reads the original file, and prints:

```
file.cwiz:12:5: error: pixel() takes 2-3 arguments, got 1 (expected pixel(x, y, c = 1)) [sema.argument-count]
  12     pixel(1);
         |     ^
     help: pass 2-3 argument(s): pixel(x, y, c = 1)
```

CWiz line numbers survive preprocessing: `preprocess_cwiz` replaces each
directive line with an empty line and records `#include` origins in
`cwiz_line_origin()`.  `cwiz_error_pos()` maps a preprocessed line back to the
real file and line before the driver reads the source snippet.

### How the linter re-runs

`lint_source()` (`lint.cpp`) does two things:

1. It calls the real compiler (`build_cwiz`, `build_source`, or `build_rop`).
   A `CompileError`/`RopResult` becomes one `LintDiagnostic` with
   `rule = "compile"`, copying `line`, `col`, `end_col`, `message`, `hint` and
   `code`.  On success the compiler's diagnostics are absent.
2. It scans the raw text for style/robustness rules (unused variables,
   shadowing, unreachable code, out-of-bounds constant indexes, assignment in
   condition, long lines, tabs, TODO markers, duplicate `#for`, unknown
   directives, ...).  These are warnings, never errors, so lint cannot reject
   a program the compiler accepts.

`LintDiagnostic` is the common shape (`rule`, `severity`, `line`, `col`,
`end_col`, `message`, `hint`, `code`).  The CLI (`main_wizlint.cpp`) renders it
gcc-style.  The linter's own directive suggestion list is broader than the
preprocessor's because the linter also accepts ROP directives
(`#rop`, `#launcher`, `#pad`, `#nopad`, `__asm__`, `__hex__`, `line`).

### How suggestions are computed

`common.hpp` implements Levenshtein edit distance and `closest_match()`:

* candidates must be at least 2 characters;
* the distance limit is 1 for words of length ≤ 3, 2 for ≤ 6, and 3 above;
* the closest candidate wins; an exact match never suggests itself.

`did_you_mean()` turns that into `did you mean 'x'?`.  Directives use a
prefixed variant (`did you mean '#launcher'?`).  Candidate sets:

| Diagnostic | Candidates |
|---|---|
| unknown CWiz identifier/function | locals, globals, arrays, tables, user consts, functions, `CW_BUILTINS`, `print`/`print_cw` |
| unknown Wiz identifier/function | all variables, functions, `BUILTINS`, prelude names |
| unknown directive (`cpre.cpp`, `lint.cpp`) | every supported directive |
| expected a type | `int`, `char`, `void`, `bool` |
| ROP undefined label | resolved against the emitter's label table |

Suggestions are hints, not extra diagnostics: they are printed on the
`help:` line and do not change the error count.

## 3. New diagnostics added in this pass

| Code | Message shape | Test |
|---|---|---|
| `sema.duplicate-global` | `duplicate global 'x' (first declared at line N)` | `test_cwiz.cpp`, `test_lint.cpp` |
| `sema.duplicate-case` | `duplicate case value N in switch` | `test_cwiz.cpp` |
| `sema.duplicate-default` | `duplicate 'default' in switch` | `test_cwiz.cpp` |
| `sema.continue-outside-loop` | `'continue' outside a loop` (separate from `break`) | `test_cwiz.cpp` |
| `sema.case-outside-switch` | `case/default outside a switch` | `test_cwiz.cpp` |
| `sema.argument-type` | `pixel() argument 1 expects int, got a string` | `test_cwiz.cpp` |
| `sema.sizeof-void` | `sizeof(void) has no meaning` | `test_cwiz.cpp` |
| `syntax.missing-semicolon` | `expected ';' before '}'` | `test_cwiz.cpp` |
| `syntax.unbalanced-paren` | `expected ')' before ';'` | `test_cwiz.cpp` |
| `syntax.unbalanced-bracket` | `expected ']' before ';'` | `test_cwiz.cpp` |
| `syntax.unmatched-brace` | `unmatched '}'` | `test_cwiz.cpp` |
| `syntax.case-range` | `case ranges are not supported` | `test_cwiz.cpp` |
| `syntax.unterminated-switch` | `unterminated switch` | parser path |
| `syntax.switch-statement` | statement before the first `case` | parser path |
| ROP `E107` | `duplicate label 'up'` | `test_rop.cpp` |
| ROP `E108` | `undefined label 'nowhere'` (was a log warning) | `test_rop.cpp` |
| ROP `E109` | `duplicate global 'a'` | `test_rop.cpp` |
| ROP `E110` | `duplicate function 'f'` | `test_rop.cpp` |

Existing diagnostics were also improved:

* builtin arity errors now include the expected signature, e.g.
  `pixel() takes 2-3 arguments, got 1 (expected pixel(x, y, c = 1))`;
* `break` and `continue` now have distinct messages/codes and a switch-aware
  rule (`break` is legal in a switch, `continue` is not);
* directive hints now include the leading `#`;
* `expected '<op>' before '<token>'` no longer doubles the token quotes.

## 4. Error-code reference

Codes are stable even when messages change.  Wiz still has a number of legacy
errors without a code (they show no `[code]` suffix); new Wiz work should add
codes following this scheme.

### Lexer (`lex.*`)

| Code | Meaning / example |
|---|---|
| `lex.bad-char` | character literal with more than one character |
| `lex.bad-escape` | `\q`, `\x` with no digits |
| `lex.bad-number` | `0x` with no digits |
| `lex.float` | `1.5` or `.5` (only 16-bit integers exist) |
| `lex.non-ascii` | non-ASCII source character in a Wiz string/identifier |
| `lex.unexpected-character` | `` ` `` or a stray symbol |
| `lex.unterminated-char` | missing closing `'` |
| `lex.unterminated-comment` | missing `*/` |
| `lex.unterminated-escape` | trailing `\` in a literal |
| `lex.unterminated-string` | newline/EOF before the closing `"` |

### Syntax (`syntax.*`)

| Code | Meaning / example |
|---|---|
| `syntax.call-target` | `(x + y)(1)` — only a named function can be called |
| `syntax.case-outside-switch` | `case`/`default` at statement level |
| `syntax.case-range` | `case 1 ... 3:` |
| `syntax.expected-declaration` | top-level token is not a type/qualifier |
| `syntax.expected-identifier` | `int 3;`, a keyword used as a name |
| `syntax.expected-token` | generic `expected 'x' before y` |
| `syntax.expected-type` | `foo x;` where `foo` is not a type |
| `syntax.expected-while` | `do { }` without `while` |
| `syntax.function-in-declaration` | `int a, f() {}` |
| `syntax.missing-semicolon` | `int x = 1 }` |
| `syntax.not-assignable` | `(a + b) = 1` |
| `syntax.switch-statement` | statement before the first case label |
| `syntax.unbalanced-bracket` | EOF/`;`/`}` while looking for `]` |
| `syntax.unbalanced-paren` | `;`/`}`/EOF while looking for `)` |
| `syntax.unexpected-token` | missing expression, `case` used in an expression |
| `syntax.unmatched-brace` | extra `}` at file scope |
| `syntax.unterminated-block` | block opened at line N never closed |
| `syntax.unterminated-switch` | switch body never closed |

`goto` is implemented (function-scoped labels, forward and backward jumps;
`compiler/src/cwiz.cpp:1418`, covered by `tests/test_cwiz.cpp`). The old
`syntax.unsupported-goto` code no longer exists.

### Semantics (`sema.*`)

| Code | Meaning / example |
|---|---|
| `sema.argument-count` | `pixel(1)`, `f(1)` for a 2-parameter function |
| `sema.argument-type` | string passed to a numeric builtin argument |
| `sema.array-assign` | `a = b` where `a` is an array |
| `sema.array-size` | `int a[0];` |
| `sema.break-outside-loop` | `break` not inside a loop/switch |
| `sema.builtin-target` | `exit()` outside `#cwapp` |
| `sema.case-outside-switch` | `case` outside a switch (analyzer path) |
| `sema.const-address` | `&C` for a constant |
| `sema.const-assign` | `C = 2` for a constant |
| `sema.constant` | non-constant ROM/array initializer |
| `sema.continue-outside-loop` | `continue` not inside a loop |
| `sema.default-order` | parameter without a default follows one with a default |
| `sema.duplicate-case` | two `case 1:` labels |
| `sema.duplicate-default` | two `default:` labels |
| `sema.duplicate-global` | two globals with the same name |
| `sema.duplicate-parameter` | `f(int a, int a)` |
| `sema.function-as-value` | using a function name without `()` |
| `sema.function-scope` | function defined inside a block |
| `sema.local-const` | `const` inside a function (declare it at file scope) |
| `sema.main-parameters` | `main` with parameters |
| `sema.missing-return-value` | `int f() { return; }` |
| `sema.not-assignable` | left side is not a variable/element/`*p` |
| `sema.not-indexable` | `x[0]` where `x` is a scalar |
| `sema.not-pointer` | `*x` where `x` is not a pointer |
| `sema.redefinition` | function/constant/local defined twice |
| `sema.rom-address` | `&table[0]` for a ROM table |
| `sema.rom-write` | `table[0] = 1` for a ROM table |
| `sema.sizeof-void` | `sizeof(void)` |
| `sema.undefined-identifier` | `x = 1` with no `x` |
| `sema.unknown-function` | `pint(...)` |
| `sema.unknown-key` | `K_*` name not in the target's key table |
| `sema.void-expression` | assigning/returning/operating on a void call |
| `sema.void-return` | `void f() { return 1; }` |
| `sema.void-variable` | `void x;` |

### Preprocessor (`preproc.*`)

| Code | Meaning / example |
|---|---|
| `preproc.binary` | malformed `#binary` |
| `preproc.cwapp-order` | `#cwapp` without `#for 580` first |
| `preproc.define` | malformed `#define` |
| `preproc.include` | malformed `#include` |
| `preproc.include-cycle` | A includes B includes A |
| `preproc.include-not-found` | quoted include does not exist |
| `preproc.slot-duplicate` | second `#slot` |
| `preproc.slot-order` | `#slot` before `#cwapp` |
| `preproc.slot-range` | `#slot 0` or `#slot 13` |
| `preproc.unbalanced-conditional` | `#endif`/`#else` without `#if`, missing `#endif` |
| `preproc.unknown-directive` | `#wibble` (with a suggestion) |

### Memory (`memory.*`)

| Code | Meaning / example |
|---|---|
| `memory.out-of-ram` | allocator crossed `var_limit` |

### Wiz (`wiz.*`)

| Code | Meaning / example |
|---|---|
| `wiz.break-outside-loop` | `break`/`continue` outside a loop |
| `wiz.chained-comparison` | `a < b < c` |
| `wiz.constant-parameter` | a constant used as a parameter name |
| `wiz.default-order` | non-default parameter after a default |
| `wiz.default-value` | non-constant default value |
| `wiz.function-scope` | `def` inside a block |
| `wiz.global-never-assigned` | `global x` but top level never assigns `x` |
| `wiz.indent` | inconsistent indentation |
| `wiz.no-effect` | expression statement with no effect |
| `wiz.range` | malformed `range()` / step 0 / step not constant |
| `wiz.return-outside-function` | `return` at top level |
| `wiz.rom8-range` | rom8 value outside 0..255 |
| `wiz.tab-indent` | tab used for indentation |
| `wiz.table-constant` | table entry is not constant |

### Assembler / ROP

| Code | Meaning / example |
|---|---|
| `asm.error` | bad expression, branch out of range |
| ROP `E100` | generic emitter error |
| ROP `E101` | wrong argument count for a ROP API (`line`, `rect`, `pixel`, ...) |
| ROP `E103` | unknown ROP value / call inside an expression |
| ROP `E104` | unsupported ROP API or gadget |
| ROP `E105` | dynamic condition in a ROP `if`/`while`/`for` |
| ROP `E106` | no function in a ROP source |
| ROP `E107` | duplicate label |
| ROP `E108` | undefined goto/keytable label |
| ROP `E109` | duplicate global |
| ROP `E110` | duplicate function |

Linter rules are a separate namespace (`readability/*`, `correctness/*`,
`style/*`, `preproc/*`); the compiler's own findings always use the rule
`compile` with the compiler code preserved in `LintDiagnostic::code`.

## 5. C support added and what remains

### `switch` / `case` / `default`

* Parser builds `NK::Switch` with a list of `NK::Case` nodes; a case may be a
  constant expression (`case C:` where `const int C = 5;`).
* Analyzer folds every case value, rejects duplicate values and duplicate
  `default`, and permits `break` while forbidding `continue` unless an
  enclosing loop exists.
* Codegen evaluates the selector once, stores it in a temporary, emits
  `CMP`/`JEQ` dispatch, then lays the case bodies out in source order.  This
  preserves C fallthrough; `break` jumps to the shared end label; `continue`
  keeps the enclosing loop's target.
* Not supported: GNU `case 1 ... 3:` ranges (explicit error).  Declarations
  directly after a case label (`case 1: int x = 1; ...`) are accepted because
  they are ordinary statements in this grammar.

### `sizeof`

* `sizeof(type)` and `sizeof expr` both work.  `sizeof` of an array name is
  the whole array in bytes (element width × element count); `sizeof` of a
  string literal includes the terminating NUL; pointers are 2 bytes and `char`
  is 1.  `sizeof(void)` is an error.
* The analyzer folds the result into a numeric literal, so the code generator
  and constant-expression evaluator need no special case.

### Still unsupported (and why)

| C feature | Status |
|---|---|
| `goto` / labels | Explicitly rejected in CWiz; ROP sources have labels and `goto` with duplicate/undefined checks |
| `struct` / `union` / `enum` / `typedef` | Not in the grammar; no aggregate layout model |
| multi-dimensional arrays (`a[i][j]`) | Only one index level |
| casts (`(int)x`, `(char*)p`) | No cast syntax |
| function pointers / varargs | Calls are direct named calls |
| designated initializers, `...` ranges | Not parsed |
| `static`/`extern`/`inline` storage semantics | Accepted as qualifiers and ignored |
| real type widths/signedness, integer promotions | Every value is 16-bit; `char` is a spelling |
| floating point | Lexer rejects literals |
| local `const` | Rejected; declare constants at file scope |
| separate compilation / linker | Whole program per file; `#include` is textual |
| `do/while`, `?:`, `++/--`, compound assignment, `break`/`continue`, `else if` | Already supported |

## 6. Test map

| Test file | Covers |
|---|---|
| `tests/test_cwiz.cpp` | CWiz expressions, control flow (`switch`/`sizeof`/`goto`), pointers, arrays, ROM tables, strings, fonts, Vietnamese text, diagnostics and suggestions |
| `tests/test_cpre.cpp` | `#define`, `#if`, `#include`, `#for`, `#cwapp`, `#slot`, line preservation |
| `tests/test_lint.cpp` | Compiler errors surfaced as `compile`, style rules, directive suggestions, switch acceptance |
| `tests/test_cw.cpp` | Wiz targets/sections, boot/runtime filtering, `getpixel`/string helpers |
| `tests/test_v02.cpp`, `test_v03.cpp`, `test_v03b.cpp` | Original Wiz language suites |
| `tests/test_cwapp.cpp` | Firmware patch path |
| `tests/test_rop.cpp` | ROP backend, launcher bytes, key tables, listings, duplicate/undefined labels |
| `tests/test_fuzz.cpp` | Parser robustness |
| `tests/harness.hpp` / `tests/main.cpp` | Shared check runner (`N checks, M failure(s)`) |

Run:

```sh
cd <repo root>          # the directory containing build.sh
cmake --build build --target wizc wiztests -- -j$(sysctl -n hw.logicalcpu)
./build/wiztests
```

## 7. Upstream gaps

See [WIZ-UPSTREAM.md](WIZ-UPSTREAM.md) for the full table.  The short version:

* ported in this pass: `strlen`, `strcmp`, `strcpy`, `atoi`, `itoa`, `getpixel`,
  `clear_rect`, `invert_rect`, `copy_rect` (prelude source in both languages);
* still missing: `ticks`/`delay_ms` (Timer0 asm), the RTC prelude, `clip`,
  interactive input, heap/file APIs, `ellipse`/`tri`/`roundrect`, `import`,
  `charz`/`textz`/`numz`/`draw`;
* the assembler has opcode parity; the C++ port adds `JMP`/`Jcc` pseudos, a
  native simulator, a linter and the CWiz front end.

## 8. Directive grammar (2026 pass)

All directive metadata lives in `compiler/src/directives.cpp` and is shared by
`cpre.cpp`, `rop.cpp` and `lint.cpp`.  See [DIRECTIVES.md](DIRECTIVES.md) for
the authoritative table, number/string grammar, ordering rules and aliases.
The linter reports compatibility aliases with
`preproc/noncanonical-directive` and non-canonical `rom`/`rom8`/
`__normalized__` placement with `preproc/noncanonical-type`.

C23 preprocessor rows now implemented: `#elifdef`, `#elifndef`, `#line`
(diagnostic remapping), `#embed`, `__has_include`, `__has_c_attribute`,
stringizing, token pasting and `__VA_OPT__`.  Freestanding angle includes
resolve to built-in headers in `cpre.cpp` (`<stdint.h>`, `<stdbool.h>`,
`<stddef.h>`, `<limits.h>`, `<string.h>`, `<stdalign.h>`, `<stdnoreturn.h>`);
there is no host include path.

## 9. C conformance status

[docs/C-CONFORMANCE.md](C-CONFORMANCE.md) is the definition of done.  This pass
added `switch`, `sizeof`, `goto`/labels, structs/unions/enums/typedefs, casts,
local `const`, multidimensional arrays, `_Static_assert`, `_Generic`,
`_Alignas`/`_Alignof`, `constexpr`, `typeof(type)`, `auto`, `nullptr`, `char8_t`,
`u8` literals, binary literals, digit separators, `_BitInt(<=16)`, fixed-enum
syntax, `[[attribute]]`, designated initializers, compound literals,
flexible array members (syntax-only), bitfields (syntax-only) and C23
preprocessor features.  Remaining Missing rows are listed with experiments in
the matrix (anonymous members, function pointers, variadic functions,
`strdup`/`strndup`, `<stdarg.h>`, `<stdckdint.h>`).

New diagnostics in this pass include `sema.type-placement` (rom/rom8),
`preproc.attribute-placement` (__normalized__), `sema.static-assert`,
`sema.generic-no-match`, `sema.alignment`, `sema.bitint-width`,
`sema.bitfield-access`, `sema.flexible-access`, `sema.auto-no-init`,
`sema.typeof-expression`, `preproc.directive-argument`,
`preproc.directive-order`, `preproc.directive-context`,
`preproc.directive-duplicate` and `preproc.error`.

Test map additions: `tests/test_conformance.cpp` (C11/C17/C23 rows, directive
table, example migration + compile sweep), `tests/test_cpre.cpp` (directive
arguments), `tests/test_lint.cpp` (canonical-spelling warnings).

## 10. Firmware corpus regression

`build/corpus_check --named --out temp/corpus` compiles the ROM agent's
`firmware-re/fx580/cwiz/{auto,named}` corpus in-process (no /tmp).  The
2026-11-03 run compiled **3195/3195 modules with 0 failures in 45.35 s**
(auto + named; 1604 auto modules).  Report: `temp/corpus/report.txt`.
No corpus module was edited.  Treat a new failure here as a compiler bug.

## 11. C23 conformance completion

The conformance matrix is terminal: every row is Full, Semantic subset,
Syntax-only or a proven target limitation.  Highlights added after §9:

* function pointers (variable/typedef declarators, assignment, indirect
  `BL ERn` calls) and the `B ERn`/`BL ERn` encoder fix it exposed;
* variadic functions: caller pushes a hidden argument count, the callee
  prologue fills a 16-word argument array, `<stdarg.h>` maps `va_start`/
  `va_arg`/`va_end`/`va_copy` to compiler/runtime helpers;
* anonymous struct/union members flatten into the parent layout;
* `strdup`/`strndup` (fixed 512-byte arena) and C23 `ckd_add`/`ckd_sub`;
* freestanding `<stdarg.h>`, `<stdckdint.h>` and the rest of the header set;
* designated initializers and compound literals (scalar + struct).

The call ABI is now **callee-pops-parameters** for all direct calls (and
indirect function-pointer calls).  This is what lets a function pointer call
reach a normal compiled function; tests cover recursion, defaults and the
corpus.

Remaining target limitations (not Missing): 32-bit integers and floating
point (16-bit ALU/registers, no FPU/soft-float runtime), and `_Atomic`/
`_Thread_local` semantics (no OS, threads or interrupts in the language model).
