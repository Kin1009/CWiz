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

Upstream `BUILTINS` (21 names at `pkdev-en/Wiz@3a4308a`) vs the two C++ front ends:

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

Upstream has 51 `PRELUDE` keys at `3a4308a` (14 in the base dict literal plus
37 later assignments).  C++ Wiz keeps the same source-PRELUDE model; CWiz has
a parallel C-syntax prelude.

| Upstream prelude | C++ Wiz | C++ CWiz | Notes |
|---|---|---|---|
| `abs`, `min`, `max`, `clamp`, `sign`, `rand`/`srand` | Ported | Ported | |
| `wait_key`, `hline`, `vline`, `rect`, `fillrect`, `line`, `circle`, `disc` | Ported | Ported | CWiz adds clipping |
| `font`, `char`, `text`, `num`, `sprite` | Ported | Ported (`glyph`/`glyph_cw`) | `glyph()` is the CWiz spelling of upstream `char()`; `char` is a keyword in C |
| `normalizeText`, Vietnamese F4 glyphs | Extended | Extended | CW Display 11x13 font |
| `strlen`, `strcmp`, `strcpy`, `atoi`, `itoa` | **Newly ported** | **Newly ported** | pure source, tested |
| `getpixel`, `clear_rect`, `invert_rect`, `copy_rect` | Ported (prelude) | Ported (prelude) | replaces the native asm versions |
| `ellipse`, `fillellipse`, `tri`, `filltri`, `roundrect` | Ported | **Ported** | `prelude_shapes.hpp`; all target screen pixels directly |
| `elapsed`, `rtc`, `set_time`, `set_date`, `_weekday`, `rtc_ctl`, `drawtime` | Ported | **Ported** | `prelude_heap_rtc.hpp` over peek/poke RTC registers; CWiz names the display helpers `rtc_read`/`draw_time`/`draw_date` |
| `getc`, `input_num`, `clip`, `sprite_rot`, `getc_tap`, `input_tap`, `input_pad` | Ported | **Ported** | `prelude_input.hpp` + `prelude_shapes.hpp`; `clip` drives the clip globals used by the clip-aware CWiz `hline`/`vline` |
| `word`, `alloc`, `free`, `file` | Ported | **Ported** | `prelude_heap_rtc.hpp` over the runtime heap (E818..EF00); `_rdw`/`_wrw` are the word helpers |
| `charz`, `textz`, `numz`, `draw` | Ported | **Ported** | `prelude_shapes.hpp`; `draw` decodes a ROM8 hex string |

The CWiz port keeps upstream's `sprite_rot` helper under that name (the existing CWiz `sprite()`
is not redefined) and adds the bounded `waitkey(ms)` alongside the blocking `wait_key()`/`getc()`.
`rtc_alive()` and `waitkey(ms)` need a running timer, so they are compile/link-tested rather than
simulated; all other ported helpers are exercised with deterministic simulator checks in
`tests/test_prelude_ports.cpp`.

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
For `cwapp`, C++ mirrors upstream's `0xE820/0xEE00` window and additionally
skips the fx-580VN X firmware ranges/immutable addresses documented in
[`CWAPP.md`](CWAPP.md) — see `cwiz_os/docs/cwizapp-ram-exclusions.md`.

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
| no hard ROM-size cap (upstream removed `ROM_LIMIT` and the overflow note in `4b1e291`/`3a4308a`) | `ROM [[at]]` is bounded only by the 16-bit address space; `note:` diagnostics and `->`/`OK` progress lines |

## 9. Assembler / opcodes

Both implementations encode the same 170-pattern nX-U8/100 and nX-U16/100
Chapter-4 table (upstream verifies it with `tools/check_core_tables.py`; the C++
tests assemble representative encodings and compare byte-for-byte).  The C++
assembler adds the `JMP`/`Jcc` pseudo-instructions described in
`docs/ARCHITECTURE.md`, and the C++ simulator has an independent decoder.
No upstream opcode is missing.

## 10. Priority list for porting

The high-value/high-risk items from the previous revision are now done:

1. ~~`ticks()` / `delay_ms()`~~ — ported as runtime built-ins; `elapsed()` is
   the prelude wrapper.  The simulator's timer is static, so timer-dependent
   helpers (`rtc_alive`, `waitkey`) are compile/link-tested only.
2. ~~Pure-source graphics prelude~~ — `ellipse`, `fillellipse`, `tri`,
   `filltri`, `roundrect`, `clip` and the clip-aware `hline`/`vline` are in
   `prelude_shapes.hpp`.
3. ~~Scaled text / hex bitmaps~~ — `charz`, `textz`, `numz`, `draw`, `sprite_rot`
   and `_hexv` are in `prelude_shapes.hpp`.
4. ~~RTC prelude~~ — all getters, setters, `rtc_read`/`rtc_alive`,
   `draw_time`/`draw_date` and `_rtc[7]` are in `prelude_heap_rtc.hpp`.
5. ~~Heap / RAM file system~~ — `_rdw`/`_wrw`, `alloc`/`free`/`_merge` and
   `f_save`/`f_load`/`f_exists`/`f_remove` are in `prelude_heap_rtc.hpp`.
6. ~~Interactive input~~ — `waitkey`, `getc`, `getc_tap`, `input_num`,
   `input_tap` and `input_pad` are in `prelude_input.hpp`.

Remaining work:

1. **`wizimport.py` in C++** — still the main missing upstream feature
   (multi-file Wiz projects).  A source-to-source pass with line preservation;
   failure modes are easy to test with temporary files.
2. **Hardware-timed behaviour in the simulator** — model TM0 so that
   `rtc_alive()` and `waitkey(ms)` can be simulated rather than only compiled.

Deliberately not ported: `#org` for non-ROP builds (the patch slot chooses the
address), and upstream's native pixel/rect/string assembly (the source
versions are easier to maintain and match the shadow-buffer model).
