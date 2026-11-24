# Integrated packager — boot payload + preserved recovered-code blob

`tools/package_integrated.py` extends `package_standalone.py` so the CWiz OS can
ship recovered firmware routines that no longer fit in the 31,502-byte
(`0x7B0E`) boot overlay.  It does **not** modify the stock ROM source, any
`firmware-re/fx580/analysis/*` file, or any shared reverse-engineering data:
the stock ROM, RAM image and model template are read-only inputs.

## Why a second blob is needed

`make build` compiles `main.cwiz` into a single program that is loaded at
linear offset 0.  With the recovered-firmware app wired in, that payload is
already about **31,216 bytes** — only ~280 bytes below the hard budget.  The
full recovered layer (`cwiz/linked/*` + `cwiz/deep/*` + the RAM shim guard)
compiles to **30,840 bytes**, which cannot be appended.  The integrated build
therefore places it in a preserved region above the payload.

## Placement

The stock ROM has one large erased (`0xFF`) flash run in bank 3:

| region | size | use |
|---|---|---|
| `3:0E40` .. `3:FFBF` (linear `0x30E40` .. `0x3FFBF`) | 61,824 B (`0xF180`) | recovered blob, default base `3:0E40` |
| `0x0000` .. payload end | ≤ 31,502 B | CWiz OS boot payload (by design) |
| `0x3FFF6` .. `0x3FFF7` | 2 B | recomputed fx-580 checksum |

The first protected region from `package_standalone.STOCK_REGIONS` is the
default key map at `0:7B0E`; all protected regions live in bank 0, so the
bank-3 blob cannot touch them.  The packager nevertheless checks every
protected range explicitly and refuses to build on overlap.  It also refuses
a blob that crosses the measured bank-3 gap or touches the checksum word.

A typical placement map from `make integrated`:

```
Placement map (linear ROM offsets):
  [0x000000..0x0079EF]  ( 31216 B)  CWiz OS boot payload  [0:0000..., by design]
  [0x030E40..0x0386B7]  ( 30840 B)  recovered firmware blob  [3:0E40..., preserved gap]
  [0x0386B8..0x03FFBF]  ( 30984 B)  unused bank-3 gap  [3:86B8..3:FFBF]
  [0x03FFF6..0x03FFF7]  (     2 B)  fx-580 ROM checksum = 0x0064
```

## The recovered app (UI entry point)

`apps/recovered/recovered.cwiz` is included from `main.cwiz` and reached with
**SHIFT+8** at boot (SHIFT+7 still enters diagnostics, which is checked first
and remains the authority).  It compiles these recovered routines into the
boot payload and calls them directly:

- `capture_key_code()` — recovery of the `2:F5E8` `getkeycode` column scan,
  from `firmware-re/fx580/cwiz/linked/key_capture.cwiz` (with `zero_key_lines()`
  from `key_ports.cwiz`);
- `display_metrics_table_lookup()` — recovery of `0:9E24`, from
  `firmware-re/fx580/cwiz/deep/unknownA/fw_09E24.cwiz`.

The screen uses `clearRender()` / `clear()` / `print_cw()` / `num_cw()` /
`render()` and waits for a key before returning to the launcher, so the
emulator shows readable text rather than a garbage framebuffer.

The four `keep_custom` authorities (`store_serial_number`, `diagnostic_menu`,
`app_launcher`, `clean_ram`) are untouched: diagnostics still runs first, the
serial span is still written by `store_serial_number()`, and RAM clearing is
still the runtime `wipe()` builtin.

## Blob contents and execution caveat

`make blob-source` generates `build/recovered-blob-source.cwiz`: the RAM shim
guard first, then every `cwiz/linked/*.cwiz` and `cwiz/deep/*.cwiz` module
(sorted for determinism), and a small `main()`.  `wizc --target cw` compiles it
at origin 0; the packager copies it verbatim to `3:0E40`.

The blob is a **preserved code artifact**, not an in-place executable image.
The compiler emits 16-bit absolute code addresses (BL/B) and 16-bit ROM data
addresses that assume origin 0 and `DSR = 0`.  Relocating it to `3:0E40` without
adjusting those addresses, and without switching the data segment to bank 3,
would not execute correctly in place.  Making the full layer callable from
bank 3 needs a bank-aware loader/relocation step in the compiler or runtime;
that is intentionally out of scope here.  The shipped UI calls the small
subset that is compiled inline into the boot payload, where those assumptions
hold.

## Commands

```sh
cd /Volumes/Code/Wiz/cwiz_os
make integrated       # payload + blob + package + verify (does not touch make build)
make verify-integrated # re-check size, checksum, payload and blob bytes
make rom-hash          # existing standalone-ROM hash helper still works
```

`make integrated` runs `package_integrated.py` with `--program`, `--blob`,
`--blob-base`, `--base-rom`, `--rom-out`, `--model-template`, `--model-out`
and `--ram-template`.  `--blob-base` defaults to `0x30E40` and can be
overridden, but the packager re-validates the gap on every run.

## Verification performed by the packager

1. `check_budget()` from `package_standalone.py` — payload ≤ `0x7B0E`.
2. `print_clobber_report()` — honest list of stock ranges overlaid by the
   payload (boot vectors and the unprotected part of the message table).
3. `check_blob()` — no protected region, inside the bank-3 gap, no checksum.
4. Build the final 262,144-byte image, then recompute the 16-bit checksum over
   `0..0xFC00` and `0x10000..0x3FFF6` and store it at `0x3FFF6`.
5. Write `rom.bin`, `wiz/diagnostic-menu.bin`, seeded `ram.dmp`, `config.bin`
   and `interface.png` into the emulator model directory.
6. `--verify` re-opens the ROM and checks size, checksum, payload bytes and
   blob bytes at the recorded base.

## Emulator evidence

`make integrated` produces `build/cwiz-os-integrated.rom` and
`build/fx-580-cwiz-os-integrated-model/`.  A headless boot through `casioctl`
reports `run_mode: run` and a readable launcher screen; holding the SHIFT+8
boot chord (casioctl `F1` + `8`) shows the recovered-firmware screen with
`KEY`, `SCAN` and `METRIC` values read from the recovered routines.
