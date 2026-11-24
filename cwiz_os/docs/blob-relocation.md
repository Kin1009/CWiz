# Bank-3 blob relocation — from preserved artifact to executable code

> Orchestrator update: relocation is now wired into the Makefile. `make
> integrated` relocates the recovered blob at `0x30E40` and the idiomatic blob
> at its own separate base before concatenating them, then runs
> `make verify-relocation`. The integrated ROM therefore carries executable
> relocated code, not a preserved artifact. Verified build: checksum
> `0x3526`, ROM SHA-256 `7dfcdbb6…8127862`; both relocation maps PASS (1,664
> + 1,030 branches). Boot evidence: `cwiz_os/build/relocated-evidence/`.
> The DSR limitation below still applies to ROM-table readers.

## Problem

`make integrated` compiles the recovered-firmware layer with
`wizc --target cw --lang cwiz` (origin 0, DSR 0) and copies the 30,840-byte
result verbatim to linear `0x30E40` (`3:0E40`).  That image is a **preserved
code artifact**, not code that can run where it sits:

* every absolute `B`/`BL` encodes bank 0 in the opcode, so the first branch
  would jump out of bank 3 back to bank 0;
* every 16-bit ROM-data constant is a bank-0 offset.

`docs/integrated-packager.md` called this out and left a bank-aware loader as
out of scope.  This document records the concrete fix.

## Chosen approach: static bank-aware relocator (not a runtime loader)

Evidence supporting a static relocator:

1. `compiler/src/asm.cpp` (`Assembler::encode`) and `compiler/src/cwmode.cpp`
   (`branch()`) agree on the encoding: absolute `B` is the four bytes
   `00 F{bank} lo hi`, `BL` is `01 F{bank} lo hi`.  The emulator
   (`CasioEmuMsvc/src/Chipset/CPU.cpp`, `CPU::OP_B`) decodes the high nibble
   into `CSR` and the following word into `PC`.  The bank is therefore an
   in-place, 1-nibble edit; no trampoline is needed.
2. `compiler/src/targets.cpp`: `TARGET_CW` has `rom_seg = 0`; all code and data
   addresses emitted by `wizc --target cw` are 16-bit bank-0 values.
3. `firmware-re/fx580/tools/nxu16.py` is an exact decoder generated from the
   emulator opcode table.  A linear decode of this compiler output covers all
   30,840 bytes and yields 11,744 instruction boundaries, so byte patterns can
   be validated against real boundaries instead of guessed.

A runtime loader would still have to patch the same fields, would need a
writable/executable mirror of the 30 KB image (none exists: the boot payload
owns bank 0 up to `0:7B0E`), and would leave the flash image non-executable.
Static relocation is smaller, deterministic, verifiable offline, and is the
approach implemented in `tools/relocate_blob.py`.

## What is patched

| class | original | relocated | count on the real blob |
|---|---|---|---:|
| absolute `B` | `00 F0 lo hi` (bank 0, offset `X`) | `00 F3 (X+0E40)` | 983 |
| absolute `BL` | `01 F0 lo hi` | `01 F3 (X+0E40)` | 681 |
| ROM-data address | `MOV Rn,#lo` / `MOV R(n+1),#hi` = `X` | value `X+0E40` | 19 |

Total **1,683 relocations**, 5,030 bytes changed.  The image keeps its size and
all non-patched bytes are identical.

Rules that prevent corrupting data:

* a branch byte pattern is only patched when it lies on a decoder-confirmed
  instruction boundary **and** decodes as absolute `B`/`BL`;
* the image is compiled at origin 0, so a pattern whose bank nibble is nonzero
  is font/table data misdecoded as a branch (3 such patterns) and is skipped;
* a genuine external branch (`__reset`: `B 0:A1CE`, the stock reset stub) is
  left alone — it belongs to the stock ROM, not the blob;
* only `MOV`-pair values inside the detected ROM-data region
  (`0x6EE8..0x7878`, auto-detected as the widest gap between 16-bit constants)
  are treated as data addresses.  RAM/SFR addresses and numeric constants are
  untouched;
* `0x0E40 + 0x7877 = 0x86B7`, so no 16-bit offset overflows.

## What is NOT patched (remaining blocker for data-using functions)

The `cwapp` target in `cwiz.cpp` emits `DSRI #3` immediately before every
ROM-data load.  In the emulator, `reg_dsr` is reset to 0 after each instruction
and only the single instruction following `DSRI` sees the new bank
(`CPU::Next()`).  Replacing a 2-byte instruction with `DSRI; load` would shift
every later address, so the in-place relocator cannot insert those prefixes.
It fixes data **offsets** but not bank selection:

* functions that touch only registers, RAM (DSR 0) and SFRs are executable
  in place after relocation;
* functions that read ROM tables still need a compiler/linker change that
  emits `DSRI #3` for the `cw` target (or a `cwapp`-style rebuild), which is
  outside the files owned by this change.

Also out of scope: computed `B ERn`/`BL ERn` targets (0 in this blob) and
pointer tables holding code addresses (none found in the data region).

## Usage

```sh
# Standalone relocation + map
python3 tools/relocate_blob.py \
  --blob build/recovered-blob.bin --blob-base 0x30E40 \
  --out build/recovered-blob.reloc.bin \
  --map-out build/recovered-blob.reloc.relocations.json

# Verify every rewritten target and the byte-level diff
python3 tools/verify_blob_relocation.py \
  --blob build/recovered-blob.bin \
  --relocated build/recovered-blob.reloc.bin \
  --map build/recovered-blob.reloc.relocations.json

# Package with the relocation enabled (default remains verbatim)
python3 tools/package_integrated.py \
  --program build/cwiz-os-program.bin \
  --blob build/recovered-blob.bin --blob-base 0x30E40 \
  --relocate-blob \
  --base-rom "/Volumes/Code/Wiz/emulator/models/fx-580VN X/rom.bin" \
  --rom-out build/cwiz-os-integrated.rom \
  --model-template assets/model --model-out build/fx-580-cwiz-os-integrated-model \
  --ram-template "/Volumes/Code/Wiz/emulator/models/fx-580VN X/ram.dmp"
```

`package_integrated.py` keeps its existing default: without `--relocate-blob`
the blob is still copied verbatim and the shipped UI still uses the inline
recovered subset.  With the flag, `--relocation-map` chooses the map path
(default `<rom-out>.relocations.json`).  All existing safety checks — payload
budget, clobber report, protected regions, bank-3 gap, checksum and 262,144-byte
output — run on the final (relocated) bytes.  `--verify` rebuilds the same
relocation before comparing.  The Makefile owner can add `--relocate-blob` to
the `integrated`/`verify-integrated` recipes to make `make integrated` produce
an executable image.

## Verification results (real 30,840-byte blob)

```
Relocated 30840 bytes from 0:0000 to 3:0E40 (linear 0x30E40).
  branches: 1664 (983 B, 681 BL); ROM data addresses: 19; total: 1683
  data start detected: 0x6EE8
  SHA-256 origin : 6055CF47...419D
  SHA-256 relocated: 8831FB93...C76D
```

`verify_blob_relocation.py` reports:

```
Relocation verification: PASS — 1664 branches, 19 data refs,
1664 target boundaries confirmed, 5030 bytes changed (expected 5030).
```

It re-decodes both images, confirms each old/new target, confirms that each
patched target maps back to an instruction boundary, and asserts that the set
of changed bytes equals the set declared in the relocation map (no silent
edits to data or RAM/SFR constants).

`package_integrated.py --verify` on the built ROM also passes (262,144 bytes,
recomputed checksum, relocated blob byte-identical at `0x30E40`).

## Emulator execution evidence

A bank-0 test harness (raw reset vectors + `BL 3:0E72`) was packaged with the
relocated blob; it jumps to the blob's relocated `_START` and lets its `main()`
call the recovered function `f_display_metrics_table_lookup` (original
`0:3BB2`, relocated `3:49F2`).  CasioEmuMsvc / casioctl results:

* stable `PC = 0x37D20`, i.e. bank 3 offset `0x7D20` = blob file offset
  `0x7D20 - 0x0E40 = 0x6EE0`, the relocated self-loop of `f_main`.  The
  unrelocated control image instead wanders to `PC = 0xF0350`, outside the
  blob — the same image without relocation does **not** execute correctly.
* memory write breakpoints with `--record` at the addresses written by the
  recovered path recorded hits with PCs `0x34A52`, `0x34A12` and `0x37D20` —
  all inside the relocated blob (`0x30E40..0x386B8`).  `0x34A52 - 0x30E40 =
  0x3C12`, inside `f_display_metrics_table_lookup`.
* the model's RAM canary at `0:DBD0` is intact, so this is a normal boot, not a
  crash path.

This demonstrates that at least one recovered function inside the bank-3 blob
actually executes after relocation.  It does **not** claim that every one of
the ~1,600 recovered functions is executable: the `DSRI #3` limitation above
still applies to ROM-data readers.
