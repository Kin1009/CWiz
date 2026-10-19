# Building, running and testing Wiz programs

This document covers the day-to-day workflow: building the toolchain, compiling `.wz` / `.cwiz`
sources, running them in the simulator or under the emulator, testing, and getting a program into
an fx-580VN X firmware.

---

## Requirements

- **CMake 3.16+** and a **C++17 compiler** (clang or gcc). No third-party packages.
- For assembling raw `.asm` by hand and hardware work, familiarity with the nX-U8/100
  instruction set helps but is not required to write Wiz or CWiz.
- The CasioEmuMsvc emulator is **macOS-only** (see [below](#the-emulator-casioemumsvc)) and is
  optional.

The toolchain is built once; after that you run the tools from `./build`.

---

## Building the toolchain

```console
$ ./build.sh                 # release build into ./build
$ ./build.sh --debug         # debug build
$ ./build.sh --clean         # wipe ./build first
```

`build.sh` configures with CMake (`-S compiler -B build`, using Ninja when available) and builds
everything. The tools land in `./build`:

```
build/wizc           the Wiz / CWiz compiler driver
build/wizrun         build + run a program in the simulator
build/customromrun   run a ROM image on a CasioEmuMsvc model
build/patch_menu     patch a program into an fx-580VN X firmware dump
build/wiztests       the toolchain test suite
```

You can also drive CMake directly:

```console
$ cmake -S compiler -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build --parallel
```

---

## Compiling (`wizc`)

```
build/wizc [options] file...
```

The front end is chosen by extension: `.cwiz` is CWiz, anything else is Wiz (`--lang wiz|cwiz`
overrides). `wizc` writes a ROM image and, unless `--no-asm`, an assembly listing next to the
source:

```console
$ build/wizc examples/01_hello.wz
CC examples/01_hello.wz -> examples/01_hello.bin
    [880 ML620909 nX-U8/100] 14 variables, 2564 bytes ROM
```

- `examples/01_hello.bin` — the ROM image.
- `examples/01_hello.asm` — the full assembly listing (boot template + your code + runtime),
  useful for debugging.

The detail line reports the target, the number of **variables** (2 bytes each) and the number of
**bytes of ROM**. If a function is recursive, an extra `note:` line estimates stack use per call.

### Options

| Option | Meaning |
|--------|---------|
| `-o FILE` | write the ROM image to `FILE` (the `.asm` listing is written next to it) |
| `--target 580\|880` | target calculator (default: `880`, or `WIZ_TARGET`; a `#cwapp` source implies 580) |
| `--cwapp` | accepted for old scripts; no longer needed (a `#cwapp` section patches automatically) |
| `--lang wiz\|cwiz` | force the input language (default: by extension) |
| `-S` | stop after assembly: write the `.asm` listing, no `.bin` |
| `-E`, `--pp` | preprocess only: expand directives (CWiz) or pick the target's sections (Wiz) and print the result to stdout, like `gcc -E` |
| `--no-asm` | do not write the `.asm` listing |
| `--color[=WHEN]` | colour diagnostics: `auto` \| `always` \| `never` (default `auto`) |
| `-v`, `--verbose` | print extra detail |
| `--version` | print the version (`2.0.0`) and exit |
| `-h`, `--help` | show help |

`wizc` accepts several source files in one invocation; `-o` is only valid with a single file.

### Seeing what the preprocessor did (`-E`)

CWiz sources are run through the preprocessor first: `#define`, `#if`, `#include`, `#for` and the
rest of the directives in [GUIDE §20.1](GUIDE.md#201-preprocessor-directives) are expanded before
the target sections are picked. `-E` (or `--pp`) stops right after that step and prints the source
that the parser would see, like `gcc -E`:

```console
$ build/wizc -E examples/cwiz/07_preproc.cwiz | head
```

The output is line-preserving — a directive line becomes an empty line — so diagnostics still point
at the line you wrote, including through `#include`d files. For a Wiz source (no directives),
`-E` prints the file with just the target's `for <target>` sections selected.

### Multi-target builds

A source file can declare `for <target>` sections (see
[GUIDE §17](GUIDE.md#17-one-file-several-machines)). `wizc` builds **one target per invocation**:

| Command | Builds | Output |
|---------|--------|--------|
| `wizc plain.wz` | the default target (`880` / `WIZ_TARGET`), since it declares no sections | `plain.bin` |
| `wizc multi.wz` | the file's only declared target, else its first declared target, else the default | `multi.bin` |
| `wizc --target 580 plain.wz` | `580` | `plain.bin` |
| `wizc --target 880 -o plain.880.bin plain.wz` | `880` | `plain.880.bin` |

To build every section of a multi-target file, run `wizc` once per target with distinct `-o`
paths:

```console
$ build/wizc --target 580 -o game.580.bin game.wz
$ build/wizc --target 880 -o game.880.bin game.wz
```

Set the default target for a session with the environment variable (using an internal target name,
`cw` for `580` or `cwii` for `880`):

```console
$ WIZ_TARGET=cw build/wizc plain.wz
```

### Using the compiler as a library

The toolchain is a static library, `wizcore`. In C++:

```cpp
#include "build.hpp"
wiz::set_data_dir(wiz::autodetect_data_dir(argv[0]));
wiz::BuildResult r = wiz::build_path("prog.wz", "cwii").r;
// r.rom: the ROM bytes; r.full_asm: the assembly text;
// r.variables: {name: RAM address}; r.syms: {label: address}
```

Errors are thrown as `wiz::CompileError` (the compiler) or `wiz::AsmError` (the assembler).
`r.variables` maps each global/local name to the RAM address it was allocated — the tests use this
to read results back out of the simulator. See [API.md](API.md#c-api).

---

## The simulator (`wizrun`)

```
build/wizrun [options] file.bin [580|880]
```

`wizrun` loads a compiled image and runs it on the bundled simulator, printing the LCD as text (by
default at half resolution — pass `--full` for every pixel). A `.bin` defaults to the **580** model.
A `.cwiz`/`.wz` source is accepted too and built with the same front end as `wizc` (it keeps
`wizc`'s target rules unless you name a model); a `#cwapp` source is patched into the built-in 580
ROM and started directly.

```console
$ build/wizc examples/cwiz/10_app_hello.cwiz -o hello.bin
$ build/wizrun hello.bin 580
```

A `.bin` that already carries the cwapp hook (the last bytes at `25E1E` are `00 F3 40 0E`) is
started at its app entry; any other image runs from its reset vector.

| Option | Meaning |
|--------|---------|
| `--target 580\|880` | target calculator (default `580`) |
| `--steps N` | step budget (default 3 000 000). Games need more; try 20 000 000. |
| `--keys K,K,…` | keys held down for the whole run (`K_*` name or a number) |
| `--press K@N` | press key `K` starting at step `N` (comma-separate several) |
| `--full` | print all 192 columns |
| `--color[=WHEN]` | colour diagnostics |
| `-v`, `--version`, `-h` | verbose / version / help |

```console
$ build/wizrun examples/03_keys.wz --keys K_RIGHT --steps 3000000
              ##
              ...
ran examples/03_keys.wz  (880, stopped: max steps, 3000000 steps)
```

The last line above the screen is the stop reason and the step count — useful to tell a finished
program (`halt: B to self at XXXX`) from one that is still looping (`max steps`), and to catch
faults (`unknown opcode …`, `write to ROM at …`).

`wizrun` only uses the first file given. It runs each `--press` by advancing the simulator to
that step, then adding the key to the held set.

You can also drive the simulator directly (for scripted checks):

```cpp
#include "build.hpp"
#include "sim.hpp"
wiz::BuildResult r = wiz::build_path("prog.wz", "cwii").r;
wiz::U8 m(r.rom, {}, "cwii");
std::printf("%s\n", m.run(5'000'000).c_str());
std::printf("%s\n", m.ascii().c_str());
```

---

## The emulator (CasioEmuMsvc)

The repo vendors a macOS build of the empirical **CasioEmuMsvc** emulator under `emulator/`
(see [emulator/BUILD-MACOS.md](../emulator/BUILD-MACOS.md) for how it was built). It is
**macOS-only**.

`customromrun` prepares a copy of a model whose ROM is your image and launches the emulator:

```
build/customromrun --model 580|880 rom.bin [--models DIR] [--emu PATH] [--no-launch] [--dry-run]
```

It:

1. copies the matching base model folder (`models/fx-580VN X` for `580`, `models/fx-880BTG` for
   `880`);
2. renames the copy to the custom ROM's name (so several builds can coexist);
3. keeps the model's original ROM as `<rom>.orig` and writes your image over the model's ROM;
4. launches CasioEmuMsvc on that model.

```console
$ build/customromrun --model 580 examples/01_hello.bin
customromrun <base model dir> -> <models>/01_hello
    base model fx-580VN X | custom rom …/01_hello.bin | replaces rom.bin
    OK model prepared at …/models/01_hello
    launching: open -n …/CasioEmuMsvc.app --args …/models/01_hello
```

`--no-launch` stops after preparing the model; `--dry-run` prints the plan and changes nothing. The
models directory defaults to `<emulator-tree>/models` (override with `--models`); set
`CASIOEMU_ROOT` or `WIZ_ROOT` to point at the trees if autodetection misses them.

You can also launch the emulator by hand on any model directory:

```console
$ open -n CasioEmuMsvc.app --args /abs/path/to/model-dir
```

The emulator's startup UI asks for a ROM **password** when a model's ROM is protected; the stock
dumps use **`CASIO`**.

---

## Testing the toolchain

```console
$ build/wiztests
```

It exits non-zero if anything fails. With CMake you can also run it as a CTest case:

```console
$ ctest --test-dir build --output-on-failure
```

Because the simulator has an independent decoder, a green run is meant to exercise the compiler,
the assembler, the runtime and the simulator together.

---

## Running on hardware

`wizc` produces a **flat ROM image** beginning at address `0x0000`. How you get that image onto a
calculator depends on the device and is outside this repository:

- Some models expose a serial/USB boot or a bootloader that accepts a raw image.
- Others are commonly run under an emulator that loads the flat image at `0x0000`.
- The image is a complete boot: it starts at the reset vector, initialises the SFRs the LCD and
  keyboard need, runs your top-level code and `main()`, then sits in a tight halt loop.

Two caveats from the source, repeated here on purpose:

- Programs have been tested on the bundled simulator and (optionally) in the CasioEmuMsvc
  emulator. They have **not** been run on a physical calculator.
- The **LCD/keyboard/SFR setup** in the boot templates comes from existing references and has not
  all been verified on real silicon.
- Memory sizes (ROM/RAM) are the documented maxima; keep an eye on the reported ROM bytes and
  variable count.

If your goal is the **fx-580VN X**, see the next section — that path is supported directly.

---

## Patching a program into an fx-580VN X firmware (`cwapp`)

`wizc` embeds the 580 ROM and does the patch for you. Write the app section and choose the menu
slot with `#slot`:

```cwiz
#for 580
void leave() { }
#cwapp
#slot 3
void leave() { exit(); }
----

void main() {
    clear();
    text(4, 4, "Hello");
    while (1) { }
}
```

```console
$ build/wizc app.cwiz -o app.bin
CC app.cwiz -> app.bin
    [580 ML620906 nX-U16/100 (firmware patch) slot 3] 1 variables, 262144 bytes ROM
$ build/wizrun app.bin 580
```

The `.bin` is a complete 256 KB ROM with the hook and menu slot already set. `#slot` takes
`1..12` and must appear after `#cwapp`; without it, slot 2 is used. No `--cwapp` flag is needed.

The lower-level `patch_menu` tool is still available for patching an external dump:

```
build/patch_menu rom.bin app.wz out.bin [--slot 1..12] [--mode E0] [--force]
```

The `cwapp` target builds a Wiz program to live in the free flash of an exported firmware:

1. The program is assembled for the **`cwapp`** target: its code and ROM tables are placed at
   `3:0E40` (ROM offset `0x30E40`), where the dump is all `0xFF`.
2. The first bytes are a small **trampoline** (see `compiler/template_cwapp.asm`) that answers the
   menu's mode id and otherwise replays the two instructions it replaced.
3. A menu slot's mode id is pointed at the trampoline, so selecting that slot launches your
   program. The slot keeps its icon and name.
4. Calling `exit()` in the program resets the firmware, returning to the normal menu.

```console
$ build/patch_menu fx580vnx.bin examples/10_app_hello.wz patched.bin --slot 3
patch_menu patched.bin
    30E40  program (… bytes)
    25E1E  hook  03 e8 30 68 -> 00 f3 40 0e
    8014   menu slot 3 (…) mode id 00 00 -> E0 00
    program entry 3:0E44   exit -> 0:A1CE (firmware reset)
    slot 3 keeps its icon and name; selecting it runs the program.
    not tested on real firmware - try it in the emulator first
```

The default slot is the source's `#slot n`, otherwise **2**; `--slot` overrides it and accepts
`1..12`. Only `--mode E0` is currently wired into the trampoline.

Safety checks built in: the tool refuses a ROM of the wrong size (an fx-580VN X dump is 262144
bytes), a ROM whose menu table does not look like the expected layout, a ROM whose hook bytes
differ (a different firmware), and a non-free target area (unless `--force`). It also warns about
menu slots whose firmware modes are known to be broken in the dump.

> This path was written **from disassembly and has not been run on a real calculator**. Validate in
> an emulator before writing anything to a device you care about.

---

## Troubleshooting

| Symptom | Likely cause / fix |
|---------|--------------------|
| `./build.sh: cmake: command not found` | install CMake 3.16+ and a C++17 compiler |
| `error: unknown target '…' (use 580 or 880)` | use a model number (or the internal name `cw`/`cwii`), not a chip name |
| `error: … out of RAM for variables` | `580` has only 4 KB for variables; shrink arrays or use `880` |
| `K_… is not a key on <target>` | the key does not exist on that machine — see [API.md](API.md#key-constants) |
| `file:line: undefined variable 'x'` | assign it at top level (global) or before use |
| The build reports `note: recursive …` and the program misbehaves deep in recursion | the ~2 KB stack ran out; reduce depth or locals |
| Simulator prints `unknown opcode`/`write to ROM` | a compiler/assembler/runtime bug or hand-written asm — inspect the `.asm` listing |
| Program shows `max steps` but should have finished | it is still looping (expected for games); raise `--steps` |
| `wizrun` shows a blank screen | the program may draw only after a key press — use `--press`/`--keys` |
| `customromrun` cannot find a model | pass `--models DIR`; models live in `emulator/models/` |
| `customromrun` cannot find the emulator | pass `--emu PATH` to the `CasioEmuMsvc` binary or `.app` |

To see exactly what a compiler emitted, open the generated `prog.asm`. The runtime routines
(`__pixel`, `__key`, `__div16`, …) are appended after your code; the boot template is prepended.
