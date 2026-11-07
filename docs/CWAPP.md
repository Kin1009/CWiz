# .cwapplink: many cwapp apps in one fx-580VN X ROM

Status: current (2026-11-07)

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
#cwapp 13 "My App" unselected.png
```

- The first argument is the app slot and shortcut position. It must be a
  positive integer.
- The second argument is the mode text. Use quotes when it contains spaces.
- The icon arguments are one or two 48x23 images. Paths are relative to the
  source file. With one icon, the selected variant is the exact inverse of the
  finished unselected bitmap, including its border and shortcut-key box. For a
  decoded image, transparent pixels are flattened over white, the RGB channels
  are inverted and the result is stored opaque. On the packed 1-bit 580 icon and
  the two-plane 880 icon the selected variant is the bitwise complement of the
  packed bytes.
- `#slot` must not be used with this form. The compiler reports
  `#slot is not used with '#cwapp <slot> <text> <image> [<selected>]'` when
  both appear.

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

The key column is the on-screen label and the hardware legend. It is not the
emulator's key name. For example, slot 13 is the `D` label with the `sin`
legend, so the physical `sin` key is the shortcut. The legends with non-ASCII
glyphs are written with ASCII-safe equivalents in the build log and the editor:
`B deg`, `C x^-1`, `z s<>d`.

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

## Icon conversion (`cwicon`)

`cwicon` turns ordinary artwork into the stock app-menu icon style and prints
the exact packed bytes the firmware stores. It uses a 48x23 one-bit canvas on
the 580 and a 64x16 two-plane canvas on the 880.

Stock tile decoration, painted on top of the artwork so a bright image cannot
erase it:

- 1 px separator on the right edge and the bottom edge.
- A filled shortcut-key box at the bottom-right. On the 580 the box is
  `x=38..46`, `y=12..21`, with the top-left pixel `(38,12)` rounded off. This is
  measured from the stock ROM icons in the menu table at `0x7FF8`. The firmware
  draws the key digit inside it in black, so the box itself stays empty.
- On the 880 the same decoration is scaled: separator at `x=62` and `y=15`, box
  `x=54..62`, `y=8..14`, rounded top-left corner.
- The selected icon is the exact bitwise inverse of the finished unselected
  bitmap, so border, box and artwork all invert.

The `-o` PNG keeps the compiler input convention: dark pixels are ink and
become 1 in the packed bytes. In the packed/LCD bitmap, and in the stock
reference dumps, 1 is white, so the border and box are white there. Use
`--raw` or unpack the bytes to inspect that polarity.

```console
$ build/cwicon --model 580 logo.png -o app_icon.png --raw app_icon.bin \
      --preview-both app_icon_both.png
$ build/wizc app.cwiz -o app.bin
# #cwapp 13 "Logo" app_icon.png
```

The three-argument `#cwapp` form then derives the selected variant as the exact
inverse. To supply a custom selected image instead, use the four-argument form.

Rules:

- Ink is a pixel whose luminance is below `--threshold` (default 128); alpha
  below 128 is background. `--threshold 0` inks nothing; `--threshold 255`
  leaves pure white as background.
- Artwork at least the content size in both axes is centre-cropped. Artwork
  smaller in both axes is centred without upscaling. Mixed sizes are scaled
  down preserving aspect, then padded. `--no-border` uses the full canvas and
  omits the separator.
- `-o` is the unselected PNG at the exact icon size, directly usable in
  `#cwapp`. `--raw` is `0x8A` bytes for the 580 or `0x100` bytes for the 880
  (plane 0 then plane 1).
- Without `--selected`, the selected variant is the inverse of the packed
  unselected bytes. `--preview-both` writes a 4x side-by-side preview.

## Preserving RAM across an app (`#preserve`)

A cwapp owns the CPU, so values written by the firmware before the app starts
survive only if the app protects them. Add repeatable `#preserve` lines after
`#cwapp`:

```cwiz
#for 580
#cwapp
#slot 1
#preserve 0xD114 1
#preserve 0xE000 4
```

At `_APP` entry the runtime snapshots those byte ranges. MENU, `exit()` and a
return from `main()` restore them byte for byte before the mode-menu return.

- Ranges are inside cwapp RAM `0xD000..0xFFFF`, have a positive length, do not
  overflow, and do not overlap the cwapp save area `0xE5D3..0xE7FF`.
  Overlapping ranges and more than 8 ranges are rejected with a diagnostic.
- The snapshot buffer is a hidden symbol allocated through the compiler's RAM
  allocator, never a hardcoded free address.
- The fixed capacity is 8 ranges and 128 snapshot bytes. The directive is
  checked at compile time. The runtime `preserve(addr, len)` call registers a
  later range and returns 1 on success, 0 for an invalid range or a full table;
  it snapshots immediately. `restore_preserved()` restores every registered
  range immediately, which is useful after a function mutates the state.
- The state block is 162 bytes: count, used bytes, 8 four-byte descriptors and
  the 128-byte snapshot area.

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
   `B 2:5E88` for mode 3 and `B 2:5E22` otherwise. A second dispatcher is
   installed at the firmware main hook (`2:9448`) so ON relaunches the app that
   was running.
4. Extra apps are appended to the firmware mode list with the cwmode engine.
   Each generated mode routine calls the app's own `_APP` entry. When legacy
   apps are also present, a small dispatcher in front of the cwmode main hook
   handles the legacy ids and passes every other id to cwmode.
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
apps in slots 13 and 14, the mode menu showed `D:APP B13` and `E:APP A14`. The
shortcut order followed the sorted slots, and pressing the physical keys for
the `sin` and `cos` legends (slot 13 is `D`/sin, slot 14 is `E`/cos) launched
APP B13 and APP A14 respectively. A thirteen-app project (slots 13 through 25)
booted, showed the keyed labels through `.`, and ran the unkeyed slot 25 app.

## Leaving and restarting the app

Every cwapp form saves the firmware stack, registers, PSW and LR when `_APP` is
entered. MENU, `exit()` and returning from `main()` restore that state and call
the firmware mode-menu routine at `2:AD4C`, so the app leaves to the mode menu
without a reset or a splash.

- MENU is checked inside `key()`, `key_down()` and `key_hit()`. An app that
  never polls a key function cannot see it.
- Plain AC is not intercepted: `key()` reports `K_AC` (29) and the app decides
  what to do with it.
- OFF is Shift+AC. The runtime follows the stock power-off path: LCD off, FCON
  cleared, interrupts masked, STOPACP programmed and STOP entered. ON then
  repowers the calculator.
- ON is a repower of the calculator, not an in-place restart. After power-on
  the firmware relaunches the app that was running. In a multi-app ROM that is
  the selected app, not the first app and not the menu.

The single legacy app returns through the set_mode continuation (`2:5E22`)
after the menu call. Multi-legacy projects also patch the firmware main hook at
`2:9448` with a boot dispatcher: it compares the running mode id in `R0`,
branches back to that app's `_APP`, and falls through to the stock menu function
for every other id. A mixed project puts the same small dispatcher in front of
the cwmode main hook, so both legacy and appended apps relaunch.

### How `exit()` works

`exit()` is a zero-argument builtin that is only accepted for the cwapp target.
It does not call the firmware reset vector. It unwinds the app and hands the
calculator back to the firmware menu:

1. **Entry snapshot.** `_APP` pushes `LR`, `PSW` and `ER0` through `ER14` on the
   firmware stack, records the stack pointer at `0xE5D3`, and copies the live
   firmware stack window into `0xE5D5` onwards. The snapshot area ends at
   `0xE7FF`, below the runtime heap at `0xE820`. The app then switches `SP` to
   `0xF000` and runs normally.
2. **Exit.** `exit()`, a MENU key read, or returning from `main()` reaches the
   generated `__cwapp_return` epilogue. It copies the saved stack back, restores
   `SP`, `ER14` through `ER0`, `PSW` and `LR`, so the CPU is exactly where the
   firmware left it when it entered the app.
3. **Menu call.** The epilogue sets the mode-menu bookkeeping (`D113 = 1`; for
   appended and re-launched apps also `D111 = 0C1h`), calls the firmware mode
   menu routine at `2:AD4C`, and continues:
   - a single legacy app resumes `set_mode` at `2:5E22`,
   - an appended-mode app returns to the generated mode routine with `pop pc`,
   - a boot-relaunched app resumes the same `2:5E22` continuation.

The menu routine repaints the display, so the mode menu appears immediately.
There is no reset, no boot splash and no RAM clear; only the firmware CPU state
is restored. App variables keep their last values in RAM until something
overwrites them, which is why a relaunch starts from `_APP` and re-runs the
program's initialisation.

If the firmware stack was too tight to snapshot (the stack pointer was below
the save window), `_APP` records a zero pointer and `exit()` falls back to the
reset handler at `0:A1CE`. That fallback is the only path that looks like a
reboot; normal builds do not take it.

`exit()` is not a general return statement. Calling it only leaves the app; it
does not return a value, and it is rejected by the compiler for any target other
than cwapp. MENU is detected inside `key()`, `key_down()` and `key_hit()`, so a
program must keep polling one of those functions for the key to be seen. AC is
never intercepted, and ON is the hardware power key handled by the calculator
itself.

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
checks 68 cases:

- slot keys 1..24, slot 25 and beyond without a key, ASCII-safe legends,
- manifest comments, blank lines and relative paths,
- two-app and thirteen-app golden SHA-256 values,
- sorted slot order and per-app code blocks that match a standalone build at
  the same base,
- distinct entries for every appended mode,
- legacy single app equal byte for byte to the existing patch,
- three legacy apps with the `R8` dispatcher and keys 1, 9, A, including the
  boot dispatcher that relaunches the running app and keeps mode 0D on the menu
  path,
- mixed legacy plus extra project,
- the three-argument `#cwapp` form for 1, 2, 6 and 30 apps, and byte equality
  against a four-argument source with the inverse PNG,
- `cwicon` crop/pad/aspect rules, threshold edges, inverse packing, stock
  round-trip and 580/880 packed lengths,
- `#preserve` single and multiple ranges, mid-run restore, dynamic
  registration, invalid/overlapping ranges and capacity limits,
- MENU, `exit()` and returning from `main()` call the firmware menu routine at
  `2:AD4C`; AC is returned to the app; Shift+AC reaches the stock STOP path,
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
