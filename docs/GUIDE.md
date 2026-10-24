# The Wiz language guide

This guide teaches Wiz from the ground up. It assumes you can write a little Python; Wiz borrows
Python's indentation, `def`, `if`/`while`/`for` and expressions, then throws away everything a
calculator cannot afford (floats, objects, strings-as-values, dynamic memory).

The toolchain also ships **CWiz**, a second language with C-like syntax and the same capabilities.
[§20](#20-cwiz-in-c-syntax) is a CWiz primer; everything else here (the screen, input, drawing,
arrays, targets, recursion) applies to both languages.

For a pure reference, see [API.md](API.md). For building and running, see [RUNNING.md](RUNNING.md).

---

## 1. Getting started

You need CMake and a C++17 compiler. Build the toolchain once, then use it:

```console
$ ./build.sh                            # -> build/wizc, build/wizrun, …
...
built:
  build/wizc
  build/wizrun
  build/customromrun
  build/patch_menu
  build/wiztests

$ build/wizc examples/01_hello.wz
CC examples/01_hello.wz -> examples/01_hello.bin
    [880 ML620909 nX-U8/100] 14 variables, 2564 bytes ROM

$ build/wizrun examples/01_hello.wz --steps 1500000
                    ####   #    #
                    #      #    #
  ...
ran examples/01_hello.wz  (stopped: max steps, 1500000 steps)
```

`wizrun` executes the compiled program in the bundled simulator and draws the LCD as text
(` #` = on, blank = off). This is how you develop: iterate in the simulator, then run the `.bin`
under the emulator (see [RUNNING.md](RUNNING.md)).

---

## 2. Your first program

```python
# greet.wz
def main():
    clear()
    text(8, 6, "Hello, Wiz!")
    render()
    wait_key()
```

- `clear()` clears the off-screen shadow buffer (SB2).
- `text(x, y, "…")` draws a string with the built-in 6×8 font. `x`, `y` are pixels; `x` ranges
  0..191, `y` ranges 0..63 (`580` shows 0..62).
- `render()` copies SB2 to the LCD. Nothing appears on screen before `render()`.
- `wait_key()` comes from the standard library and blocks until a key is pressed.

Top-level statements run **before** `main()`, once, in order. Use them to set up globals:

```python
score = 0            # a global: assigned at top level
lives = 3
speed = 2

def main():
    ...
```

A program does **not** need a `main()`. If there is none, the top-level code runs and the machine
halts. If there is a `main()`, it runs after the top-level code.

---

## 3. Source files, comments, indentation

- A comment starts with `#` and runs to the end of the line. A `#` inside a string is literal.
- Indentation uses **spaces only**; tabs are a compile error. Nesting works like Python.
- A source file may be built for several machines with `for <target>` sections (see [§17](#17-one-file-several-machines)).

Blank lines and comment-only lines are ignored.

---

## 4. Values: numbers, strings, booleans

There is exactly **one numeric type: a signed 16-bit integer** (−32768..32767). Every arithmetic
result wraps modulo 2¹⁶, exactly like a real machine.

```python
a = 300            # decimal
b = 0x7FFF         # hex
c = a + 1          # 301
d = 32767 + 1      # wraps to -32768
e = True           # 1
f = False          # 0
```

**Strings** are not values you can store or join. A string literal is *read-only data in ROM*;
you pass it to functions that consume it (`text`, `print`, `sprite`). Printable ASCII 32..126
only, with the escapes `\n`, `\t`, `\0`, `\\`, `\"`.

```python
def main():
    text(0, 0, "line one\nline two")
    render()
```

---

## 5. Variables and scope

All variables are 16-bit. There are two kinds:

- **Globals** — a name assigned at the top level (directly, or as a `for` loop variable at the top
  level). Every function can read them.
- **Locals** — a name assigned anywhere inside a function body (including inside an `if`, `while`
  or `for`), or a function parameter.

To **modify** a global from inside a function you must say `global name` first; otherwise the
assignment creates a local that shadows the global — just like Python:

```python
score = 0

def bump():
    global score      # without this line, score below would be a new local
    score += 1

def read():
    return score      # reading a global needs no declaration
```

Reading a name that is neither a local nor a global is a compile error: *undefined variable*.

---

## 6. Operators

Arithmetic and bitwise operators, in the usual precedence:

| Operators | Meaning |
|-----------|---------|
| `*` `/` `%` | multiply, divide, remainder |
| `+` `-` | add, subtract |
| `<<` `>>` | shift left (logical), shift right (**arithmetic**) |
| `&` `^` `\|` | bitwise and, xor, or |
| `==` `!=` `<` `<=` `>` `>=` | comparison → `1` or `0` |
| `not` `and` `or` | boolean, → `1` or `0` |
| `-` `+` `~` | unary negate, plus, bitwise not |

Details that bite:

- **Division truncates toward zero** and `%` takes the sign of the dividend: `-7 / 2 == -3`,
  `-7 % 2 == -1`, `7 / -2 == -3`.
- **Division by zero yields 0** (both `/` and `%`) rather than trapping.
- `>>` is an **arithmetic** shift (it copies the sign bit), so `-64 >> 2 == -16`.
- Shift **counts** may be any expression. When both operands of a shift/arithmetic are constants it
  is folded at compile time; otherwise it shifts one bit at a time at runtime.
- Comparisons are **not chainable**: `a < b < c` is an error — write `a < b and b < c`.
- `and`/`or` short-circuit when used in a condition, and produce `0`/`1` when used as a value.

---

## 7. Control flow

```python
if score >= 10:
    win()
elif score >= 5:
    almost()
else:
    keep_going()

while lives > 0:
    play()
    lives -= 1

for i in range(0, 10):        # 0,1,...,9  (stop is exclusive)
    pixel(i, i)

for i in range(10, 0, -2):    # 10,8,6,4,2  (step may be negative)
    pixel(i, 5)
```

- `for` only iterates over `range(...)` with 1–3 arguments: `range(stop)`, `range(start, stop)`,
  `range(start, stop, step)`. The step must be a **constant** and non-zero.
- `break` and `continue` work inside `while` and `for`.
- `pass` is a no-op (useful for an empty block).

---

## 8. Functions

```python
def clamp01(v):
    if v < 0:
        return 0
    if v > 1:
        return 1
    return v

def greet(name_score, y=20):     # default values must be constants
    print(4, y, "score: ", name_score)

def main():
    greet(5)                     # y defaults to 20
    greet(9, 40)
    render()
```

- `def` is only allowed at the top level (no nested functions, no closures, no lambdas).
- Parameters are 16-bit locals. Trailing parameters may have **constant** defaults.
- A function without an explicit `return` returns 0.
- `return` with no value returns 0.
- `main()` must take no parameters.
- A user function may **replace a standard-library function of the same name** (e.g. define your
  own `abs`). You may not redefine a *built-in* (see [API.md](API.md#built-in-functions)).

Recursion is supported (see [§16](#16-recursion-and-the-stack)).

---

## 9. The screen

- The screen is **192 wide**; height is **64** rows on `880` and **63** on `580` (0..62).
- `pixel(x, y)` sets a pixel; `pixel(x, y, 0)` clears it; `pixel(x, y, c)` with non-zero `c` sets.
  Off-screen coordinates are silently ignored, so you can draw partly off-screen.
- On `880` the panel has two bitplanes (four grey levels) but Wiz's `pixel` only turns a pixel
  fully on or fully off. `580` is pure black/white.

### Shadow buffer and `render()`

Every drawing call writes to an off-screen **shadow buffer** (SB2), never to the LCD.
`render()` is the only call that copies SB2 to the visible LCD, so a frame appears
atomically and never flickers while it is being cleared and redrawn:

```python
def main():
    while True:
        clear()
        draw_scene()
        render()         # copy the finished frame (SB2) to the LCD (SB1)
        delay(3)
```

If you forget `render()`, nothing appears on screen. `flip()` is kept as an alias
for `render()`; `buffered()` is accepted for old programs but does nothing.

---

## 10. Drawing

The standard library provides the usual primitives (all coordinates clipped to the screen):

```python
clear()
rect(x, y, w, h, c=1)        # rectangle outline
fillrect(x, y, w, h, c=1)    # filled rectangle
hline(x0, x1, y, c=1)        # horizontal span, x0..x1-1
vline(x, y0, y1, c=1)        # vertical span, y0..y1-1
line(x0, y0, x1, y1, c=1)    # Bresenham line
circle(cx, cy, r, c=1)       # circle outline
disc(cx, cy, r, c=1)         # filled circle
```

`c` is the colour: non-zero draws, `0` erases. See [examples/02_shapes.wz](../examples/02_shapes.wz).

---

## 11. Text, numbers and `print`

```python
text(x, y, "Hello")     # draws a string, returns the x just past the last character
num(x, y, -1234)        # draws a signed decimal, returns the end x
char(x, y, 65)          # draws one character by ASCII code (out of range -> '?')
```

`print` is special sugar: it takes an `x`, a `y`, then any number of values, and lays them out
left to right. A string literal (or a `rom8`/array name) prints as **text**; anything else prints
as a **number**. It returns the end `x`.

```python
score = 42
def main():
    print(4, 4, "Score ", score, " of ", 100)   # -> "Score 42 of 100"
```

Because text is 6 pixels wide per character, you can use the return value to continue drawing:

```python
x = print(4, 20, "n = ", n)
print(x, 20, "!")
```

### Native ClassWiz CW Display font

The `*_cw` family uses the calculator's native 11x13 CW Display glyphs instead of the 6x8
bitmap.  It is not resized: the font's 10 units per LCD pixel become exactly one pixel, giving an
11 px advance, a 12 px cap height and one descender row.  Both the 580 and the 880 use the same
font data.

```python
text_cw(x, y, "Hello")  # one string, returns the end x
num_cw(x, y, -1234)     # one signed decimal, returns the end x
char_cw(x, y, 65)       # Wiz: one character by ASCII code
print_cw(2, 2, "score ", score)   # like print(), but with the CW Display font
```

CWiz spells the one-character helper `glyph_cw` (since `char` is a type):

```c
glyph_cw(2, 2, 'A');
text_cw(2, 2, "ClassWiz CW");
print_cw(2, 16, "score ", 99);
```

Use 14 px between lines (`y = 2, 16, 30, 44`) to leave a one-pixel gap below the 13-row cell.
The 2.4 KB table is emitted only when a `*_cw` function is referenced; programs that use only the
original 6x8 API keep their old size.

The current table covers printable ASCII (`32..126`), matching Wiz's byte-string model.  The
source font also contains Vietnamese and CJK glyphs, but Wiz strings are still one byte per
character, so those would need a separate Unicode string extension.

---

## 12. Sprites

A sprite is a packed 1-bit-per-pixel bitmap stored in `rom8`, one row of `ceil(width/8)` bytes,
MSB first:

```python
# an 8x8 arrow, 8 bytes
rom8 arrow = [
    0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18,
]

def main():
    sprite(20, 20, 8, 8, arrow)
```

`sprite(x, y, w, h, data)` — `data` is the address of the bitmap (the name of a `rom8` table, or
`another_table + offset`). Arbitrary widths are allowed; rows are byte-aligned.

---

## 13. Input

```python
k = key()                    # scan index 0..63 of the key being held, or -1 if none
if key_down(K_LEFT):
    x -= 2
if key_hit(K_OK):            # 1 only on the iteration where the key goes down
    do_something()
k = wait_key()               # block until a key is released, then pressed; returns it
```

**Prefer `key_down` / `key_hit` to raw `key()`.** They take a named key constant and handle the
scan matrix for you. `key_hit(K)` must be called once per loop iteration for the key you care
about; it remembers the previous state.

Named keys look like `K_UP`, `K_DOWN`, `K_LEFT`, `K_RIGHT`, `K_OK`, `K_EXE`, `K_AC`, `K_SHIFT`,
`K_0`..`K_9`, `K_PLUS`, … The **exact set differs per machine** (the ClassWiz and ClassWiz II key
pads are different). Using a key that does not exist on the current target is a compile error that
lists the valid names. The full tables are in [API.md](API.md#key-constants).

If a key is missing from a target but present on the hardware, you may define it once yourself:

```python
const K_EXE = 15      # only allowed for a K_* name the target does not already define
```

---

## 14. Timing

```python
delay(n)     # busy-wait n units (uncalibrated: measure on your hardware if it matters)
```

`delay` is the only timing primitive. Games use `delay(3)`–`delay(5)` per frame.

---

## 15. Arrays, tables and constants

### RAM arrays

Arrays are **16-bit**, **fixed-size**, and must be declared at the **top level**:

```python
scores = [10, 20, 30]      # an array of 3
zeros  = [0] * 64          # 64 cells, all zero
scores[1] = 99             # index read/write
scores[2] += 5             # compound assignment on an element
v = scores[i + 1]          # index is any expression
```

The index may be any expression. In a *compound* assignment (`scores[i] += 1`) the index may not
contain a function call (compile error). Writing past the end is not checked and corrupts RAM —
size your arrays correctly.

### ROM tables

Read-only tables live in ROM and cost no RAM:

```python
rom  wave  = [100, -5, 30000, 7]         # 16-bit entries
rom8 bytes = [1, 2, 255, 128]            # 8-bit entries (0..255)
rom8 msg   = "Hi!"                       # a string (a trailing 0 byte is added)
```

Read them with `[index]`, or use the table name as an address with `peek`/`sprite`/`text`.
`rom` (16-bit) names are addresses too, but are meant for numeric lookup.

### Constants

```python
const W = 192
const H = W / 3          # folded at compile time
const MASK = 1 << 4
```

`const` values must be constant expressions. Constants are usable as array-size multipliers,
loop bounds, default arguments, etc. A constant cannot be reassigned, and cannot be redefined —
except that you may shadow a *missing* built-in key name once (see [§13](#13-input)).

---

## 16. Recursion and the stack

Functions normally use **static frames** (fixed parameter/return slots), which is fast and uses no
stack. When the compiler detects that a function can reach itself (directly or through a cycle),
it automatically saves and restores that function's locals around the recursive call, so recursion
works:

```python
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)
```

You do **not** need tail calls. The build prints a note for each recursive function estimating
bytes of stack per level and the approximate maximum depth (the stack has roughly 2000 bytes,
growing down from 0xF000). Mutually recursive functions (`is_even`/`is_odd`) are handled too.

---

## 17. One file, several machines

The key pads and memory maps differ between the ClassWiz and ClassWiz II. A file can hold code for
several machines using `for <target>` sections. A section lasts until a line of `---`/`===`
separators; a `for all` section applies everywhere; code *before the first header* and *after a
separator* is common.

```python
# common setup
score = 0

for 880                      # ML620909 only   (the internal name `cwii` also works)
const SCREEN_H = 64
def exit_key():
    return K_OK
─────────                    # any run of 3+ dashes/equals signs works
for 580                      # ML620906 only   (the internal name `cw` also works)
const SCREEN_H = 63
def exit_key():
    return K_EXE
─────
# common again: this runs on every target
def main():
    clear()
    print(4, 4, "score ", score)
    render()
```

`cwapp` is a third header (`for cwapp`) for the fx-580VN X firmware patch.

`wizc` builds **one** target per invocation. Without `--target` it uses the file's only declared
target if there is exactly one; if the file declares several and none is the default, it uses the
first declared one; otherwise the default (`880`, or `WIZ_TARGET`). To build every section, run it
once per target:

```console
$ build/wizc --target 580 -o game.580.bin game.wz
$ build/wizc --target 880 -o game.880.bin game.wz
```

Output names default to the source name with `.bin`/`.asm`; use `-o` to keep targets apart. See
[RUNNING.md](RUNNING.md#multi-target-builds).

---

## 18. Common mistakes

The compiler prints gcc-style diagnostics: `file:line:col: error: message`, then a source excerpt
with a `^` caret and a `N error(s) generated.` summary.

| Message | Fix |
|---------|-----|
| `tabs are not allowed for indentation` | use spaces |
| `undefined variable 'x'` | assign it at the top level (global) or before use, or declare it |
| `... is a constant, cannot be a parameter` / `cannot assign to constant` | rename, or don't use a `const` name as a variable |
| `chained comparisons are not supported` | `a < b and b < c` |
| `... is a table/array, cannot assign to the name itself` | index it: `arr[i] = …` |
| `... is not a RAM array (rom tables are read-only)` | don't assign to a `rom`/`rom8` table |
| `call to unknown function` | the function (or standard-library name) is misspelled or not defined |
| `K_X is not a key on <target>` | that key does not exist there; see [API.md](API.md#key-constants) |
| `out of RAM for variables` | shrink arrays / use fewer globals (esp. on `580`, which has only 4 KB) |

---

## 19. Where to go next

- Browse [examples/](../examples) — each file is heavily commented, from `01_hello` to a full
  Pong game and a firmware-patchable app; `examples/cwiz/` mirrors the tour in CWiz.
- The [API reference](API.md) lists every built-in and standard-library function.
- [ARCHITECTURE.md](ARCHITECTURE.md) explains what the compilers emit and how the runtime works.

---

## 20. CWiz in C syntax

**CWiz** is the same toolchain with a C-syntax language. It has its own compiler
(`compiler/src/cwiz.cpp`) but shares the assembler, runtime, boot templates, targets, built-ins and
graphics library. Its source files end in `.cwiz` and its default target is `880`.

```c
// hello.cwiz
int score = 0;

int bump(int by = 1) {          // trailing parameters may have constant defaults
    score = score + by;
    return score;
}

void main() {
    clear();
    text(8, 6, "Hello, CWiz!");
    print(8, 18, "score = ", bump(5));   // print() is a compiler special form
    render();
    while (1) {
        if (key_hit(K_EXE)) { break; }
        delay(3);
    }
}
```

The common ground: a signed 16-bit `int`, the same operators and semantics as [§6](#6-operators),
the same built-ins and standard library, the same screen/input/timing model and the same
`for <target>` sections.

The C-shaped bits:

- Types are `int`, `char`, `void` (all 16-bit at run time; `char` is a spelling, not a width).
- Blocks are `{ }`; statements end with `;`; comments start with `//`.
- Control flow: `if`/`else`, `while`, `do … while`, C-style `for (init; cond; step)`, `break`,
  `continue`, `return`.
- Declarations:
  ```c
  const int W = 192;
  int score = 0;                 // a global
  int values[64];                // a RAM array
  rom  int table[4] = {1, 2, 3, 4};
  rom8 char msg[20] = "hello";   // a string in ROM
  rom8 char sprite[8] = "\x18\x3C\x7E\xFF\xFF\x7E\x3C\x18";
  ```
- Expressions include the ternary `?:`, `&&`/`||`/`!`, `++`/`--` and compound assignment
  (`+=`, `<<=`, …). Character literals are written `'A'`.
- `_ROWS` and the target's `K_*` key constants are injected as predefined constants.

The deliberate differences from Wiz:

- The single-character helper is **`glyph(x, y, c)`**, not `char` (C reserves `char` as a type).
- `char` arrays hold one byte per element (`char buf[8]`), `int` arrays two; a `rom`/`rom8` table
  is read-only and lives in ROM. There are **no structs** and only one index level
  (`a[i]`, never `a[i][j]`) — but a pointer *is* indexable, see [§20.2](#202-pointers-and-memory).
- `char`, `rom`, `rom8`, `void`, … are keywords, so you cannot use them as identifiers.

`print(x, y, …)` works exactly as in [§11](#11-text-numbers-and-print); a string literal or a
`rom8` name prints as text, everything else as a number.  `print_cw(x, y, …)`,
`text_cw(x, y, s)`, `num_cw(x, y, n)` and `glyph_cw(x, y, c)` are the native ClassWiz CW
Display set (11x13, 1x); see [§11](#native-classwiz-cw-display-font).

### 20.1 Preprocessor directives

`#` is a comment character in Wiz, so directives exist **only in CWiz**. They run before the
target sections are selected and before parsing, and they are line-preserving: a directive line
becomes an empty line, so the line numbers in diagnostics are the line numbers in your file.

```c
#include "sprites.cwiz"          // inlined here; guard with #pragma once

#define W 192                    // object-like macro
#define HUD(x, y) text(x, y, "score")   // function-like macro, ... for a variadic tail
#define AREA (W * ROWS)

#pragma once                     // this file is included at most once
#undef HUD

#if defined(__CW__)              // 580 family (the cw / cwapp targets)
const int ROWS = 63;
#elif defined(__CWII__)          // 880 family (the cwii target)
const int ROWS = 64;
#else
#error "unknown machine"
#endif

#ifdef DEBUG                     // DEBUG is never defined by the toolchain
rom8 char debug_tag[4] = "dbg";
#endif
```

- Conditionals: `#ifdef`, `#ifndef`, `#if`, `#elif`, `#else`, `#endif`. `#if` takes the usual C
  operators (arithmetic, `<<`, `>>`, `&`, `|`, `^`, `~`, `!`, comparisons, `&&`, `||`, `?:`,
  parentheses), plus `defined(NAME)`; an identifier that is not a macro counts as `0`.
- Other directives: `#define`, `#undef`, `#include "file.cwiz"`, `#pragma once`, `#error`.
- Predefined macros: `__CWIZ__`, exactly one of `__CW__` / `__CWII__` / `__CWAPP__`, and
  `__ROWS__` (the target's row count).
- `#for 580` / `#for 880` is the **target directive** — the directive spelling of the
  `for <targets>` section header ([§17](#17-one-file-several-machines)) — and `#cwapp` marks the
  section that follows as the **app**: a patch for the fx-580VN X firmware rather than a ROM file
  (write it after `#for 580`). A `----` line ends the section and returns to code that every
  target gets:

  ```c
  #for 580
  void leave() { }              // 580: a plain ROM, nothing to leave
  #cwapp
  void leave() { exit(); }      // the app: back to the fx-580VN X menu
  ----
  void main() { ... leave(); }   // built for 580 and for the app
  ```

`#include` is the one thing that adds lines; when a diagnostic points inside an included file the
drivers report that file and line, and `wiz::cwiz_line_origin()` in [API.md](API.md) exposes the
same map.
See [examples/cwiz/07_preproc.cwiz](../examples/cwiz/07_preproc.cwiz).

### 20.2 Pointers and memory

A pointer is a 16-bit address. Because every CWiz variable, array and local already has an
absolute address, `&x` is simply that address and `*p` is a load or a store through a register:

```c
int hist[64];
int total;

rom8 char hello[6] = "hi";

void add(int v) {
    int *p = hist;            // an array decays to a pointer to its first element
    p = p + total;            // int* moves two bytes per step
    *p = v;                   // store through the pointer
    total = total + 1;
}

void main() {
    int i;
    int *first;
    char *c;
    for (i = 0; i < 64; i = i + 1) { add(i * i); }
    first = &hist[0];         // the address of an element
    c = hello;                // a ROM pointer: pass it to a string built-in
    text(0, 0, "hi");
    num(0, 12, hist[63]);     // p[i] and *(p + i) are the same thing
    num(0, 24, *first);
    render();
}
```

- `int *p` steps two bytes per `+`, `-`, `++` and `--`; `char *q` steps one. `p[i]` is
  `*(p + i)`. `q - p` for two `int *`s gives the element count.
- `&x`, `&array[i]`, `&*p` (which is just `p`), and assigning an array name to a pointer all work.
  `int **pp` holds the address of a pointer, so `**pp = 8` writes through two levels.
- Pointers can be passed to functions and returned; a function can walk a caller's array.
- Comparisons work on addresses as plain 16-bit unsigned values, so `p < q` is a valid loop
  condition.

Three honest caveats:

1. **RAM only.** All addresses are RAM (or SFR) addresses in a single 64 KB space. A `rom`/`rom8`
   table lives in a different segment, so taking its address is a compile error — read ROM with
   `peek` / `_rpeek` and the table's index instead, or keep a copy in a RAM array.
2. **Recursion still moves locals.** `&local` is a fixed address, but a recursive call saves and
   restores the locals of the frame being re-entered ([§16](#16-recursion-and-the-stack)), so a
   pointer to a local is only meaningful for as long as that frame is the live one. Pointers to
   *globals* and to arrays are always stable.
3. **No casts and no type checking on assignment.** `int` and `char` are both two bytes, and a
   pointer is two bytes, so `p = 0xA000;` and `x = p;` compile. The compiler checks what it can
   (`*x` on an `int` is an error) but it will not stop you from writing an address you should not.

See [examples/cwiz/08_pointers.cwiz](../examples/cwiz/08_pointers.cwiz) and
[API.md](API.md#cwizhpp--the-cwiz-front-end) for the compiler interface.
