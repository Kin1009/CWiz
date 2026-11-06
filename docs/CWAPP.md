# .cwapplink: many cwapp apps in one fx-580VN X ROM

Status: current (2026-11-06)

A `.cwapplink` project compiles several cwapp sources into one 262144-byte
fx-580VN X firmware image. Every app gets its own code block and its own mode
menu entry. Each entry branches to that app's real entry point, so selecting a
mode runs the correct program.

The single-file `#cwapp` build is unchanged. A bare `#cwapp` plus `#slot n`
still replaces one of the twelve stock menu slots with exact legacy bytes.

## Quick start

```console
$ build/wizc project.cwapplink -o multi.bin
$ build/wizrun project.cwapplink
```

The manifest lives next to the sources. Paths in the manifest are relative to
the manifest file; icon paths in a `#cwapp` line are relative to the source
file. Absolute paths are accepted but never required.

The repository example is `examples/cwapplink/demo.cwapplink`.

## Project file

`project.cwapplink` is plain UTF-8 text:

```
# blank lines and whole-line comments are ignored
app_a.cwiz
app_b.cwiz
```

Rules:

- One source path per line. Blank lines and lines whose first non-space
  character is `#` are ignored.
- A path may be quoted when it contains spaces, for example
  `"my apps/one.cwiz"`.
- Relative paths resolve against the manifest's directory.
- Only `.cwiz` and `.wz` sources are accepted. A missing file, a duplicate
  path, or a source without `#cwapp` is an error that names the file and line.
- The `.cwapplink` model is the fx-580VN X. There is no fx-880BTG cwapp boot
  and runtime target, so a link project cannot build 880 app code. See the 880
  section below.

## The two `#cwapp` forms

### Legacy form

```cwiz
#for 580
#cwapp
#slot 3
```

- `#cwapp` alone is the legacy app-menu patch.
- `#slot n` is optional and takes `1..12`; the default is 2.
- The app replaces the stock menu slot, keeping that slot's icon and name.
- A single legacy app in a project produces exactly the same bytes as the
  existing single-file `wizc` build and `patch_menu`.

### Extra app form

```cwiz
#for 580
#cwapp 13 "My App" unselected.png selected.png
```

- The first argument is the app slot and shortcut position. It must be a
  positive integer.
- The second argument is the mode text. Use quotes when it contains spaces.
- The last two arguments are the unselected and selected 48x23 icons. Paths are
  relative to the source file.
- `#slot` must not be used with this form. The compiler reports
  `#slot is not used with '#cwapp <slot> <text> <image> <selected>'` when both
  appear.

## Slots, shortcut keys and ordering

The slot is the on-screen shortcut position. The mapping is:

| Slot | Key | Hardware legend |
| --- | --- | --- |
| 1 | `1` | `1` |
| 2 | `2` | `2` |
| 3 | `3` | `3` |
| 4 | `4` | `4` |
| 5 | `5` | `5` |
| 6 | `6` | `6` |
| 7 | `7` | `7` |
| 8 | `8` | `8` |
| 9 | `9` | `9` |
| 10 | `A` | `(-)` |
| 11 | `B` | degrees |
| 12 | `C` | `x^-1` |
| 13 | `D` | `sin` |
| 14 | `E` | `cos` |
| 15 | `F` | `tan` |
| 16 | `x` | `(` |
| 17 | `y` | `)` |
| 18 | `z` | `s<>d` |
| 19 | `M` | `M+` |
| 20 | `+` | `+` |
| 21 | `-` | `-` |
| 22 | `*` | `*` |
| 23 | `/` | `/` |
| 24 | `.` | `.` |
| 25 and above | none | none |

The legends with non-ASCII glyphs are written with ASCII-safe equivalents in
the build log and the editor: `B deg`, `C x^-1`, `z s<>d`.

Ordering rules:

- Apps are sorted by slot, not by manifest order. The same order is used for
  code blocks, dispatcher entries, mode ids and appended modes.
- Slots 1..12 belong to the stock menu. An **extra** app on those slots is an
  error: it would take a key that a stock mode already owns. Use the legacy
  `#cwapp` plus `#slot n` form to replace that menu slot instead.
- Slots 13..24 are the appended shortcut keys `D` through `.`. The firmware
  labels appended modes by their append order, so these slots must form a
  contiguous run starting at 13. A project that uses slot 15 must also include
  slots 13 and 14; the diagnostic names the missing slot.
- Slots 25 and above are valid apps with unique identity and no shortcut key.
  They are appended after the keyed modes.
- No 26-app cap is imposed. The practical limit is the free flash and the
  mode-id range described below.

## Icons on the 580 and the selected/unselected pair

The fx-580VN X mode record stores exactly one 48x23 one-bit icon per mode. The
menu draws its own selection highlight around the selected icon, so there is no
second icon slot in the record.

The builder therefore:

- validates both images (at least 48x23),
- stores the **unselected** image as the mode icon,
- reports a warning when the two images differ: the selected image cannot be
  stored separately, and the menu highlight is the firmware's own,
- reports no warning when the images are identical.

The selected image is never dropped silently: a warning is part of the build
result, and the documentation states the rule. If the artwork needs a selected
variant, bake the highlight into the stored icon.

## fx-880BTG

The fx-880BTG mode table stores one 64x16 two-plane icon and rendered 64x13
text images per language. There is no 880 cwapp boot template, exit handler or
runtime target, so a `.cwapplink` project cannot build 880 app code. The builder
rejects a non-580 base ROM with a precise diagnostic.

Use `cwmode --model 880` (or the debugger's Add Modes panel) to add 880 modes
whose entry points already exist in the firmware. That path requires two 64x13
text images per mode, because the 880 menu stores rendered text, not a string.
The rule is documented in [CWMODE.md](CWMODE.md).

## ROM layout

The base is a stock 262144-byte fx-580VN X image: the bundled ROM or a file
passed with `--rom FILE`.

1. Code blocks start at `3:0E40` (`0x30E40`), the first long erased area after
   the firmware code.
2. With one legacy app, that app occupies `0x30E40` and the existing trampoline,
   hook and menu-slot patch are used unchanged. With extra apps, the extra code
   blocks follow.
3. With several legacy apps, a dispatcher is written at `3:0E40`. It compares
   the mode id in `R8` with each legacy id and branches to the matching app's
   `_APP`. It replays the two instructions the hook replaced:
   `B 2:5E88` for mode 3 and `B 2:5E22` otherwise.
4. Extra apps are appended to the firmware mode list with the cwmode engine.
   Each generated mode routine calls the app's own `_APP` entry.
5. The cwmode tables and routines are placed after the last code block. The
   builder verifies the complete block fits before `0x40000`.

Mode ids:

- A single legacy app keeps the legacy `E0` id so its bytes stay identical.
- New modes use the safe range `A0..BF`, which avoids the stock ids seen in the
  bundled menu table (`E0`, `E1`, `02`, `06`, `07`, `03`, `0C`, `88`, `45`,
  `4B`, `89`, `4A`).
- The range gives at most 32 new modes. This is the hard limit, not 12 or 26.

Every code block and the appended region is checked for `0xFF` before writing.
`--force` allows a non-blank target. The build reports the changed spans. On
any error nothing is written, and the result carries no ROM.

## Dispatcher proof

`compiler/template_cwapp.asm` documents the hook contract: the firmware's
`set_mode` reaches `2:5E1E` with `R8` holding the mode id taken from the menu
table at `0x7FF8 + 2`. The stock bytes are `CMP R8,03` / `BEQ 2:5E88`.

For one legacy app the existing trampoline compares `R8` with `E0` and replays
the same fallbacks. For several legacy apps the generated dispatcher uses the
same compare and branch encoding:

```
cmp r8,#id0 ; bne next ; B app0
cmp r8,#id1 ; bne next ; B app1
...
cmp r8,#03  ; bne back ; B 2:5E88
back: B 2:5E22
```

The menu slot's mode byte is set to that app's id. Selecting the slot calls
`set_mode` with the id, the dispatcher matches it, and `B _APP` enters the app.

This was checked in CasioEmuMsvc, not only in unit tests: with two legacy apps
in slots 1 and 2, selecting slot 1 ran app A and slot 2 ran app B. With extra
apps in slots 13 and 14, the mode menu showed `D:APP B13` and `E:APP A14`; the
shortcut key order followed the sorted slots, and selecting each ran its own
app. A thirteen-app project (slots 13 through 25) booted and ran the last app
as well.

## Command line

```console
$ build/wizc project.cwapplink -o multi.bin
$ build/wizc project.cwapplink -o multi.bin --rom stock.bin
$ build/wizc project.cwapplink -o multi.bin --force
$ build/wizrun project.cwapplink
```

`--rom FILE` selects the base firmware. The default is the bundled fx-580VN X
ROM. `--force` permits overwriting a non-blank code block or appended region.

`wizrun` builds the project and starts the first app (the lowest slot) in the
simulator. The simulator starts at that app's `_APP`; use the emulator for a
visual check of the menu and of several apps.

For a single extra-form source, `wizc app.cwiz -o app.bin` applies the same
builder to one app and writes a complete ROM.

## Debugger integration

The Wiz Compiler opens a `.cwapplink` file like any other document. **Build &
Run** compiles every listed source, writes the combined image, and reloads the
model on it. The log shows one line per app:

```
slot 13  key D    entry 30E54   2848 bytes  app_a.cwiz
slot 14  key E    entry 31960   2848 bytes  app_b.cwiz
```

The result is stored as the session image `wiz/rom-cwapplink.bin`. The stock ROM
file on disk is not modified. The Wiz Compiler's **Stock ROM** action switches
back. Build without Run writes the combined `.bin` next to the manifest.

The examples browser sets the editor's working directory to the opened
example's own folder for every example set, so relative `#include`, `#binary`,
icon and asset paths resolve against the example. The same directory is used by
file open and Build & Run.

The Add Modes panel is the mode-table counterpart: its appended modes take the
same next shortcut labels in order (`D`, `E`, `F`, `x`, `y`, `z`, `M`, `+`,
`-`, `*`, `/`, `.`).

## Diagnostics

| Situation | Diagnostic |
| --- | --- |
| Missing or unreadable source | `source file not found: <name>` with the manifest line |
| Duplicate source path | `duplicate source in the project: <name>` |
| Source has no `#cwapp` | `not a #cwapp source: ...` with the file |
| Two apps use one slot | `duplicate app slot N (also used by ...)` |
| Extra app on a stock key | `slot N is the stock menu shortcut key K ...` |
| Missing keyed slot | `app slot N (key K) needs slot M first ...` |
| `#slot` with the extra form | `#slot is not used with '#cwapp <slot> ...'` |
| Non-blank target | `.... is not free flash (not 0xFF); use --force` |
| Project too large | `the project does not fit: <file> starts at ...` |
| Bad icon | the image decoder message, plus the source file |
| 880 or other base | `the .cwapplink builder needs a 262144-byte fx-580VN X image` |

Every failure returns no ROM. `wizc` writes no output file on failure.

## Tests

`ctest` runs `cwapplink` and `toolchain`. `build/cwapplink_tests` currently
checks 53 cases:

- slot keys 1..24, slot 25 and beyond without a key, ASCII-safe legends,
- manifest comments, blank lines and relative paths,
- two-app and thirteen-app golden SHA-256 values,
- sorted slot order and per-app code blocks that match a standalone build at
  the same base,
- distinct entries for every appended mode,
- legacy single app equal byte for byte to the existing patch,
- three legacy apps with the `R8` dispatcher and keys 1, 9, A,
- mixed legacy plus extra project,
- duplicate slot, stock-key conflict, keyed gap, non-cwapp source, missing
  source and non-580 base errors,
- CLI success, duplicate-slot failure with no output, and a single extra
  source.

The emulator self-test builds the `examples/cwapplink` project through the Wiz
Compiler and reports `example_build=PASS`.

## Limits

- fx-580VN X only for linked app code.
- 32 new modes maximum (`A0..BF`), and the ROM must have room for every code
  block plus the mode tables and routines.
- Keyed extra slots 13..24 must be contiguous from 13. Slots 25 and above are
  allowed without a key.
- One 48x23 icon per 580 mode; the selected image is validated and reported but
  cannot be stored as a second icon.
- 580 mode text is ASCII. Use the 880 text-image path for other scripts, but
  that path does not build app code.
- Not tested on real hardware.
