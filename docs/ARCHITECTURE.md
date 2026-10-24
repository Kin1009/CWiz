# Wiz toolchain architecture

How a `.wz` or `.cwiz` file becomes a running program: the compiler pipeline, the generated code's
calling convention, the hand-written runtime, the assembler, the simulator, and how to extend each
part.

Read [GUIDE.md](GUIDE.md) for the two languages and [API.md](API.md) for the surface API.

---

## Pipeline

The toolchain is one static library (`wizcore`) plus four thin drivers. Two independent front ends
feed a single shared back end:

```
 prog.wz                                    prog.cwiz
   │  wiz::select_sections()                  │  wiz::select_sections()      pick the chosen machine
   ▼                                           ▼
 wiz::compile_to_asm()                       wiz::compile_cwiz()
 (compiler/src/wiz.cpp)                      (compiler/src/cwiz.cpp)
   │                                           │
   └──────────────────┬────────────────────────┘
                      ▼  assembly text + variable map
   wiz::build_source() / wiz::build_cwiz()   (compiler/src/build.cpp)
      │  make_boot(target)     prepend the boot/startup template
      │  + your code
      │  + runtime_for(target) append the filtered runtime
      ▼
   wiz::Assembler::assemble()                two-pass assemble -> {address: byte}
      │
      ▼  to_bin()
   prog.bin  ──►  wiz::U8 (simulator)  or  CasioEmuMsvc  or  a firmware patch
```

`build_source` / `build_cwiz` return a `BuildResult { rom, full_asm, variables, syms }`; the tests
use `variables` (name → RAM address) and `syms` (label → address) to inspect behaviour.

Targets are described once in `compiler/src/targets.cpp`; the user-facing names are `580` and
`880` (internal names `cw` and `cwii`, plus `cwapp`). See [API.md](API.md#targets).

---

## The Wiz front end (`compiler/src/wiz.cpp`)

### 1. Lexing and parsing

A hand-written lexer produces INDENT/DEDENT/NEWLINE tokens Python-style (spaces only; tabs are
rejected). The recursive-descent parser builds a tuple-based AST with these statement forms:

```
def   name params body defaults        rom/rom8  name values
if    arms else                        assign / aassign  (scalar / indexed)
while cond body                        for  var start stop step body
return expr                            break continue pass global
```

Constant folding happens during parsing (`fold()`), replicating the runtime's exact 16-bit
semantics, so `const` expressions and literal arithmetic are evaluated at compile time. `for`-loop
steps and `rom`/`rom8`/array entries must be constants.

### 2. Desugaring

- `print(...)` is rewritten into a chain of `text`/`num` calls (`rewrite_print`).
- The **standard library** (`PRELUDE`) is appended on demand: the compiler walks the program's
  referenced names and pulls in only the library snippets needed, transitively, skipping anything
  the user redefined. `@ROWS@`/`@LAST@` in snippets are substituted with the target's height, and
  `_rpeek` becomes `peek` on targets whose ROM tables live in segment 0.

### 3. Analysis

- Top-level assignments define the set of **globals**.
- Arrays/`rom` tables must be top-level; they are declared first and get RAM/ROM.
- A **call graph** is built. Calls to unknown functions are errors. The transitive closure
  `reach[n]` = everything function `n` can reach is computed to detect recursion cycles.

### 4. Code generation

Every expression is compiled into `ER0`; binary operators evaluate the left operand into `ER0`
and the right into `ER2` (`operands()`), then combine. Multiplication, division, shifts and modulo
call runtime routines. Comparisons and `and`/`or`/`not` are turned into branches that materialise
`0`/`1`.

Emitted program skeleton:

```asm
__wiz_entry:
    BL __rt_init          ; runtime state (draw mode, key_hit bitmap)
    BL __init             ; top-level (global) code
    BL f_main             ; only if a main() exists
__wiz_halt:
    B __wiz_halt          ; park forever

__init:
    PUSH LR
    <array initialisation>
    <top-level statements>
    MOV ER0, #0
    POP PC

f_<name>:
    PUSH LR
    <body>
    MOV ER0, #0
f_<name>__ret:
    POP PC
```

Then `emit_data()` writes `rom` tables (`DW`), `rom8` tables and string literals (`DB`), and pads
to an even boundary.

### Calling convention and variables

- **Variables are statically allocated** from `var_base` upward in 2-byte cells. A global is stored
  under its own name; a local under `function.name`; a `for`-loop's saved stop value under
  `function.__endN`.
- Calling a user function pushes the arguments, pops them into the callee's fixed parameter slots,
  and does `BL f_name`. The result is in `ER0`.
- Calling a **built-in** evaluates the arguments and leaves them in `ER0`, `ER2`, `ER4`, … then
  `BL __name` (with `pixel` defaulting `c` to 1 in `ER4`).
- `rom` tables get the label `__d_<name>`; string literals are interned as `__s0`, `__s1`, ….

### Recursion

Because local slots are static, a function that can reach itself would overwrite its own locals on
re-entry. The compiler detects this and, around the recursive call, emits save/restore markers:

```asm
;;SAVE <fn>
BL f_<name>
;;REST <fn>
```

`expand_frames()` replaces each marker with `PUSH ER2` / `POP ER2` of every local of `<fn>`. It also
emits a build **note** estimating stack bytes per level for each recursive function. Tail calls are
not optimised; deep recursion consumes the ~2 KB stack.

### Registers

`ER0` is the expression accumulator; `ER2` the second operand; `ER4`/`ER6` scratch and the first
built-in arguments. The runtime may clobber `ER2`–`ER14`. The compiler does not attempt register
allocation beyond this scheme — clarity over speed.

---

## The CWiz front end (`compiler/src/cwiz.cpp`)

CWiz is a **complete, independent compiler**: its own lexer, parser, semantic analyser and code
generator. It shares no code with the Wiz front end above the assembly level — only the target
table, the runtime, the boot templates and the assembler.

### Syntax

CWiz is a C subset: `{}` blocks, `;` statements, `//` comments, and the types `int`, `char` and
`void` (all 16-bit at run time — `char` is a spelling, not a distinct width). It has
`if`/`else`, `while`, `do … while`, C-style `for (init; cond; step)`, `return`, `break`,
`continue`, the usual arithmetic / bitwise / comparison / logical operators, the ternary `?:`,
`++`/`--` and compound assignment. Functions support **default arguments**:

```c
int scale(int x, int k = 2) { return x * k; }
```

Top-level declarations: globals (`int score = 0;`), RAM arrays (`int values[N];`), `const`
(`const int N = 32;`), and ROM tables:

```c
rom  int squares[11] = {0, 1, 4, 9, 16, 25, 36, 49, 64, 81, 100};
rom8 char title[20] = "CWiz arrays";
rom8 char sprite[16] = "\x3C\x3C\x66\x66...";
```

There are **no pointers and no structs**, and only one index level (`a[i]`, never `a[i][j]`).

### Differences from Wiz

The feature set matches Wiz, but a few names must differ because C reserves them:

- the glyph helper is **`glyph(x, y, c)`**, not `char` (which is a type/keyword);
- `text`/`num`/`sprite` still take a ROM address in an `int`;
- `print(x, y, …)` is a compiler special form (as in Wiz), not a function;
- `_ROWS` and every `K_*` key constant of the target are injected as predefined constants, so
  `print(0, 0, "h = ", _ROWS)` and `key_hit(K_OK)` work on both machines' sections;
- `hline`/`vline` are half-open like Wiz (`hline(x0, x1, y)` fills `x0..x1-1`).

The prelude (the CWiz standard library) is emitted as source and parsed at start-up; the same
on-demand selection applies — only the routines a program references are kept, and a user
definition of the same name wins. The 6×8 font is injected as a generated `rom8 _font[760]` table.

CWiz's default target is `880`.

---

## The runtime (`compiler/runtime.asm`)

Hand-written assembly, assembled together with your program. Registers are the ones the compiler
uses; a leaf routine returns with `RT`, the few that call others use `PUSH LR … POP PC`.

| Routine | Purpose |
|---------|---------|
| `__mul16` | `ER0 × ER2` → `ER0` (low 16 bits), shift-and-add |
| `__div16` | signed division: quotient in `ER0`, remainder in `ER2` (signs handled separately) |
| `__shl16` / `__sra16` | `ER0 << R2` / arithmetic `ER0 >> R2` |
| `__pixel` | plot/clear one pixel in the shadow buffer (SB2) |
| `__clear` | zero the shadow buffer (SB2) |
| `__render` | copy SB2 to the LCD; on `cwii` it refreshes both bitplanes |
| `__renderStatus` | copy only the 23-byte status-icon range from SB2 to the LCD plane(s) |
| `__buffered` | compatibility no-op |
| `__flip` | branch to `__render` |
| `__rt_init` | zero the `key_hit` bitmap |
| `__key` | scan the key matrix → index or `-1` |
| `__key_down` / `__key_hit` | held / edge-detect for one key |
| `__delay` | busy loop |
| `__peek` / `__rpeek` / `__poke` | byte load (segment 0 / table segment) / store |
| `__hspan` | fast horizontal span (used by `hline`/`fillrect`) |

Build-time filtering: `runtime_for()` drops lines tagged `;@1p` (1-plane) or `;@2p` (2-plane)
depending on the target, drops `;@seg` lines when the target's `rom_seg` is 0, and adjusts the
`;@off` VRAM row offset for `580`. It emits `_ROWS EQU <rows>` and `_SB2 EQU <address>` first;
`_SB2` is `0xDDD3` for `cw`/`cwapp` and `0xCA54` for `cwii`.

### Shadow buffer and render

All drawing primitives (`__pixel`, `__hspan`, `__clear` and everything built on them) write only
to SB2, a plain RAM region. `render()` (`__render`) is the sole flush: it copies `0x800` bytes of
SB2 to `0xF800`. On `cwii` the copy loops `R12 = 0, 4`, selecting plane 0 then plane 1 through
`0F037H`; on `cw` the loop runs once and never touches `0F037H`. Because the shadow is
monochrome, both bitplanes receive the same bits, so a set pixel is fully black.

---

## The assembler (`compiler/src/asm.cpp`)

A compact **two-pass** assembler: pass one defines labels with best-effort expression values, pass
two resolves them and checks short-branch ranges (`±127` words). It supports labels, `EQU`, `ORG`,
`DW`/`DB`, the full instruction set of the shared nX-U8/100 & nX-U16/100 encoding, and the
`JMP`/`Jcc` pseudo-ops. Operation groups are matched by the top nibble of the word and the
register/operand shapes (see [API.md](API.md#the-assembler)).

Both cores share one encoder (the OKI nX-U8/100 manual cross-checked against the LAPIS nX-U16/100
manual), which is why `resolve_core()` only changes the label, the branch-cycle model and which
errors are reported: both produce **byte-identical** ROMs.

---

## The simulator (`compiler/src/sim.cpp`)

Decodes raw machine code with its **own** decoder (it shares no tables with the assembler), so an
assembler or compiler mistake shows up as a simulator fault instead of being masked. It models:

- segment 0 memory plus segment-addressed ROM (needed for `cwapp`);
- the LCD as two bitplane bytearrays selected through `0F037H`, with the `580` row offset;
- the key matrix (a list of `(KO, KI)` masks read back through `0F040H`);
- `PUSH LR`/`POP PC` as a 4-byte frame (large model).

`run()` executes until a `stop` address, a halt (`B` to itself), a fault, or the step budget, and
returns a human-readable reason. `ascii()` renders the LCD. See
[API.md](API.md#the-simulator).

---

## Targets and boot templates

`compiler/src/targets.cpp` describes each machine (memory ranges, planes, core, row offset).
`compiler/src/build.cpp`:

- `make_boot(target)` reads the target's `template_*.asm`, strips Lexide-only directives
  (`type(`, `model`, `romwindow`, `extrn`, `public`), and rewrites `b _main` to `B __wiz_entry`.
  The template sets the SFRs the LCD and keyboard need and then jumps into the program.
- `template_cwapp.asm` instead begins with `ORG 30E40h` and a trampoline, and defines `__exit`
  (reset the firmware) plus `_APP`/`_TRAMP` symbols used by `patch_menu`.

`for <target>` sections are handled by `targets.select_sections()`, which blanks out foreign lines
**without changing line numbers**, so compile errors keep pointing at the original line.

---

## Extending the toolchain

**Add a standard-library function** — add a snippet to `PRELUDE` in `wiz.cpp` (Wiz) and/or to
`PRELUDE_SRC` in `cwiz.cpp` (CWiz); it is pulled in only when referenced, and a user definition
overrides it. It must be pure source in the language concerned.

**Add a built-in** — add its name and arity to `BUILTINS` in `wiz.cpp` and `CW_BUILTINS` in
`cwiz.cpp`, implement `__name` in `compiler/runtime.asm`, and (if it needs frame-like stack use)
follow the `PUSH LR … POP PC` convention.

**Add a target** — add a `Target` to `compiler/src/targets.cpp` with a boot template, memory map,
plane count, core and row offset; then `for <name>` sections and `--target <name>` work
automatically (extend `canonical()` with the new spelling).

**Teach the assembler an instruction** — extend `Assembler::encode()` in `compiler/src/asm.cpp` and
add a test (the project cross-checks encodings against the manuals).

**Test a change** — build and run `wiztests` (`./build.sh` then `build/wiztests`, or `ctest` from
`build/`). Because the simulator has an independent decoder, a green suite exercises the compiler,
assembler, runtime and simulator together.
