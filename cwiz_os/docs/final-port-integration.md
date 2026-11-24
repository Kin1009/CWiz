# Final port integration — flags / stackframes / tails into the integrated ROM

This is the last integration step of the proper port.  All conversion waves are
complete upstream (531 idiomatic functions, all clean under
`tools/check_c_like.py` with zero violations):

| wave | converted functions | ledger |
|---|---:|---|
| bank0 / bank1a / bank1b / bank2 / bank3key | 251 + 5 data items | `analysis/idiomatic_registry.json` |
| `tails/` | 10 | `analysis/idiomatic_tails.json` |
| `stackframes/` | 169 | `analysis/idiomatic_stackframes.json` |
| `flags/` | 101 | `analysis/idiomatic_flags.json` |

Only `cwiz_os/Makefile`, `cwiz_os/apps/recovered/**`, this document and the
generated `cwiz_os/build/final-manifest.json` were changed.  No cwiz tree,
ledger, tool, ROM source, `firmware-re` analysis file or git state was touched.

## 1. Deterministic selection policy

The bank-3 blank flash gap is `3:0E40..3:FFBF` = 61,824 B.  The recovered
linked/deep blob occupies 30,840 B of it, so the absolute idiomatic-blob ceiling
is **30,984 B**.  The Makefile uses `IDIOMATIC_SIZE_CAP = 30920`, i.e. it stops
64 B short of the ceiling so the placement is not razor-thin.

Tiers, in strict priority order (recorded machine-readably in
`build/final-manifest.json → policy`):

1. **A — real execution, `tails/`**: all 10 converted modules, each with a real
   `verify_idiomatic.py --mode io` differential — **43/43 vectors equivalent**,
   0 not-equivalent, plus a passing static instruction-level proof.
2. **B — real execution, `stackframes/`**: the 6 converted modules that have a
   real wizrun differential — **3/3 pairs equivalent each** (18 equivalent pairs
   total) *and* a passing frame certificate.  The 3 functions whose structural
   reference never reached the `0xA5` end magic were demoted to `blocked`
   upstream and are never candidates.
3. **C — executed vector equivalence, `flags/`**: modules ranked by descending
   executed equivalence-vector count, then path.  Every admitted module is
   `proven` by 4,256 or 8,088 concrete-interpreter vs rendered-C vectors and
   compiles clean.  (The flag wave's 492,953 vectors are semantic-model
   differentials, not stock-ROM traces; the docs say so explicitly.)
4. **D — frame certificates only**: the remaining 163 converted stackframes
   modules.  Lower evidence than tiers A–C and there is no budget left.
   Several of them also carry duplicate helper names (`num_*`) that prevent
   them sharing one translation unit — an additional reason they stay as
   individually compiled artifacts.

Within A/B/D the module path is lexicographically ordered; in C it is
vector-count-descending then path.  A module is admitted only while the
cumulative compiled blob stays at or under the cap.  Any module with a sweep
mismatch or inconclusive dynamic verification is excluded outright (none of the
tiers A–C candidates has one — the sweep's 4 non-equivalent results are all
first-wave b2/b3key, and the 3 inconclusive stackframes functions are blocked).

### Cross-batch duplicate dedup

`drive_all_key_lines` (raw `0:A162`) and `zero_key_lines` (raw `0:A16A`) are
defined both in `bank0/` (static-only `void` copies) and in
`bank3key/key_runtime.cwiz` (`int`-returning leaves with the real-call ABI and
154/154 dynamic key-primitive evidence).  The bank3key definitions are
authoritative and are compiled **into the boot payload** by
`apps/recovered/recovered.cwiz → key_getkeycode.cwiz → key_runtime.cwiz`.
Because the cwiz trees may not be edited, the Makefile filters the two losing
bank-0 files out of the generated blob source (`B0_EXCLUDE`).  No other bank-0
module references them, so the blob still compiles; this also removes 50 B of
duplicated machine code.

## 2. Exact module manifest

The generated `build/idiomatic-blob-source.cwiz` compiles **230 modules**: 206
first-wave modules (bank0/bank1a/bank1b/bank2, after the two dedup exclusions)
plus the 24 new modules below.  `bank3key` as a whole is deliberately *not*
compiled into the idiomatic blob; only the UI-critical key path runs in the boot
payload.

| # | tier | raw id | module | evidence |
|---:|---|---|---|---|
| 1 | A | `f_0AC60` | `tails/buffer_backscan_high_byte.cwiz` | real differential 4/4 |
| 2 | A | `f_0EA4A` | `tails/firmware_char_decode_16.cwiz` | real differential 3/3 |
| 3 | A | `f_0E5E6` | `tails/firmware_string_cell_count.cwiz` | real differential 4/4 |
| 4 | A | `f_0C1D0` | `tails/math_0C1D0.cwiz` | real differential 3/3 |
| 5 | A | `f_16DA2` | `tails/math_16DA2.cwiz` | real differential 6/6 |
| 6 | A | `f_0A9DE` | `tails/mode_dependent_char_code.cwiz` | real differential 5/5 |
| 7 | A | `f_0CB0E` | `tails/system_helper_0CB0E.cwiz` | real differential 1/1 |
| 8 | A | `f_0C784` | `tails/ui_0C784.cwiz` | real differential 5/5 |
| 9 | A | `f_09CC4` | `tails/ui_mode_code_resolver.cwiz` | real differential 6/6 |
| 10 | A | `f_0D80C` | `tails/xbank_helper_0D80C.cwiz` | real differential 6/6 |
| 11 | B | `f_0AAD4` | `stackframes/display_0AAD4.cwiz` | real differential 3/3 + frame certificate |
| 12 | B | `f_19F7E` | `stackframes/math_19F7E.cwiz` | real differential 3/3 + frame certificate |
| 13 | B | `f_1B4AC` | `stackframes/math_1B4AC.cwiz` | real differential 3/3 + frame certificate |
| 14 | B | `f_1B708` | `stackframes/ui_1B708.cwiz` | real differential 3/3 + frame certificate |
| 15 | B | `f_1B71C` | `stackframes/ui_1B71C.cwiz` | real differential 3/3 + frame certificate |
| 16 | B | `f_2B8DE` | `stackframes/bcd_17_access_2B8DE.cwiz` | real differential 3/3 + frame certificate |
| 17 | C | `f_170E6` | `flags/common_user_170E6.cwiz` | proven, 8,088 vectors |
| 18 | C | `f_1675A` | `flags/display_helper_1675A.cwiz` | proven, 8,088 vectors |
| 19 | C | `f_15374` | `flags/math_15374.cwiz` | proven, 8,088 vectors |
| 20 | C | `f_15722` | `flags/math_15722.cwiz` | proven, 8,088 vectors |
| 21 | C | `f_15730` | `flags/math_15730.cwiz` | proven, 8,088 vectors |
| 22 | C | `f_16368` | `flags/math_16368.cwiz` | proven, 8,088 vectors |
| 23 | C | `f_19CB6` | `flags/math_19CB6.cwiz` | proven, 8,088 vectors |
| 24 | C | `f_14BF0` | `flags/mem_14BF0.cwiz` | proven, 4,256 vectors |

Paths are relative to `firmware-re/fx580/cwiz/idiomatic/`.

### Exclusions (all recorded, none silent)

`build/final-manifest.json → excluded` has 265 entries with an exact reason each:

| group | count | reason |
|---|---:|---|
| `flags/` not admitted | 93 | lower vector-count than admitted tier-C modules; 30,920 B cap reached |
| `stackframes/` converted, no real differential | 163 | tier D ranks below real-execution/vector tiers; gap exhausted (and some share duplicate helper names) |
| bank0 `drive_all_key_lines`, `zero_key_lines` | 2 | cross-batch duplicate; authoritative bank3key/key_runtime.cwiz in boot payload |
| sweep mismatch (`f_20DD2`, `f_26FFA`, `f_0D38E`, `f_2F5E8`) | 4 | not-equivalent/error in the first-wave sweep; not claimed |
| inconclusive dynamic (`f_1B818`, `f_1B83A`, `f_1B84E`) | 3 | structural reference never reached the 0xA5 harness end; demoted to blocked |

The 250 tails-blocked, 163 flags-blocked and 334 stackframes-blocked functions
remain structural; their per-function evidence is in the respective ledgers.

## 3. Build and ROM facts (`make integrated`)

| item | value |
|---|---|
| Boot payload | 31,496 / 31,502 B — **6 B free** |
| Recovered blob | 30,840 B at `0x30E40` (`3:0E40`) |
| Idiomatic blob | 30,916 B, relocated at its **own** base `0x386B8` (`3:86B8`) |
| Combined `--blob` | 61,756 B; 68 B of bank-3 gap still unused |
| Relocation | recovered: 1,664 branches + 19 data refs, PASS; idiomatic: 1,377 branches + 4 data refs, PASS (each blob relocated separately before concatenation) |
| ROM size | 262,144 B |
| ROM SHA-256 | `6C1E68D9EB7D1595CD3A2DC317172EB2462FCC51A0C45318C4C4145BCBE95F61` |
| ROM checksum | `0x9E7D` (`0x3FFF6..0x3FFF7`) |
| RAM layout | OK, 1,592 allocated refs, 0xD000..0xD111, 3,265 B free |

The full build log is `build/final-evidence/make-integrated.log`; the packager
and relocation checks are in `build/final-evidence/packager-verify.txt`.
`tools/generate_serial.py` draws a fresh random serial on every `make integrated`,
so the ROM hash is build-specific; the module selection, layout and sizes above
are deterministic, and all evidence below was captured against this exact ROM
(the same hash is recorded in `final-manifest.json → rom/evidence.rom_sha256`).

## 4. Headless emulator evidence

Booted in CasioEmuMsvc through `casioctl`.  Each scenario used a fresh paused
session, `reset` + `resume`, a 2 s settle, then `status`, `regs` and
`screenshot`.  The provided
`firmware-re/fx580/tools/emulate_built_rom.py --model-dir` was also run for a
plain-boot cross-check (`build/final-evidence/tool-plain/`), reporting PC 30924
and the same launcher screenshot as the plain scenario below.

The two boot chords must be **held before reset**; `main.cwiz` samples them only
during boot.  `casioctl key hold` handles one key at a time, so a small scratch
script (`/Users/kin/Desktop/Wiz/temp/final-port/capture_final_evidence.py`)
pressed both matrix positions with `key matrix <ki> <ko> press` before reset and
called `key releaseall` after capture.  Positions come from the CWiz scan ABI
`key_down(k)`: `KO = 0x80>>(k>>3)`, `KI = 0x80>>(k&7)`; for `K_SHIFT=0x38` that
is `(ki=7, ko=0)`, `K_8=0x35` → `(2,1)`, `K_7=0x3D` → `(2,0)`.  The script and
all emulator scratch lived under `/Users/kin/Desktop/Wiz/temp/final-port/` and
were removed after the evidence was captured.

| scenario | held keys | PC after reset → final | observed screen | evidence |
|---|---|---|---|---|
| plain boot | none | 220 → 30930 | launcher `1:Calculate` | `final-evidence/plain.png`, `plain_report.json` |
| SHIFT+8 recovered | `matrix:7,0` + `matrix:2,1` | 220 → 31018 | `REC`, `KEY 1`, `RAW 12B 1`, `DSP 7` — real scan bytes and `display_helper_17094()` = 7 | `final-evidence/recovered.png`, `recovered_report.json` |
| SHIFT+7 diagnostics | `matrix:7,0` + `matrix:2,0` | 220 → 13686 | `4+1=?`, `00/00`, `Press AC` — diagnostics still the first boot authority | `final-evidence/diagnostics.png`, `diagnostics_report.json` |

Final PC values depend on the 2 s settle window (the CPU runs continuously);
the screens and the recorded ROM hash are the authoritative evidence.  All
three reports include `regs` and the initial/final `status`.

The recovered screen showing concrete `KEY`/`RAW`/`DSP` values confirms the
boot-payload idiomatic key path still executes after the two larger blobs were
added; the diagnostics screen confirms it was not displaced.

## 5. Honest remaining blockers

1. **DSR for ROM-table readers.** The relocator fixes data *offsets* but cannot
   insert `DSRI #3` prefixes (that would shift every following address).
   Functions touching only registers, RAM (DSR 0) and SFRs execute in place;
   ROM-table readers still need a compiler/linker change emitting `DSRI #3` for
   the `cw` target, or a `cwapp`-style rebuild.  The relocated blobs are
   preserved artifacts with verified branches/data offsets, not a complete
   bank-switching loader.  See `docs/blob-relocation.md`.
2. **6-byte payload headroom.** The boot payload is 31,496/31,502 B.  Any new
   UI feature must first free space in `main.cwiz` or move to the preserved
   blob; the 68 B left in the bank-3 gap is not a substitute for payload space.
3. **Functions left structural.** 250 tails, 163 flags and 334 stackframes
   blocked functions (plus the first-wave blockers and the static-only bank3key
   leaves not used by the recovered app) have no idiomatic equivalent.  The
   163 frame-certificate-only conversions and 93 lower-vector flag conversions
   exist as compiled modules but do not fit in the ROM; all exclusions with
   reasons are in the manifest.
4. **Evidence limits.** Flag equivalence is a semantic-model differential
   (not a stock-ROM trace); tails/stackframe differentials compare against an
   independently written structural reference, not stock ROM.  The four sweep
   mismatches and three inconclusive cases are recorded, not claimed.

## 6. Reproduce

```sh
cd /Volumes/Code/Wiz/cwiz_os
make integrated                 # builds ROM + final-manifest.json
make final-manifest              # regenerate build facts (preserves emulator block)
python3 -c 'import hashlib,pathlib; p=pathlib.Path("build/cwiz-os-integrated.rom"); print(hashlib.sha256(p.read_bytes()).hexdigest().upper())'
```
