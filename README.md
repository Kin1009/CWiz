# Wiz

**Wiz** is a tiny, Python-flavoured language and toolchain for the Casio **ClassWiz** family of
calculators (ML620909 / ML620906, and the fx-580VN X firmware). You write short programs in a
Python-like syntax, and Wiz compiles them straight to the machine code of the calculator's
OKI/LAPIS **nX-U8/100 / nX-U16/100** CPU, with drawing, key input and timing built in.

The toolchain also ships **CWiz**, a second and fully independent language with C-like syntax. It
targets the same machines and shares only the machine-level assembler, the boot templates and the
hand-written runtime.

Everything is **native C++17** — there is no Python anywhere and no third-party dependency beyond
CMake and a C++17 compiler.

```python
# hello.wz
def main():
    clear()
    text(8, 6, "Hello, Wiz!")
    wait_key()
```

```c
// hello.cwiz — the same program in CWiz
void main() {
    clear();
    text(8, 6, "Hello, CWiz!");
}
```

```console
$ ./build.sh                              # configure + build the toolchain into ./build
$ build/wizc examples/01_hello.wz         # -> examples/01_hello.bin (+ .asm listing)
$ build/wizrun examples/01_hello.wz       # build, run in the simulator, print the LCD
```

---

## Why it exists

Writing for these calculators normally means hand-writing assembly or fighting a large C SDK.
Wiz is an experiment in the opposite direction: two compact compilers, a hand-written assembler, a
hand-written runtime of a few hundred instructions, and a simulator to test it all without a real
device. The whole thing is dependency-free C++17 (plus one hand-written `.asm` runtime).

Everything is a **signed 16-bit integer**. There are no floats, no garbage collector, no heap —
just a fixed block of RAM for variables, a stack for function calls, and the LCD.

## Quick start

Requires CMake 3.16+ and a C++17 compiler. No third-party packages.

```console
# build everything into ./build
./build.sh

# build an example to a ROM image (default target: 880)
build/wizc examples/01_hello.wz

# pick a machine explicitly
build/wizc --target 580 game.wz

# build a CWiz program (the language is chosen by the .cwiz extension)
build/wizc examples/cwiz/02_shapes.cwiz

# run a program in the simulator and print the screen
build/wizrun examples/03_keys.wz --keys K_RIGHT --steps 3000000

# drive the simulator with timed key presses
build/wizrun --press K_OK@2000000,K_UP@3000000 examples/cwiz/06_demo.cwiz

# run the toolchain test suite
build/wiztests
```

See [docs/RUNNING.md](docs/RUNNING.md) for the full workflow, including the emulator.

## Documentation

| Document | What it covers |
|----------|----------------|
| [docs/GUIDE.md](docs/GUIDE.md) | Learn Wiz (and meet CWiz): syntax, scoping, screen, input, arrays, targets, recursion. Task-oriented. |
| [docs/API.md](docs/API.md) | Reference: built-ins, standard library, `print`, key constants, memory map, targets, the C++ APIs, the runtime ABI. |
| [docs/RUNNING.md](docs/RUNNING.md) | Building, simulating, running under the emulator, testing, patching into a firmware, troubleshooting. |
| [docs/ROP.md](docs/ROP.md) | ROP payloads: the chain encoding, the nX-U16 register layout, the gadget ABIs and the launchers. |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Internals: compiler pipeline, calling convention, runtime, ISA/assembler, boot templates. |
| [examples/README.md](examples/README.md) | Annotated tour of every example program, Wiz and CWiz. |

## Two languages, one toolchain

| Language | Extension | Front end | Syntax | Notes |
|----------|-----------|-----------|--------|-------|
| **Wiz**  | `.wz`   | `compiler/src/wiz.cpp`  | indentation, Python-flavoured | the original language |
| **CWiz** | `.cwiz` | `compiler/src/cwiz.cpp` | C-like (`{}`, `;`, `int`) | independent compiler, same feature set |

Both are 16-bit, share the same built-ins (`pixel`, `clear`, `key`, …), the same graphics standard
library (`hline`, `fillrect`, `circle`, …) and the same targets. The two front ends share nothing
above the assembly level.

CWiz has no structs, but it does have pointers: `int*` / `char*`, `&x`, `*p`, `p[i]` and pointer
arithmetic, all lowered to plain loads and stores (RAM only — see
[docs/GUIDE.md §20.2](docs/GUIDE.md#202-pointers-and-memory)). A few names differ because C reserves
them: CWiz's single-character drawing helper is called `glyph` (Wiz calls it `char`), and `print(...)`
is a compiler special form in both languages. CWiz sources also run through a small C-style
preprocessor (`#define`, `#if`, `#include`, `#for`, …) before parsing; Wiz has no directives because
`#` starts a comment there. See [docs/GUIDE.md](docs/GUIDE.md#20-cwiz-in-c-syntax).

## Targets

A single source file can carry code for more than one machine using `<target>` sections: `for 580` /
`for 880` in Wiz, or the same headers written as directives (`#for 580`, `#cwapp`) in CWiz.

| Target | Chip | CPU core | ROM | Variables | LCD | Notes |
|--------|------|----------|-----|-----------|-----|-------|
| `880`  | ML620909 | nX-U8/100  | 0x0000–0x8FFF | 0x9000–0xDFFF (20 KB) | 192×64, 2 planes | ClassWiz II. **Default target.** |
| `580`  | ML620906 | nX-U16/100 | 0x0000–0xCFFF | 0xD000–0xDFFF (4 KB)  | 192×63, 1 plane  | ClassWiz. Row 0 of VRAM is not shown. |
| `cwapp`| ML620906 | nX-U16/100 | (in a firmware) | 0xD000–0xDFFF | 192×63, 1 plane | Program patched into an fx-580VN X ROM; `exit()` returns to the menu. |

`880` and `580` are the user-facing target names; internally they are the boards `cwii` and `cw`
(both spellings are accepted by `--target` and by `for …` section headers). Chip names such as
`ML620909` are **not** accepted. The default target is `880`, unless the `WIZ_TARGET` environment
variable overrides it (with an internal name, e.g. `WIZ_TARGET=cw`).

## ROP payloads

A program that starts with `#rop` is not compiled into a ROM image: it is translated into an
**nX-U16 ROP gadget chain** — a sequence of `call` / `pop` / `flow` words that the calculator
executes straight from RAM.

```c
#rop
#for 580
#org E9E0          // payload address (E9E0 on the 580, E330 on the 880)
#launcher common   // the community "launcher chung"

int main() {
    setlr();
    setsfr();
    screen_del();
    print(0x30, 0x01, "HELLO ROP");
    render();
}
```

The compiler emits the chain, the launcher that starts it, and a low-level listing that shows
every gadget word, its class and its inline data. The payload is injected at `#org`; the launcher
goes into the calculator's program input area (`D180` on the 580, `9268` on the 880) and is
started with **LEFT** then **EXE** in Program mode.

The exact ABI — chain word encoding, register file, `printline` / `smallprint` / `line_draw` /
`render_bitmap` conventions and the launcher bytes — is documented in
[docs/ROP.md](docs/ROP.md), with the community reference material in `docs/rop-notes/`.

## Project layout

```
build.sh              one command: configure + build everything into ./build
compiler/
  CMakeLists.txt      the toolchain build (wizcore + the four tools + wiztests)
  runtime.asm         hand-written runtime (shared by Wiz + CWiz)
  template_ML620906.asm  boot template, 580
  template_ML620909.asm  boot template, 880
  template_cwapp.asm  boot template, fx-580VN X firmware patch
  src/                the C++17 sources
    common.hpp/.cpp   string/file helpers, fmt()
    diag.hpp/.cpp     coloured diagnostics, gcc-like
    targets.hpp/.cpp  target table: 580/cw, 880/cwii, cwapp
    asm.hpp/.cpp      the nX-U8/nX-U16 assembler
    sim.hpp/.cpp      the U8 simulator (nX-U8 + nX-U16)
    font6x8.hpp       generated 6x8 font table
    wiz.hpp/.cpp      the Wiz front end + codegen
    cwiz.hpp/.cpp     the CWiz front end + codegen (independent)
    build.hpp/.cpp    shared pipeline: boot + body + runtime -> ROM
    cli.hpp           small argument parser + data-dir autodetect
    modelinfo.hpp     CasioEmuMsvc config.bin reader
    version.hpp       WIZ_VERSION
    main_*.cpp        drivers: wizc, wizrun, customromrun, patch_menu
examples/             *.wz (Wiz) and cwiz/*.cwiz (CWiz) programs
tests/                the C++ test suite (built as wiztests)
emulator/             vendored CasioEmuMsvc (macOS-only), models/; it embeds this
                      toolchain in its debugger (emulator/docs/WIZ-TOOLCHAIN.md)
docs/                 this documentation
```

## Status & honesty

This is a hobby/experimental toolchain. In particular:

- Programs are tested on the bundled simulator and, optionally, in the empirical CasioEmuMsvc
  emulator. They have **not** been run on a physical calculator.
- The key scan-code tables and the LCD memory map are lifted from existing references and, as the
  source comments say, are **not all verified on real hardware**.
- `cwapp` (patching into an fx-580VN X ROM) is written from disassembly and is **not tested on a
  real firmware**; try it in an emulator first.
- Emitting a `.bin` is fully tested in the simulator; flashing it to a device is chip- and
  board-specific and out of scope here (see [docs/RUNNING.md](docs/RUNNING.md)).

## Contact

- 🐛 **Issues**: [github.com/Kin1009/CWiz/issues](https://github.com/Kin1009/CWiz/issues)
- 📧 **Email**: [khoi.nguyenminh100912@gmail.com](mailto:khoi.nguyenminh100912@gmail.com)
- 🖥️ **Emulator with this toolchain built in**: [github.com/Kin1009/CasioEmuMsvc-Mac](https://github.com/Kin1009/CasioEmuMsvc-Mac)

## License / provenance

Several pieces are derived from other projects and manuals; the specific sources are credited in
the file headers (LAPIS/OKI instruction manuals, `libcw`, the ML620906 DCL, etc.). Check those
before redistributing.
