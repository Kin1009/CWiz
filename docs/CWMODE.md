# cwmode: adding modes to fx-580VN X and fx-880BTG firmware

Status: current (2026-11-06)

`cwmode` is a native C++17 port of the two C# ROM patchers `cwxmode` (fx-580VN X)
and `cw2mode` (fx-880BTG). It adds extra modes (an icon, one or two names, and
an entry point) to a firmware image without touching the rest of the firmware.
The reference sources are kept under `tools/cwmode/reference/` for provenance.

The patched firmware boots in CasioEmuMsvc and the added modes appear in the
mode menu. Real hardware has not been tested.

- `cwmode --model 580` is the `cwxmode` path, a 0x40000-byte image.
- `cwmode --model 880` is the `cw2mode` path, a 0x60000-byte code window.

## Quick start

```console
$ build/cwmode --model 580 stock.bin patched.bin --modes 1 \
      --code E1 --icon icon48.png --name "APP" --name "APP" --entry 31000

$ build/cwmode --model 880 stock.bin patched.bin --modes 1 \
      --code E1 --icon icon64.png --name text0.png --name text1.png --entry 30000

$ build/cwmode --model 580 stock.bin --info
$ build/cwmode --model 580 stock.bin patched.bin --interactive
```

`--model 580` accepts PNG and BMP icons of at least 48x23 pixels. `--model 880`
accepts a 64x16 icon and two 64x13 text images per mode. The input image is
only read; the output is written after every check succeeds.

## What "a mode" is in this firmware

A mode is a row in the firmware's mode menu. The menu code stores:

- a mode count byte,
- an icon for every mode,
- a name, or a rendered text bitmap, for every mode and language,
- a callback that starts the mode.

The reference tools find the firmware's tables by signature, append new data at
the first long erased (0xFF) run after the code, and redirect a handful of
branch instructions to small generated routines. The generated routines walk
the new tables and call the original firmware routines for everything else.

### fx-580VN X path (`cwxmode`, `--model 580`)

A 0x40000-byte image. Signatures in the bundled dump:

| Item | Address |
| --- | --- |
| Mode count signature | 0x2D902 |
| Mode count byte | 0x3EA0 (11) |
| Main entry call | 0xA22E, main function 0x293D8 |
| Main hook | 0x29448, resumes at the old callee 0x2E234 |
| Menu hook | 0x2BA08 |
| Mul helper | 0x1EDC8 |
| Unknown helper | 0x9846 |
| Line print helper | 0x8F7E |
| Mode hooks | 0x2C7D6 and 0x2DBD0 (plus 0x2DBEE) |
| Mode table | 0x7FFA |
| String table | 0x7FFC |
| First free byte | 0x30E42 |

New data is appended in this order:

1. Per mode: a 48x23, 1 bit per pixel icon (0x8A bytes), then two names.
   A name is written as bytes plus a NUL and padded to an even offset. A name
   that starts with `*` is a hex byte string, for example `*414243` is `ABC`.
2. A 4-byte icon pointer table, one entry per new mode.
3. For each language, a table of 6-byte mode records: a 3-byte string pointer,
   a zero byte, the mode code (language 0 only), and a zero byte.
4. A table of two 16-bit language pointers.

The tool then patches the main hook, the menu hook, the two mode hooks and the
string-extractor hook. The generated blocks are:

- the mode dispatcher (branches by mode code to the new entry points),
- the icon-entry routine (selects the icon row),
- the icon-leave routine,
- the string-extract routine (looks up the per-language name table),
- the mode-select routine (redraws the icon of the selected mode).

Only the low 16 bits of the language pointers are stored; the text routine
takes the high byte from DSR, which is why the generated code sets DSR from the
string table address. This is reference behaviour, not a bug introduced by
the port.

### fx-880BTG path (`cw2mode`, `--model 880`)

The firmware maps a 0x60000-byte code window. Code addresses 0x70000 to
0x71FFF alias the last 8 KB of that window (physical 0x5E000 to 0x5FFFF). The
port keeps the reference relocation rule: when a stored pointer lies in
0x5E000 to 0x5FFFF it is written as `pointer + 0x12000`. A 1 MB dump is
accepted, but only the first 0x60000 bytes are patched; the rest is preserved
byte for byte.

Signatures in the bundled dump:

| Item | Address |
| --- | --- |
| Mode count signature | 0x439E0 |
| Mode count byte | 0x21D8 (12) |
| Main entry call | 0x6F64, main function 0x39442 |
| Main hook | 0x39500, resumes at the old callee 0x44A64 |
| Menu hook | 0xDFC4 |
| Mode hooks | 0x40904 and 0x43CA4 (plus 0x43CB6) |
| Mode table | 0x6008 |
| String table | 0x6338 |
| First free byte | 0x54C28 |

New data is appended in this order:

1. Per mode: a 64x16 icon in two bit planes (0x80 bytes each, 0x100 total),
   then two 64x13 text images (0x68 bytes each).
2. An 8-byte icon record per mode: 3-byte pointer to plane 1, a zero byte,
   3-byte pointer to plane 2 (`plane1 + 0x80`), and the mode code.
3. String pointers. While the new mode index is below 14, the existing
   per-language string table at `sys_str_table + (old + j + 1) * 4 + language *
   60` is overwritten in place. Later modes are appended as 4-byte pointers.
4. When `old + new > 14`, a per-language pointer table and the extended text
   routine are appended as well.

The generated blocks are the mode dispatcher, the icon routine, the optional
text routine, and the mode-select routine. The icon routine's table base is
deliberately computed before the table is appended, so the generated indexing
matches the reference byte for byte.

## Interactive mode

Without `--modes`, `--interactive` asks the same questions as the C# tools:

```
[ + ] Patch from 3:0E42 (Y/N)
[ * ] Number of modes to add:
[ * ] Code for mode 0:
[ * ] Icon for mode 0:
[ * ] Name for mode 0 (language 0):        (580)
[ * ] Text image for mode 0 (language 0):  (880)
[ * ] Entry addr for mode 0:
```

Answering `N` to the first question asks for a patch address. On the 880,
values at or above 0x70000 are stored at `address - 0x12000`, exactly like the
reference prompt. The 580 does not relocate.

## Command-line options

```
cwmode --model 580|880 IN.bin OUT.bin --modes N
       --code HEX --icon PNG [--name TEXT|PNG]... [--entry ADDR]
       [--code HEX ...]
```

| Option | Meaning |
| --- | --- |
| `--model 580|880` | Firmware family, required |
| `--modes N` | Number of modes that follow, required unless `--interactive` |
| `--code HEX` | Mode id byte, starts a new mode; `E1` or `0xE1` |
| `--icon PNG` | Icon file |
| `--name` | 580: menu name (repeat for language 0 and 1). 880: 64x13 text image (repeat twice) |
| `--name-image PNG` | 880 alias for `--name` |
| `--entry ADDR` | Entry address for the current mode |
| `--at ADDR` | Patch offset; default is the first long erased run |
| `--force` | Allow a target range that is not blank |
| `--info` | Print the discovered tables and exit |
| `--interactive` | Ask the reference questions |
| `--json` | One-line machine-readable summary |
| `--verbose` | Dump every generated payload, like the reference tools |

Options apply to the mode opened by the preceding `--code`. A mode must have
an icon before it can be patched. The 880 requires exactly two text images per
mode; missing images are an error. The 580 fills a missing name with an empty
string.

### Validation

Before anything is written:

- the image is the expected size (580 exactly 0x40000; 880 at least 0x60000),
- every signature and helper address is present and inside the window,
- the mode count is plausible and the new count fits in a byte,
- every icon and text image decodes and is at least the required size,
- the target address is even, inside the window, and after the firmware code,
- the target range is blank (0xFF) unless `--force` is given,
- the generated block, including all tables and routines, fits before the end
  of the window.

On any failure the command exits non-zero, prints the reason, and does not
create or truncate the output file. The library returns an empty image on
failure for the same reason.

## Integration with cwapp

The Wiz toolchain's `cwapp` target builds a program that is placed into the
fx-580VN X firmware by `patch_menu`. `cwmode` is the companion for the other
half of the job: it adds a menu entry with a chosen icon and name and points it
at an entry address.

Two ways to use it:

1. Command line, after building the app:

   ```console
   $ build/wizc --target cwapp app.cwiz
   $ build/patch_menu stock.bin app.cwiz app.bin
   $ build/cwmode --model 580 app.bin app-modes.bin --modes 1 \
         --code E1 --icon app48.png --name "APP" --name "APP" --entry 30E40
   ```

   The entry address is the app entry reported by `patch_menu` or `wizc`
   (`30E40` for the current template). The mode starts that app from the mode
   menu.

2. The Wiz Compiler "Add Modes..." panel. It is a separate debugger window
   (`Gui/CwModeWindow.cpp`, registered next to the Wiz Compiler window). It
   reads the stock ROM, patches it in memory with the same library, writes the
   result as a session image (`wiz/rom-modes-N.bin`), and reboots the model on
   it. The stock ROM file on disk is never modified, and the Wiz Compiler's
   "Stock ROM" action switches back.

The exact one-hunk menu addition for `Gui/WizToolchain.cpp` (owned by another
agent) is in `tools/cwmode/integration/WizToolchain-add-modes.patch`. It adds
"Calculator > Add Modes..." and brings the panel to the front. The panel itself
and its registration in `Gui/Ui.cpp` are already in the tree.

## Tests

`ctest` runs two entries:

- `toolchain`: the whole Wiz test suite, which now includes the cwmode cases,
- `cwmode`: a standalone binary for the ROM fixture.

The fixture in `tests/data/cwmode` contains:

- the PNG inputs used for the reference runs,
- golden metadata with the SHA-256 of the input and of the complete patched
  image, produced by the original C# `Program.cs` (compiled on macOS against a
  small WPF `BitmapImage` shim; see `tools/cwmode/oracle/`).

A patch is considered correct only when the SHA-256 of the whole output equals
the reference hash. The tests also verify:

- the discovered mode count, hooks, mode and string table addresses,
- the mode count byte after patching,
- the icon table entries and both string-table languages,
- the extended table path (`old + new > 14`),
- that a 1 MB 880 dump keeps its bytes past 0x60000,
- the 880 relocation to physical 0x5E000 when asked for logical 0x70000,
- the rejection paths (size, signature, target inside code, non-blank target,
  odd target, undersized icon).

The ROMs are read from `emulator/models` and copied in memory; the originals
are never modified.

The boot check was done with CasioEmuMsvc's headless control channel
(`casioctl`): the patched 580 and 880 images boot to the mode menu, and the
added icons and names appear after scrolling. Both the simple and the extended
table paths were checked.

## Limitations

- The signatures match the bundled fx-580VN X and fx-880BTG dumps. A different
  firmware revision is rejected cleanly instead of being patched blindly.
- The 880 path only patches the 0x60000 code window. Bytes past it in a 1 MB
  dump are preserved but not used by the added pointer tables.
- The reference tools use WPF `BitmapImage`. The port uses stb_image, which
  supports PNG, BMP, TGA, JPEG and GIF but not TIFF. Use opaque images. For
  semi-transparent pixels WPF's premultiplied conversion can differ; the mode
  icons are 1-bit or 2-bit, so alpha should be either 0 or 255.
- Images larger than the target are cropped at the top-left, like WPF
  `CopyPixels`. Images smaller than the target are rejected; the reference
  would have read uninitialised stack bytes for Set3/Set4, so there is no
  defined byte output to reproduce.
- fx-580VN X names are ASCII. Characters above U+007F become `?`, matching
  `Encoding.ASCII`. Use the fx-880BTG text-image path for non-Latin names.
- The unusual backward table bases (the 880 icon table and the 580 language
  tables) are reference behaviour. Changing them would change the generated
  routines and is not covered by hardware testing.
- No ROM checksum is updated. The bundled emulator does not validate one.
- Mode counts above 14 take the reference's extended path; very large counts
  are limited only by the free space and the one-byte count field.
- The Add Modes panel is registered for the 580 and 880 models only. It does
  not replace the Wiz Compiler's own patch actions and does not compile code.

## Reference layout

```
tools/cwmode/reference/cw2mode/     original C# fx-880BTG tool and CasioInternal helpers
tools/cwmode/reference/cwxmode/     original C# fx-580VN X tool and CasioInternal helpers
tools/cwmode/oracle/                WPF shim and projects that regenerate the golden hashes
tools/cwmode/integration/           WizToolchain.cpp patch for the Add Modes menu item
compiler/src/cwmode.hpp             library API (analysis, images, patch)
compiler/src/cwmode.cpp             implementation
compiler/src/main_cwmode.cpp        the cwmode command-line driver
compiler/src/third_party/stb_image.h vendored public-domain image decoder
tests/test_cwmode.cpp               CTest fixture
tests/data/cwmode/                  PNG inputs and golden SHA-256 metadata
```

The vendored copies in the emulator tree (`third_party/wiz/src/cwmode.*` and
`third_party/wiz/src/third_party/stb_image.h`) must stay identical to the Wiz
compiler copies, because the in-process Add Modes panel links the same library.
`tools/sync_vendored_cwmode.sh --check` reports drift; run it without `--check`
to update the preview trees. The stable snapshot is intentionally excluded.
