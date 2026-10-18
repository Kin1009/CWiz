# Example programs

Every example is a single source file. Wiz examples live directly in this directory (`*.wz`);
CWiz examples live in [`cwiz/`](cwiz) (`*.cwiz`). Build one with `wizc`, or run it in the simulator
and look at the screen with `wizrun`:

```console
$ build/wizc examples/01_hello.wz               # -> examples/01_hello.bin (+ .asm)
$ build/wizrun examples/02_shapes.wz --steps 4000000

$ build/wizc examples/cwiz/02_shapes.cwiz        # the CWiz front end is picked by .cwiz
$ build/wizrun examples/cwiz/01_hello.cwiz
```

Most Wiz examples are written to be target-independent and build unchanged for either machine
(`--target 580` or `--target 880`). `10_app_hello.wz` is `cwapp`-only;
`cwiz/10_app_hello.cwiz` uses `#for 580` / `#cwapp` sections to build for both.

## Wiz

| File | What it shows |
|------|---------------|
| `01_hello.wz` | The minimum: `clear`, `text`, `wait_key`. |
| `02_shapes.wz` | Every drawing primitive: `rect`, `fillrect`, `line`, `circle`, `disc`, `hline`, `vline`, `text`, `num`. |
| `03_keys.wz` | Input: move a box with the arrow keys, echo `key()`; double buffering with `buffered`/`flip`. |
| `04_arrays.wz` | A RAM array (`[0] * 6`), indexed reads/writes in loops, a `rom8` string, and `print`. |
| `05_pong.wz` | A complete two-paddle Pong: `fillrect` gameplay, an opponent that tracks the ball, a score strip, a win screen. Read this to see a real game loop. |
| `06_demo.wz` | Recursion (`fib`), a loop (`fact`), `print` layout, and `key_hit` edge detection. |
| `07_catch.wz` | A reflex game using `rand`, `key_down`, `rect`/`fillrect`, and a status line. (Comments are in Vietnamese.) |
| `10_app_hello.wz` | A `cwapp` program: a `for cwapp` section with `exit()`, patchable into an fx-580VN X menu. It declares only the `cwapp` target, so build it with `--cwapp`. |

## CWiz

The same tour in C syntax, sharing the built-ins and the graphics library:

| File | What it shows |
|------|---------------|
| `01_hello.cwiz` | `clear`, `text`, `num`. |
| `02_shapes.cwiz` | The drawing library plus a `rom8` sprite and `sprite()`. |
| `03_keys.cwiz` | `key_down`/`key_hit`, the `K_*` constants and a box you can move. |
| `04_arrays.cwiz` | RAM arrays, a `rom int` table and a `rom8 char` string (see [GUIDE §20](../docs/GUIDE.md#20-cwiz-in-c-syntax)). |
| `05_pong.cwiz` | A one-player Pong game loop with `fillrect`, `hline`, `num` and `key_down`. |
| `06_demo.cwiz` | Recursion, functions with default arguments, `print`, and `abs` from the standard library. |
| `07_preproc.cwiz` | The preprocessor: `#define` (object- and function-like), `#if`/`#ifdef`, `#include`, `__ROWS__`, and target sections written with the `#for` directive. It includes `07_hud.cwiz`, which is not a program but the shared HUD header. |
| `08_pointers.cwiz` | Addresses and memory: `int*`/`char*`, `&`, `*p`, `p[i]`, pointer arithmetic, and a `char` string buffer (see [GUIDE §20.2](../docs/GUIDE.md#202-pointers-and-memory)). |
| `10_app_hello.cwiz` | The CWiz twin of `10_app_hello.wz`: an fx-580VN X firmware app. `#for 580` / `#cwapp` sections pick `leave()` per target, so the same file builds as a plain 580 ROM or as the app (`--cwapp`). |

## A good reading order

1. `01_hello` → `02_shapes` → `03_keys` to learn the basics interactively.
2. `04_arrays` once you need data.
3. `05_pong` as a template for a full game (double buffering, input, collision, scoring).
4. `10_app_hello` if you want to run something inside the stock firmware.
5. Then read `examples/cwiz/` to see the same ideas in CWiz:
   `07_preproc.cwiz` for the preprocessor and `08_pointers.cwiz` for memory.

The comments at the top of each file state what it demonstrates; the code itself is commented
inline. For the language details behind any line, see [../docs/GUIDE.md](../docs/GUIDE.md).
