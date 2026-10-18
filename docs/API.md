# Wiz / CWiz toolchain API reference

Quick reference for everything the toolchain exposes: the command-line tools, the built-ins, the
standard library, special forms, key constants, the memory map, targets, the C++ APIs, the runtime
ABI, the assembler and the simulator.

New to Wiz? Read [GUIDE.md](GUIDE.md) first.

---

## Command-line tools

All four tools are built into `./build` by [`build.sh`](../build.sh) and share the same gcc-like
coloured output (auto-detected TTY, `NO_COLOR`, `--color=auto|always|never`).

| Tool | Usage |
|------|-------|
| `wizc`         | `wizc [options] file…` — compile `.wz` / `.cwiz` to `<name>.bin` (+ `<name>.asm`) |
| `wizrun`       | `wizrun [options] file` — compile and run in the simulator, print the LCD |
| `customromrun` | `customromrun --model 580\|880 rom.bin` — run a custom ROM on a CasioEmuMsvc model |
| `patch_menu`   | `patch_menu rom.bin app.wz out.bin` — patch a program into an fx-580VN X firmware |
| `wiztests`     | `wiztests` — run the toolchain test suite |

`wizc` options: `-o FILE`, `--target 580|880`, `--cwapp`, `--lang wiz|cwiz`, `-S` (listing only),
`-E`/`--pp` (print the preprocessed/selected source), `--no-asm`, `--color[=WHEN]`, `-v`,
`--version`, `-h`.
`wizrun` options: `--target`, `--cwapp`, `--steps N`, `--keys LIST`, `--press LIST`, `--full`,
`--color`, `-v`, `--version`, `-h`.
Full details in [RUNNING.md](RUNNING.md).

---

## Built-in functions

Built-ins are provided by the runtime (`compiler/runtime.asm`) and are available in **both**
languages. You cannot define a function with one of these names. Unless noted, arguments are signed
16-bit and the result (if any) is returned in `ER0`.

| Call | Args | Returns | Description |
|------|------|---------|-------------|
| `pixel(x, y)` | 2 | — | Set the pixel at `(x, y)`. Off-screen coordinates are ignored. |
| `pixel(x, y, c)` | 3 | — | Set (`c != 0`) or clear (`c == 0`) the pixel. |
| `clear()` | 0 | — | Clear the LCD (direct mode) or the back buffer (buffered mode). |
| `key()` | 0 | scan index | Index `0..63` of the key being held (`8*KO + KI`, MSB first), or `-1` if none. |
| `key_down(k)` | 1 | 0/1 | `1` while key `k` is held. |
| `key_hit(k)` | 1 | 0/1 | `1` only on the call where `k` went from released to held. Call it every loop iteration. |
| `delay(n)` | 1 | — | Busy-wait `n` units. Units are uncalibrated. |
| `peek(addr)` | 1 | 0..255 | Unsigned byte at `addr` (RAM, SFR or ROM). |
| `poke(addr, v)` | 2 | — | Store the **low byte** of `v` at `addr`. |
| `buffered(on)` | 1 | — | `on != 0`: draw into the back buffer (cleared now). `on == 0`: draw straight to the LCD. |
| `flip()` | 0 | — | Copy the back buffer to the LCD. |
| `exit()` | 0 | — | **cwapp only.** Leave the program and restart the firmware (returns to the menu). |
| `_hspan(x0, x1, y, c)` | 4 | — | Internal fast span fill, `x0..x1-1`. Requires `0 <= x0 < x1 <= 192`, `0 <= y < ROWS`. |
| `_rpeek(addr)` | 1 | 0..255 | Internal: like `peek` but assumes the data segment of ROM tables (used on `cwapp`). |

`key_hit` state is kept in runtime RAM; see the [memory map](#memory-map).

---

## Standard library

These are written in the source language and included on demand (Wiz: the `PRELUDE` in
`compiler/src/wiz.cpp`; CWiz: `PRELUDE_SRC` in `compiler/src/cwiz.cpp`). Only the routines a program
actually calls are compiled in, and **defining a function with the same name replaces the library
version**.

### Math

| Call | Description |
|------|-------------|
| `abs(n)` | absolute value |
| `min(a, b)` / `max(a, b)` | smaller / larger |
| `clamp(v, lo, hi)` | `v` constrained to `[lo, hi]` |
| `sign(n)` | `-1`, `0` or `1` |

### Input

| Call | Description |
|------|-------------|
| `wait_key()` | waits for all keys to be released, then for a press; returns the key |

### Random numbers

| Call | Description |
|------|-------------|
| `srand(s)` | seed the generator |
| `rand(n)` | next value in `0..n-1` |

The generator is a 16-bit LCG: `seed = seed * 25173 + 13849` (mod 2¹⁶), and the result is
`((seed >> 1) & 0x7FFF) % n`. The default seed is `12345`, so `rand` is usable without `srand`.

### Drawing primitives

All coordinates are clipped to the visible screen. `c` is the colour: non-zero draws, `0` erases.

| Call | Description |
|------|-------------|
| `hline(x0, x1, y, c=1)` | horizontal span `x0..x1-1` (half-open) |
| `vline(x, y0, y1, c=1)` | vertical span `y0..y1-1` (half-open) |
| `rect(x, y, w, h, c=1)` | rectangle outline |
| `fillrect(x, y, w, h, c=1)` | filled rectangle |
| `line(x0, y0, x1, y1, c=1)` | Bresenham line |
| `circle(cx, cy, r, c=1)` | circle outline |
| `disc(cx, cy, r, c=1)` | filled circle |

### Text and sprites

| Call | Returns | Description |
|------|---------|-------------|
| `char(x, y, c)` / `glyph(x, y, c)` | — | Draw one character, ASCII code `c` (out of `32..126` draws `?`). Wiz: `char`; CWiz: `glyph`. |
| `text(x, y, s)` | end `x` | Draw a NUL-terminated string from ROM. |
| `num(x, y, n)` | end `x` | Draw signed decimal `n`. |
| `sprite(x, y, w, h, data)` | — | Draw a 1-bit-per-pixel bitmap; `data` is a `rom8` table (or table + offset). |

CWiz names its glyph helper `glyph` because `char` is a reserved word (it is a type). Wiz names it
`char`.

### `print`

`print(x, y, a, b, c, …)` lays out its arguments left to right, starting at `(x, y)`, and returns
the final `x`. Each argument is rendered as **text** if it is a string literal or the name of a
`rom8` table, and as a **number** otherwise. It is expanded by the compiler into a chain of
`text`/`num` calls (so it needs at least three arguments).

```python
print(4, 4, "Score ", score, " / ", 100)    # -> "Score 42 / 100"
```

---

## Key constants

Key names are compile-time constants of the **scan index**. The two machines have different key
pads, so a name valid on one is an error on the other (the compiler lists the valid names). You may
define a `const K_*` once for a name a target does not already define.

In CWiz the same `K_*` names are injected as predefined constants; in Wiz they are keywords built
into the front end.

Example: `if key_hit(K_OK): …`

### `880` / `cwii` — ClassWiz II (ML620909)

`K_0`=11 `K_1`=63 `K_2`=55 `K_3`=47 `K_4`=62 `K_5`=54 `K_6`=46 `K_7`=61 `K_8`=53 `K_9`=45
`K_UP`=32 `K_DOWN`=34 `K_LEFT`=41 `K_RIGHT`=25 `K_OK`=33 `K_HOME`=48 `K_BACK`=49
`K_SETTINGS`=57 `K_SHIFT`=58 `K_VAR`=50 `K_FUNC`=42 `K_CATALOG`=26 `K_TOOLS`=18
`K_PGUP`=16 `K_PGDOWN`=17 `K_X`=59 `K_FRAC`=51 `K_SQRT`=43 `K_POWER`=35 `K_SQUARED`=27
`K_LOGAB`=19 `K_ANS`=60 `K_SIN`=52 `K_COS`=44 `K_TAN`=36 `K_LPAREN`=28 `K_RPAREN`=20
`K_DEL`=37 `K_AC`=29 `K_MUL`=38 `K_DIV`=30 `K_PLUS`=39 `K_MINUS`=31 `K_DOT`=12
`K_SCI`=13 `K_FORMAT`=14 `K_EXE`=15

### `580` / `cw` / `cwapp` — ClassWiz (ML620906)

`K_0`=11 `K_1`=63 `K_2`=55 `K_3`=47 `K_4`=62 `K_5`=54 `K_6`=46 `K_7`=61 `K_8`=53 `K_9`=45
`K_UP`=40 `K_DOWN`=33 `K_LEFT`=41 `K_RIGHT`=32 `K_MENU`=24 `K_OPTN`=57 `K_CALC`=49
`K_INTEGRAL`=25 `K_X`=17 `K_FRAC`=58 `K_SQRT`=50 `K_SQUARED`=42 `K_POWER`=34 `K_LOG`=26
`K_LN`=18 `K_NEG`=59 `K_DEG`=51 `K_INV`=43 `K_SIN`=35 `K_COS`=27 `K_TAN`=19
`K_STO`=60 `K_ENG`=52 `K_LPAREN`=44 `K_RPAREN`=36 `K_SD`=28 `K_MPLUS`=20
`K_DEL`=37 `K_AC`=29 `K_MUL`=38 `K_DIV`=30 `K_PLUS`=39 `K_MINUS`=31 `K_DOT`=12
`K_EXP`=13 `K_ANS`=14 `K_EXE`=15

`K_NONE` is `-1` on both.

> These scan codes are transcribed from existing references (`libcw.h`, etc.). They are not all
> verified against real hardware — see the disclaimer in the [README](../README.md#status--honesty).

---

## Memory map

Variables/arrays are allocated upward from `var_base`; the runtime state and stack live above.

| Region | `880` / `cwii` (ML620909) | `580` / `cw` / `cwapp` (ML620906) |
|--------|---------------------------|-----------------------------------|
| Program ROM | `0x0000`–`0x8FFF` | `0x0000`–`0xCFFF` |
| Variables & arrays | `0x9000`–`0xDFFF` (up to 20 KB) | `0xD000`–`0xDFFF` (up to 4 KB) |
| Back buffer | `0xE000`–`0xE7FF` | `0xE000`–`0xE7FF` |
| Draw mode (`buffered`) | `0xE800` | `0xE800` |
| `key_hit` bitmap | `0xE808`–`0xE80F` | `0xE808`–`0xE80F` |
| Stack (grows down from `0xF000`) | `0xE810`–`0xF000` (~2 KB) | `0xE810`–`0xF000` (~2 KB) |
| LCD VRAM | `0xF800` + `y*32 + (x>>3)`, plane select `0xF037` | `0xF800` + `y*32 + (x>>3)` |

On `580`, VRAM row 0 is not displayed: user `y = 0` is written to VRAM row 1 (`row_off = 1`). The
visible height is therefore 63 rows.

---

## Targets

See the table in the [README](../README.md#targets). `compiler/src/targets.cpp` holds the
authoritative description (`wiz::Target`, declared in `targets.hpp`):

| Field | Meaning |
|-------|---------|
| `name` | target id (`cwii`, `cw`, `cwapp`) |
| `chip` | chip name (`ML620909` / `ML620906`) |
| `tmpl` | boot template file |
| `rom_limit` | first address that is not ROM |
| `ram_lo` | first RAM address |
| `var_base`, `var_limit` | range the compiler allocates variables/arrays in |
| `planes` | LCD bitplanes (2 = `cwii`, 1 = `cw`) |
| `rom_seg` | data segment (DSR) of ROM tables; 3 for `cwapp` |
| `rows` | visible LCD rows (64 / 63) |
| `core` | CPU core for the assembler/simulator |
| `row_off` | VRAM row of user `y = 0` (0 / 1) |

`wiz::canonical(name)` maps the user-facing names and internal names onto the internal ones
(`580`/`cw` → `cw`; `880`/`cwii` → `cwii`; `cwapp` → `cwapp`). `wiz::get_target(name)` throws for
anything else.

### Multi-target sources and the CLI

```
build/wizc prog.wz [out.bin] [--target 580|880]
```

- Without `--target`, `wizc` uses the file's only declared target if it has exactly one; else, if
  the file declares sections and the default target is not among them, the first declared target;
  otherwise the default target (`WIZ_TARGET`, else `880`).
- One invocation builds **one** target. To build every section of a multi-target file, run `wizc`
  once per target, e.g. `wizc --target 580 -o game.580.bin game.wz` and
  `wizc --target 880 -o game.880.bin game.wz`.
- Output name: `prog.bin` (or the `-o` path), with a `.asm` listing written next to it.
- Section syntax (`for 580` / `for 880` / `for cwapp` / `for all`, separated by `-----`) is
  described in [GUIDE §17](GUIDE.md#17-one-file-several-machines). Line numbers are preserved across
  sections so errors point at the right line. In CWiz the same headers can be written as directives
  — `#for 580`, `#for 880`, and `#cwapp` for the fx-580VN X firmware-app section
  ([GUIDE §20.1](GUIDE.md#201-preprocessor-directives)).

---

## C++ API

The toolchain is a static library, `wizcore` (see `compiler/CMakeLists.txt`), plus four thin
drivers. Every header lives in `compiler/src/` and is in `namespace wiz`.

### `common.hpp` — helpers and the error type

```cpp
struct CompileError : std::runtime_error { int line; int col; };  // 1-based, or -1
struct AsmError : CompileError {};

std::string fmt(const char* f, ...);                 // printf into a std::string
std::string lstrip/rstrip/strip(const std::string&, const char* extra = "");
std::string to_lower(std::string);
std::vector<std::string> split(const std::string&, char sep);
std::string join(const std::vector<std::string>&, const std::string& sep);
bool contains(const std::vector<std::string>&, const std::string&);
std::string replace_all(std::string, const std::string& from, const std::string& to);
bool starts_with/ends_with(const std::string&, const std::string&);

std::string read_text_file(const std::string& path, bool* ok = nullptr);
std::vector<uint8_t> read_binary_file(const std::string& path, bool* ok = nullptr);
bool write_text_file(const std::string& path, const std::string& data);
bool write_binary_file(const std::string& path, const std::vector<uint8_t>& data);
```

### `diag.hpp` — diagnostics

gcc-style, coloured. Prints `file:line:col: error: message`, a source excerpt with a `^` caret,
then a `N error(s) generated.` summary. The `:col` part appears when the front end knows the column
(CWiz always does; the Wiz front end sometimes reports only `file:line`).

```cpp
enum class ColorMode { Auto, Always, Never };
void set_color_mode(ColorMode);
void set_color_mode_from_arg(const std::string&);   // "auto" | "always" | "never"
bool stdout_is_tty(); bool stderr_is_tty();

struct Diag {
    std::string tool; int errors = 0, warnings = 0, notes = 0;
    void error(...); void warning(...); void note(...);
    [[noreturn]] void fatal(...);
    int summary();
    void ok(const std::string&); void info(const std::string&);
};
std::string source_line_at(const std::string& text, int line);   // 1-based
```

The colour mode starts at `Auto` and honours `NO_COLOR` and `TERM=dumb`; `--color=WHEN` overrides it.

### `cli.hpp` — argument parsing and data-dir autodetect

```cpp
struct CliOption {
    std::string flag;             // exact spelling: "--target", "-o"
    std::string name;             // canonical key
    bool takes_value = false;
    bool value_optional = false;  // --color[=WHEN]
    std::string const_value = "always";
    std::vector<std::string> allowed;   // restrict the consumed value
};
struct Cli {
    std::vector<std::string> positional;
    std::set<std::string> present;
    std::map<std::string, std::string> values;
    bool has(const std::string&) const;
    std::string value(const std::string&, const std::string& def = "") const;
};
bool parse_cli(int argc, char** argv, const std::vector<CliOption>&, Cli&, std::string& error);

std::string autodetect_data_dir(const char* argv0);   // dir holding runtime.asm
std::string splitext_root(const std::string&);        // "a/b.wz" -> "a/b"
std::string path_basename/path_dirname(const std::string&);
```

`autodetect_data_dir` looks (in order) at `$WIZ_ROOT`, the executable's own directory and
its parents, and the current directory; the default is `./compiler`.

### `targets.hpp` — the target table and `for <target>` sections

```cpp
struct Target { std::string name, chip, tmpl; int rom_limit, ram_lo, var_base, var_limit;
                int planes; int rom_seg = 0; int rows = 64;
                std::string core = "nX-U8/100"; int row_off = 0; };

extern const Target TARGET_CWII, TARGET_CW, TARGET_CWAPP;
extern const std::string DEFAULT_TARGET;              // WIZ_TARGET env or "cwii"

std::optional<std::string> canonical(const std::string& name);
const Target& get_target(const std::string& t);
struct SectionError : std::runtime_error {};

std::vector<std::string> declared_targets(const std::string& src);
std::string select_sections(const std::string& src, const Target& target);
```

`select_sections` blanks out foreign `for` lines **without changing line numbers**, so compile
errors keep pointing at the original line.

### `wiz.hpp` — the Wiz front end

```cpp
struct WizError : CompileError {};
struct WizResult { std::string asm_body; std::map<std::string, int> variables; };

WizResult compile_to_asm(const std::string& src, const Target& target);
const std::vector<std::string>& last_notes();          // build notes from the last compile
const std::map<std::string, int>& key_consts(const std::string& target_name);
```

### `cwiz.hpp` — the CWiz front end

```cpp
struct CwizError : CompileError { explicit CwizError(const std::string&, int line, int col); };
struct CwizVarInfo { int addr = 0; std::string kind; int size = 0; };   // kind: var|array|rom|rom8
struct CwizResult { std::string asm_body; std::map<std::string, CwizVarInfo> variables; };

CwizResult compile_cwiz(const std::string& src, const Target& target);
```

`compile_cwiz` expects already-preprocessed source (run it through `preprocess_cwiz` and
`select_sections` first — `build_cwiz` does both). Pointers are a language feature handled inside
the front end, so there is no extra entry point: `int*` / `char*` declarators, `&x`, `*p`, `p[i]`
and pointer arithmetic all compile to plain loads and stores through a register pair
([GUIDE §20.2](GUIDE.md#202-pointers-and-memory)).

### `cpre.hpp` — the CWiz preprocessor

```cpp
std::string preprocess_cwiz(const std::string& src, const std::string& file,
                            const std::string& target);   // target: cw | cwii | cwapp

struct CwizOrigin { std::string file; int line = 0; };
CwizOrigin cwiz_line_origin(int line);                    // output line -> real file:line

struct CwizPos { std::string file; int line = 0; };
CwizPos cwiz_error_pos(const std::string& fallback_file, int line);
```

`preprocess_cwiz` expands C-style directives (`#define`, `#undef`, `#if`/`#elif`/`#else`/`#endif`,
`#ifdef`/`#ifndef`, `#include`, `#pragma once`, `#error`), rewrites `#for 580, 880` /
`#for 880` into the `for …` target-section headers the section filter understands, accepts `#cwapp`
as the marker for an fx-580VN X firmware-app section, and defines `__CWIZ__`, one of
`__CW__` / `__CWII__` / `__CWAPP__`, and `__ROWS__`.

The output is **line-preserving** — a directive line becomes an empty line — so errors from later
stages keep pointing at the line you wrote. `#include` is the exception: `cwiz_line_origin(label)`
maps a 1-based output line back to the file and line it really came from (used by `wizc` to print the
excerpt from the included file), and `cwiz_error_pos` is the convenience wrapper for a diagnostic.

### `build.hpp` — the shared pipeline

```cpp
struct BuildResult {
    std::vector<uint8_t> rom;
    std::string full_asm;
    std::map<std::string, int> variables;   // name -> RAM address
    std::map<std::string, int> syms;        // assembler symbol table
};

void set_data_dir(const std::string& dir);
const std::string& data_dir();

std::string make_boot(const Target& target);     // boot template, linker-only lines removed
std::string runtime_for(const Target& target);   // runtime.asm, filtered per target

BuildResult build_source(const std::string& src, const std::string& target);  // Wiz
BuildResult build_cwiz(const std::string& src, const std::string& target);    // CWiz

struct PathBuild { BuildResult r; std::string lang; };
PathBuild build_path(const std::string& path, const std::string& target,
                     const std::string& forced_lang = "");
std::string language_of(const std::string& path, const std::string& forced = "");
```

`build_path` selects the front end by extension (`.cwiz` → CWiz, else Wiz) unless `forced_lang` is
`"wiz"` / `"cwiz"`. `build_source` throws `CompileError` (compiler) or `AsmError` (assembler).
For a CWiz source `build_cwiz` runs `preprocess_cwiz` and then `select_sections` before compiling;
the error-excerpt helpers that follow an `#include` are in [`cpre.hpp`](#cprehpp--the-cwiz-preprocessor).

### `asm.hpp` — the assembler

```cpp
std::string resolve_core(const std::string& name);   // "nX-U8/100" | "nX-U16/100"
class Assembler {
public:
    explicit Assembler(const std::string& core = "nX-U8/100");
    std::map<uint32_t, uint8_t> assemble(const std::string& text, int base = 0);
    std::string core;
    std::map<std::string, int> syms;
};
std::vector<uint8_t> to_bin(const std::map<uint32_t, uint8_t>& image, int size = 0);
```

### `sim.hpp` — the simulator

```cpp
class U8 {
public:
    U8(const std::vector<uint8_t>& rom,
       std::vector<std::pair<int, int>> keys = {},   // (KO mask, KI mask) held down
       const std::string& target = "",               // "cwii" | "cw" | "cwapp"
       int start_csr = -1, int start_pc = -1,        // optional (csr, pc) entry (cwapp)
       std::vector<std::pair<int, int>> stop = {});  // (csr, pc) addresses that stop execution

    std::string run(long max_steps = 5000000);       // human-readable reason
    int pixel(int x, int y) const;                   // 0, 1 or 3 (user coordinates)
    std::string ascii() const;                       // the LCD as text
    // state: mem (64 KB + segment space), planes[2], r[16], pc, sp, csr, steps, keys, …
};
```

### `modelinfo.hpp` / `version.hpp`

```cpp
std::string model_rom_path(const std::vector<uint8_t>& config_bin);   // ROM filename, else "rom.bin"
#define WIZ_VERSION "2.0.0"
```

`modelinfo.hpp` is just enough of a CasioEmuMsvc `config.bin` reader for `customromrun`.

---

## The runtime ABI

`compiler/runtime.asm` is assembled **after** the program body; the boot template is **prepended**.
The compiler and the runtime agree on the following convention.

### Entry point and program skeleton

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

### Registers and calls

- **Result in `ER0`**, the **second operand in `ER2`**, and further built-in arguments in `ER4`,
  `ER6`, … Built-ins are called with `BL __<name>` (`_hspan` → `__hspan`, `_rpeek` → `__rpeek`).
  `pixel` with two arguments defaults `c` to 1 in `ER4`.
- The runtime may clobber `ER2`–`ER14`. Leaf routines return with `RT`; the few that call others
  (`__buffered`, `__key_hit`) use `PUSH LR … POP PC` like compiled functions.
- User functions are called with `BL f_<name>`.

### Variables, tables and strings

- **Variables are 2-byte (16-bit) cells**, allocated upward from `var_base`. A global is stored
  under its own name; a local under `function.name`; a `for`-loop's saved end value under
  `function.__endN`.
- **ROM tables** are emitted with the label `__d_<name>` (`rom` → `DW`, `rom8` → `DB`).
- **String literals** are interned as `__s0`, `__s1`, … (`DB`, with a trailing NUL byte).
- `rom8` byte loads zero-extend (`MOV R1, #0`); `rom` loads are 16-bit. On `cwapp`, a `DSRI #3`
  prefix selects the data segment before a table/string access.

### Runtime routines

| Routine | Purpose |
|---------|---------|
| `__mul16` | `ER0 × ER2` → `ER0` (low 16 bits), shift-and-add |
| `__div16` | signed division: quotient in `ER0`, remainder in `ER2` (signs handled separately) |
| `__shl16` / `__sra16` | `ER0 << R2` / arithmetic `ER0 >> R2` |
| `__pixel` | plot/clear one pixel; picks LCD or back buffer from the draw-mode cell |
| `__clear` | clear the LCD (both planes) or the back buffer |
| `__buffered` | set the draw mode; clears the back buffer when turned on |
| `__flip` | copy the back buffer to the LCD |
| `__rt_init` | zero draw mode and the `key_hit` bitmap |
| `__key` | scan the key matrix → index or `-1` |
| `__key_down` / `__key_hit` | held / edge-detect for one key |
| `__delay` | busy loop |
| `__peek` / `__rpeek` / `__poke` | byte load (segment 0 / table segment) / store |
| `__hspan` | fast horizontal span (used by `hline`/`fillrect`) |

`__exit` (cwapp only) lives in `template_cwapp.asm`, not the runtime.

### Per-target filtering

`build.runtime_for()` drops lines tagged for the other machine: `;@1p` (1-plane) or `;@2p`
(2-plane), `;@seg` when `rom_seg == 0`, and it adjusts the `;@off` VRAM row offset for `580`. The
first emitted line is `_ROWS EQU <rows>`, which `__pixel`/`__hspan` use to clip.

---

## The assembler

Assembles the nX-U8/100 and nX-U16/100 cores. One statement per line; `;` starts a comment.
Numbers: decimal, `0xNN`, or `NNh`. Words are 16-bit little-endian; the output is a flat ROM image
starting at `0x0000` (unwritten bytes are `0xFF`).

Used as a library through `wiz::Assembler` (see [above](#asmhpp--the-assembler)); there is no
stand-alone assembler driver. In C++:

```cpp
#include "asm.hpp"
wiz::Assembler as("nX-U16/100");
auto img = as.assemble(text);          // {address: byte}
auto rom = wiz::to_bin(img);
```

### Directives

| Directive | Meaning |
|-----------|---------|
| `label:` | define a label at the current address |
| `NAME EQU expr` | define a constant |
| `ORG expr` | set the current address |
| `DW e[, e…]` | emit 16-bit words |
| `DB e[, e…]` | emit bytes |

### Instructions

- **No operand:** `NOP RT RTI BRK EI DI SC RC CPLC INC DEC` (also `INC [EA]` / `DEC [EA]`).
- **Branches:** `B label | ERn`, `BL label | ERn`, `JMP label` (= `B`).
  Conditional `Bcc label` for `cc` in `GE LT GT LE GES LTS GTS LES NE EQ NV OV PS NS AL`
  (short, ±127 words). `Jcc label` is a pseudo-op that inverts the branch over a full `B`, so it
  reaches anywhere.
- **Data transfer:** `L Rn|ERn, src` / `ST Rn|ERn, dst` where `src`/`dst` is `[ERn]`, `[EA]`,
  `[EA+]`, `label[ERn]`, or a direct `Dadr`.
- **Arithmetic / logic** (`Rn, Rm`, `Rn, #imm8`, `ERn, ERm`; `ADD`/`MOV` also `ERn, #imm7`):
  `ADD ADDC AND CMP CMPC MOV OR XOR SUB SUBC`.
- **Special:** `MUL ERn, Rm`, `DIV ERn, Rm`, `EXTBW ERn`, `NEG|DAA|DAS Rn`,
  `SB|RB|TB Rn.b | label.b` (bit set/clear/test).
- **Shifts:** `SLL SLLC SRL SRLC SRA Rn, #imm3 | Rn, Rm`.
- **Stack:** `PUSH|POP` of `Rn`, `ERn`, or a list (`EA`, `LR`, `PSW`/`EPSW`, `PC`/`ELR`).
- **Special registers:** `MOV PSW, #imm`, `MOV Rn, PSW`, `MOV ERn, SP`, `MOV SP, ERn`,
  `ADD SP, #imm`.
- **`DSRI #imm8`** — Wiz-internal prefix: sets the data segment (DSR) for the **next** memory
  access only. Used by `cwapp` for ROM tables that live in segment 3.

Both cores share the same opcode encoder (`compiler/src/asm.cpp`, sourced from the two official
manuals) and produce byte-identical ROMs; the core choice only affects the label, the
conditional-branch cycle model and error reporting.

---

## The simulator

`compiler/src/sim.cpp` decodes raw machine code with its **own** decoder (it shares no tables with
the assembler), so an assembler, compiler or runtime mistake shows up as a simulator fault instead
of being masked. It covers segment 0 (plus segment-addressed ROM for `cwapp`) and models the LCD
planes.

```cpp
#include "sim.hpp"
wiz::U8 m(rom, {{0x80, 0x40}} /* keys */, "cwii");
std::string why = m.run(5'000'000);
printf("%s\n", why.c_str());
printf("%s\n", m.ascii().c_str());   // ' .+#' per pixel
```

`run()` returns a reason string:

- `max steps` — the step budget ran out.
- `halt: B to self at XXXX` — the program reached its halt loop.
- `stop at s:XXXX` — a `stop` address was hit (used for `cwapp`).
- `BRK at XXXX`, `unknown opcode …`, `write to ROM at …`, `write to 0F037H …` — faults.

`wizrun` wraps this for command-line use; see [RUNNING.md](RUNNING.md#the-simulator-wizrun).
