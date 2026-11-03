# Wiz upstream vs the C++ port

Upstream studied: <https://github.com/pkdev-en/Wiz> (`Wiz-main`, the Python
reference implementation).  The C++ port in this repository keeps the same
targets, assembler encoding and runtime contract, adds a second front end
(CWiz), a native simulator, a linter and an fx-580VN X firmware patcher.

Legend: **Ported** = behaviour is present and tested; **Partial** = present
with a documented difference; **Missing** = not in the C++ port; **Extended** =
the C++ port does more than upstream.

## 1. Source layout

| Upstream file | C++ port | Status |
|---|---|---|
| `wizc.py` (lexer/parser/codegen + prelude) | `compiler/src/wiz.cpp` | Ported |
| — (CWiz did not exist upstream) | `compiler/src/cwiz.cpp` + `cpre.cpp` | Extended |
| `u8asm.py` | `compiler/src/asm.cpp` | Ported |
| `targets.py` | `compiler/src/targets.cpp` | Ported + `fwnx` |
| `build.py` | `compiler/src/build.cpp`, `main_wizc.cpp` | Extended |
| `runtime.asm` | `compiler/runtime.asm` | Partial (see §5) |
| `template_*.asm` | `compiler/template_*.asm` | Ported + `template_fwnx.asm` |
| `font6x8.py` | `compiler/src/font6x8.hpp` | Ported |
| `patch_menu.py` | `compiler/src/patch.cpp` | Ported |
| `wizimport.py` | `Wizimport.py` (Python, unused by `wizc`) | Missing in C++ |
| `tests/test_*.py` | `tests/test_*.cpp` | Ported (except `test_fontsize`) |
| `fuzz.py` | `tests/test_fuzz.cpp` | Ported |
| — | `sim.cpp`, `lint.cpp`, `rop.cpp`, `diag.cpp` | Extended |

## 2. Language features

| Feature | Upstream Wiz | C++ Wiz | C++ CWiz |
|---|---|---|---|
| indentation blocks, `def`, `if/elif/else`, `while`, `for ... range` | yes | Ported | n/a |
| `return`, `break`, `continue`, `pass`, `global` | yes | Ported | `break`/`continue` only (C has no `pass`/`global`) |
| `and`/`or`/`not`, chained comparisons rejected | yes | Ported | C operators `&&`/`\|\|`/`!` |
| `const`, `rom`, `rom8`, `[v] * N` tables | yes | Ported | `const`, `rom`, `rom8`, C initializer lists |
| string literals, `_rpeek`, `peek`/`poke` | yes | Ported | Ported |
| `import` / `from x import y` | yes (`wizimport.py`) | **Missing** | n/a |
| C subset (`int`/`char`/`void`/`bool`, pointers, arrays) | — | — | Extended |
| `switch`/`case`/`default` | — | — | **Newly ported** (fallthrough, `break`, duplicate-case checks) |
| `sizeof` | — | — | **Newly ported** (type, expression, whole array) |
| `goto` | — | — | Rejected with an actionable error |
| `do ... while`, `?:`, `++`/`--`, compound assignment | — | — | Ported |
| recursion | detection + save/restore notes | Ported | Ported (static frames) |

## 3. Built-ins

Upstream `BUILTINS` (20 names) vs the two C++ front ends:

| Upstream builtin | Arity | C++ Wiz | C++ CWiz | Notes |
|---|---|---|---|---|
| `pixel` | 2–3 | Ported | Ported | CWiz checks the argument signature/type |
| `clear` | 0 | Ported | Ported | |
| `key`, `delay`, `peek`, `poke`, `_hspan` | — | Ported | Ported | |
| `key_down`, `key_hit`, `buffered`, `flip` | — | Ported | Ported | |
| `_rpeek`, `exit` | — | Ported | Ported | `exit` is cwapp-only in CWiz |
| `ticks` | 0 | Ported | Ported | Timer0 assembly (`__ticks`) |
| `delay_ms` | 1 | Ported | Ported | Timer0 assembly (`__delay_ms`) |
| `getpixel` | 2 | Ported as prelude | Ported as prelude | reads the shadow buffer directly |
| `copy_rect` | 6 | Ported as prelude | Ported as prelude | forward copy; overlap order is the caller's job |
| `invert_rect` | 4 | Ported as prelude | Ported as prelude | |
| `clear_rect` | 4 | Ported as prelude | Ported as prelude | |
| `_hpeek`, `_hpoke` | 1 / 2 | Ported | Ported | runtime byte access |
| — | — | `render`, `renderStatus`, `clearRender` | same + `wipe`, `recover`, `reset` | C++ extensions |

## 4. Standard-library prelude

Upstream has 37 `PRELUDE` keys.  C++ Wiz keeps the same source-PRELUDE model;
CWiz has a parallel C-syntax prelude.

| Upstream prelude | C++ Wiz | C++ CWiz | Notes |
|---|---|---|---|
| `abs`, `min`, `max`, `clamp`, `sign`, `rand`/`srand` | Ported | Ported | |
| `wait_key`, `hline`, `vline`, `rect`, `fillrect`, `line`, `circle`, `disc` | Ported | Ported | CWiz adds clipping |
| `font`, `char`, `text`, `num`, `sprite` | Ported | Ported (`glyph`/`glyph_cw`) | `char` is a keyword in C |
| `normalizeText`, Vietnamese F4 glyphs | Extended | Extended | CW Display 11x13 font |
| `strlen`, `strcmp`, `strcpy`, `atoi`, `itoa` | **Newly ported** | **Newly ported** | pure source, tested |
| `getpixel`, `clear_rect`, `invert_rect`, `copy_rect` | Ported (prelude) | Ported (prelude) | replaces the native asm versions |
| `ellipse`, `fillellipse`, `tri`, `filltri`, `roundrect` | Ported | **Missing** | Wiz prelude; CWiz has the string/rect subset |
| `elapsed`, `rtc`, `set_time`, `set_date`, `_weekday`, `rtc_ctl`, `drawtime` | Ported | **Missing** | Wiz prelude over peek/poke RTC registers |
| `getc`, `input_num`, `clip`, `sprite_rot`, `getc_tap`, `input_tap`, `input_pad` | Ported | **Missing** | Wiz prelude; `clip` drives the clip globals used by hline/vline |
| `word`, `alloc`, `free`, `file` | Ported | **Missing** | Wiz prelude over the runtime heap (E818..EF00) |
| `charz`, `textz`, `numz`, `draw` | Ported | **Missing** | Wiz prelude |

## 5. Runtime routines

| Upstream `__name` | C++ runtime | Status |
|---|---|---|
| `__mul16`, `__div16`, `__shl16`, `__sra16` | yes | Ported |
| `__pixel`, `__clear`, `__buffered`, `__flip` | yes | Ported (shadow-buffer model) |
| `__rt_init`, `__key`, `__key_down`, `__key_hit` | yes | Ported |
| `__delay`, `__peek`, `__rpeek`, `__poke`, `__hspan` | yes | Ported |
| `__getpixel` | prelude source | Partial |
| `__ticks`, `__delay_ms`, `__tm0_*`, `__delay_chunk*` | yes | Ported |
| `__copy_rect`, `__invert_rect`, `__clear_rect` | prelude source | Partial |
| `__hpeek`, `__hpoke` | yes | Ported (byte access; `getc` is prelude) |
| `__strlen`, `__strcmp`, `__strcpy`, `__atoi` | prelude source | Partial |
| — | `__render`, `__renderStatus`, `__clearRender`, `__wipe`, `__reset`, `__recover` | C++ extensions |

The C++ runtime draws into the SB2 shadow buffer and flushes with `render()`;
the upstream routines that read and copy pixels assume its own buffering
scheme, which is why the pixel/rect helpers were ported as source instead of
copied byte-for-byte.

## 6. Targets

| Target | Upstream | C++ port | Status |
|---|---|---|---|
| `cwii` / `880` (ML620909, 192x64, 2 planes) | yes | yes | Ported |
| `cw` / `580` (ML620906, 192x63, 1 plane, row offset 1) | yes | yes | Ported |
| `cwapp` (fx-580VN X firmware patch, segment 3) | yes | yes | Ported + slot patcher |
| `fwnx` (full firmware image, `#binary`) | — | yes | Extended |

RAM limits differ by design: the C++ allocator stops before the SB2 shadow
buffer (`var_limit` 0xCA54 on cwii, 0xDDD3 on cw), not at upstream's 0xE000.

## 7. Directives and sections

| Feature | Upstream | C++ Wiz | C++ CWiz / ROP |
|---|---|---|---|
| `for cw`, `for cwii`, `for all` sections | yes | Ported | n/a |
| `---` / `===` section separators | yes | Ported | n/a |
| `#` comments in Wiz | yes | Ported | `//` and `/* */` in CWiz |
| `#define`, `#undef`, `#if/#ifdef/#ifndef/#elif/#else/#endif` | — | — | Extended |
| `#include`, `#pragma once`, `#error` | — | — | Extended |
| `#for`, `#cwapp`, `#slot` | — | — | Extended |
| `#org`, `#binary` | — | — | Extended (see note) |
| `#rop`, `#launcher`, `#pad`, `#nopad` | — | — | Extended (ROP backend) |

Note: `#org` is only meaningful to the ROP backend; the `cwapp` example that
uses it is currently rejected by the CWiz front end (pre-existing).

## 8. CLI behaviour

| Upstream `build.py` | C++ `wizc` |
|---|---|
| `build.py prog.wz [out.bin]` | `wizc [options] file...` |
| `--target cwii\|cw\|all` (default cwii) | `--target 580\|880` / `--cwapp` / default from source or cwii |
| always writes `.bin` + `.asm` | `.bin` + `.asm`; `-S` assembles only; `--no-asm` skips listing |
| one language (Wiz) | `--lang wiz\|cwiz`, chosen by extension by default |
| — | `-E/--pp`, `--color[=WHEN]`, `-v/--version`, `-h/--help` |
| `error [target]:` + source line | gcc-style `file:line:col: error: ... [code]` + `help:` |
| exit 2 on failure | exit 1 on failure |
| `note` for ROM overflow / compiler notes | `note:` diagnostics and `->`/`OK` progress lines |

## 9. Assembler / opcodes

Both implementations encode the same 170-pattern nX-U8/100 and nX-U16/100
Chapter-4 table (upstream verifies it with `tools/check_core_tables.py`; the C++
tests assemble representative encodings and compare byte-for-byte).  The C++
assembler adds the `JMP`/`Jcc` pseudo-instructions described in
`docs/ARCHITECTURE.md`, and the C++ simulator has an independent decoder.
No upstream opcode is missing.

## 10. Priority list for porting

Ordered by value / risk.  "Low risk" means pure source over already-ported
built-ins or a self-contained assembler routine.

1. **`ticks()` / `delay_ms()`** — high value (timing, `elapsed`, input
   timeouts).  Port `__tm0_start`, `__tm0_read`, `__ticks`, `__delay_ms` from
   upstream, add both built-ins to Wiz/CWiz, test with the simulator's timer
   model (or only the `n <= 0` path if the simulator has no TM0).
2. **`wizimport.py` in C++** — high value for multi-file Wiz projects.  It is
   a source-to-source pass with line preservation; failure modes are easy to
   test with temporary files.
3. **Pure-source graphics prelude** — `ellipse`, `fillellipse`, `tri`,
   `filltri`, `roundrect`.  Low risk, no runtime changes.
4. **`clip()` + clip-aware `hline`/`vline`** — medium risk: needs the four
   runtime state bytes upstream reserves at 0xE801–0xE804 and `__rt_init`
   initialisation.
5. **`charz` / `textz` / `numz` / `draw`** — low risk once the string helpers
   (now ported) exist.
6. **RTC prelude** (`rtc`, `set_time`, `set_date`, `_weekday`, `rtc_ctl`,
   `drawtime`, `elapsed`) — medium/high risk: hardware registers and
   simulator support.
7. **Heap / RAM file system** (`word`, `alloc`, `free`, `file`, `_hpeek`,
   `_hpoke`) — high risk: needs a heap model, affects RAM limits.
8. **Interactive input** (`getc`, `input_num`, `getc_tap`, `input_tap`,
   `input_pad`, `sprite_rot`) — high risk: depends on clip, timing and heap
   pieces above.

Deliberately not ported: `#org` for non-ROP builds (the patch slot chooses the
address), and upstream's native pixel/rect/string assembly (the source
versions are easier to maintain and match the shadow-buffer model).
