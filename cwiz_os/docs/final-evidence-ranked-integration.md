# Final evidence-ranked integration — shipping the best-evidence modules in the bank-3 budget

This is the last integration step.  All conversion waves are complete; the only
remaining decision was *which conversions to preserve* in the fixed 61,824-B
blank bank-3 flash gap.  The answer is now evidence-ranked: the strongest
dynamic-equivalence modules ship, the weaker static and certificate-only
modules are excluded with exact reasons in `build/final-manifest.json`.

Ownership was respected: only `cwiz_os/Makefile`, this document, the generated
`cwiz_os/build/final-manifest.json` and `build/final-ranked-evidence/**` were
changed.  `main.cwiz`, `apps/**`, `package_integrated.py`, the relocation tools,
every `firmware-re` file, every `cwiz` tree, the stock ROM and git state are
untouched.  No compiler change was needed (DSR support already landed as the
`cwreloc` target; see the blockers section for its remaining limit).

## 1. Evidence ranking across all converted modules

The ledgers describe **567 converted module records** (after path
normalisation).  They fall into one ranking, strongest first:

| rank | tier | ledger / source | evidence actually recorded | converted | shipped |
|---:|---|---|---|---:|---:|
| 1 | A1 | `idiomatic_tails.json` | real `wizrun --mode io` differential, 43/43 vectors equivalent, plus static proof | 10 | **10** |
| 2 | A2 | `idiomatic_stackframes.json` (real-dynamic subset) | real wizrun differential, 3/3 pairs each, plus frame certificate | 6 | **6** |
| 3 | A3 | `idiomatic_symbolic.json` (NEW) | real wizrun differential, **46/46** vectors equivalent after fixing 3 real bugs; 120,524 static proof vectors | 24 | **22** |
| 4 | A4 | `idiomatic_blflags.json` (NEW) | real `verify-io` differential, **52/52** vectors, explicit flag decoding (no flag model) | 6 | **6** |
| 5 | A5 | `idiomatic_loops.json` (NEW) | real wizrun differential, **44/44** vectors, static proof pass, zero `goto` | 7 | **7** |
| 6 | B | `idiomatic_flags.json` | exhaustive semantic-model vector equivalence (4,256–8,088 vectors per module), not a stock-ROM trace | 101 | **5** |
| 7 | D | `idiomatic_stackframes.json` (remainder) | frame certificate only (register save/restore), no dynamic differential | 163 | 0 |
| 8 | E | bank0 / bank1a / bank1b / bank2 / bank3key | first-wave instruction-mapping evidence (static proof) | 250 | 0 |

The task's ranking (`real wizrun/vector equivalence > exhaustive semantic-model
vectors > frame certificates > static proof`) is applied literally: tiers A1–A5
outrank B, which outranks D and E.  Within a tier the ordering is descending
executed-vector count, then ascending compiled byte cost, then path.  A sweep
mismatch or inconclusive dynamic case is never admitted.  The two third-party
duplicate key-line definitions stay filtered at source-list level; the
authoritative `bank3key/key_runtime.cwiz` leaves (raw `0:A162`/`0:A16A`,
154/154 dynamic key-primitive evidence) run in the boot payload.

## 2. What was added and what was removed

**Added — 35 of the 37 newly proven modules** (all real-dynamic tiers):

* `symbolic/`: 22 of 24 (all except `ui_2D314` and `ui_28350`);
* `blflags/`: all 6;
* `loops/`: all 7.

The two excluded symbolic modules are real-dynamic too, but their marginal
compiled costs are **15,130 B** (`ui_2D314`, `f_2D314`) and **7,236 B**
(`ui_28350`, `f_28350`).  Admitting either would displace more than twenty
smaller real-dynamic modules, so excluding them maximises the amount of
high-evidence code that ships.  Their ledger entries remain `proven`; only the
byte budget stopped them.  The shared `_symbolic_boundary.cwiz` (14 external-tail
ABI stubs, include-guarded) is emitted once ahead of the symbolic ports so no
name is defined twice — this is the source-list dedup the Makefile performs.

**Removed — 222 previously shipped modules** to make room:

* all **206** tier-E first-wave modules (bank0 57, bank1a 70, bank1b 44,
  bank2 35) — static instruction-mapping evidence, the weakest tier;
* **12 of the 17** tier-B flag modules (lower vector count / higher byte cost
  than the five retained flags).

Kept from the old set: 10 tails, 6 real-dynamic stackframes, and the 5
highest-vector flags (`ui_block_17B80_caller_17B48`, `math_15374`,
`math_15722`, `math_15730`, `ui_14258`).  The removed first-wave functions are
still present as structural copies in the recovered blob; the four UI
authorities and the SHIFT+7 / SHIFT+8 key path still run from `main.cwiz` and
the boot payload.  Every removed module is listed individually in
`build/final-manifest.json → excluded[]` with its id, tier, compiled size and
an exact reason.

Honesty note: the earlier space-recovery dedup removed 38 structural
linked/deep copies on the assumption that their higher-evidence idiomatic twins
would ship.  For any of those whose twin was a first-wave module now excluded,
the function is no longer present in either blob.  This is the direct cost of
ranking evidence above raw conversion coverage in a fixed 61,824-B gap; it is
not concealed by the manifest, which lists both the excluded twin and the
already-dropped structural copy's ledger.

### Exact shipped set

The idiomatic blob compiles **56 converted modules + 1 shared boundary stub**
(35,394 B).  Full per-module ids, evidence strings, standalone byte sizes and
marginal blob costs are in `build/final-manifest.json → included[]`; the
summary is:

| tier | count | marginal bytes (sum) |
|---|---:|---:|
| A1 tails | 10 | 1,940 |
| A2 real-dynamic stackframes | 6 | 1,468 |
| A3 symbolic (NEW) | 22 | 25,684 |
| A4 blflags (NEW) | 6 | 1,902 |
| A5 loops (NEW) | 7 | 1,526 |
| B flags | 5 | 910 |
| shared symbolic boundary stub | 1 | 476 |
| **sum of marginal costs** | **57** | **33,906** |

The marginal bytes are measured by compiling the final source list with one
module removed and subtracting — a real co-compiled cost, not a standalone
figure with a duplicated runtime template.  Their sum (33,906 B) is 1,488 B
below the 35,394-B blob size because the shared `cw` runtime template is counted
once in the image, not once per module.  The boundary stub's marginal cost is
measured against the tails+stackframes base (4,896 → 5,372 B) because removing
it from the final set makes the symbolic ports fail to compile.  `bytes` in the
manifest is the module's standalone `wizc` output size.

### Exclusions

`build/final-manifest.json → excluded[]` has **520** entries:

| group | count | reason |
|---|---:|---|
| tier E first-wave (bank0/1a/1b/2/b3key) | 250 | static proof ranks below every admitted tier; structural copy retained in recovered blob, bank3key authority in boot payload |
| tier D frame-certificate stackframes | 163 | frame certificate only, no dynamic differential; budget exhausted |
| tier B flags not admitted | 96 | lower vector count / larger byte cost than the five admitted flags |
| A3 symbolic too large | 2 | real-dynamic proven, but 7,236 / 15,130 B would displace 20+ smaller proven modules |
| cross-batch duplicate bank0 definitions | 2 | authoritative `key_runtime.cwiz` leaf in boot payload (154/154 vectors) |
| sweep mismatches (`f_20DD2`, `f_26FFA`, `f_0D38E`, `f_2F5E8`) | 4 | not equivalent in the sweep; never claimed |
| inconclusive dynamic (`f_1B818`, `f_1B83A`, `f_1B84E`) | 3 | structural reference never reached the `0xA5` harness end |

## 3. Build and ROM facts (`make integrated`)

| item | value |
|---|---|
| Boot payload | **31,496 / 31,502 B** — 6 B free (serial regeneration moves this by ±2) |
| Recovered blob | **26,328 B** at `0x30E40` (`3:0E40`) |
| Idiomatic blob | **35,394 B** at `0x37518` (`3:7518`), relocated at its own base |
| Combined `--blob` | **61,722 B**, **102 B** of the bank-3 gap still free |
| Relocation | recovered 1,485 branches + 23 data refs, PASS (4,501 B rewritten); idiomatic 1,848 branches + 22 data refs, PASS (5,588 B rewritten); each blob relocated separately before concatenation |
| `make verify-integrated` | 262,144 B, checksum `0x4C86`, payload and blob bytes re-verified, placement inside `3:0E40..3:FFBF` |
| RAM layout | 1,592 allocated refs, `0xD000..0xD111`, 3,265 B free, no overlap |
| ROM size | **262,144 B** |
| ROM SHA-256 | **`7603C6938F47C5CFABF42FD334E6FF4911E2D0E84BE11037D10C5D582856AED1`** |
| ROM checksum | **`0x4C86`** (`0x3FFF6..0x3FFF7`) |

Full logs: `build/final-ranked-evidence/make-integrated.log` and
`verify-integrated.log`.  The serial number is regenerated on every build, so
the hash is build-specific; module selection, order and sizes are deterministic.

## 4. Headless emulator evidence

`build/final-ranked-evidence/` holds plain, SHIFT+8 and SHIFT+7 captures from
CasioEmuMsvc driven by `casioctl` (reusing
`firmware-re/fx580/tools/emulator_regression.py`).  Boot chords are **held
before `reset`** (`K_SHIFT=(7,0)`, `K_8=(2,1)`, `K_7=(2,0)`), then `resume`, 2 s
settle, `status`, `regs`, `screenshot`, `key releaseall`.  Each report includes
the initial/final status and full registers.  The harness scratch script was
`/Users/kin/Desktop/Wiz/temp/final-ranked/capture_evidence.py` (deleted after
capture).

| scenario | PC after 2 s | observed screen | screenshot SHA-256 (first 16) |
|---|---:|---|---|
| plain boot | 31,018 | launcher `1:Calculate` | `00fec5ebc9d1368…` |
| SHIFT+8 recovered | 30,986 | `REC / KEY 1 / RAW 12B 1 / DSP 7` | `0a20860b6056c09f…` |
| SHIFT+7 diagnostics | 31,018 | `4+1=? / 00/00 / Press AC` | `4de52fe1269de40e…` |

The recovered screen still shows **real live values**: `KEY 1`, the two raw
matrix-scan bytes `12B 1`, and `DSP 7` from `display_helper_17094()`.  The
diagnostics screen still wins the first boot-authority check and is unchanged,
so diagnostics remains authoritative.  The independent
`firmware-re/fx580/tools/emulate_built_rom.py --model-dir` cross-check
(`build/final-ranked-evidence/tool-plain/`) reports PC 31,018 and the **same**
launcher screenshot SHA-256 as the casioctl plain run.  All three PNGs were
visually inspected after capture.

## 5. Remaining blockers

1. **DSR hand-written peek helpers.**  The `cwreloc` target emits `DSRI #3`
   before compiler-generated ROM loads (`rom`/`rom8` indexing, `_rpeek`), but
   ROM reads hidden inside hand-written runtime helpers that assume DSR 0
   (`peek()` → `__peek`) are unchanged.  A relocated blob that must read its
   *own* bank-3 table through such a helper still needs that helper made
   DSR-aware.  The relocator itself only patches in-place bytes (branches and
   data offsets); it cannot insert `DSRI` without shifting every later address.
   See `firmware-re/fx580/docs/dsr-bankfix.md` and `cwiz_os/docs/blob-relocation.md`.
2. **~1,000 structural functions remain.**  The registry records 1,604 unique
   function ids, of which 249 were converted and **1,355 remain blocked** —
   roughly a thousand structural bodies with no idiomatic port.  Only 56
   converted modules fit in this ROM; the other 511 converted-but-excluded
   records are documented in the manifest, and the structural copies of the
   dropped first-wave modules stay in the recovered blob.
3. **No real device.**  Every equivalence claim is against an independently
   written structural reference, a concrete/semantic interpreter, or the
   emulator; no fx-580VN X hardware measurement was made.  Flag equivalence is
   a semantic-model differential, not a stock-ROM trace; the tails and symbolic
   differentials are against structural references, not stock ROM.
4. **6 B payload headroom.**  The boot payload is 31,496/31,502 B; the 3,456-B
   icon table cannot be reduced without editing out-of-scope files, so no new
   payload feature can be added here.
5. **Excluded high-evidence modules.**  `ui_2D314` and `ui_28350` are proven but
   too large; `hub_1A9FC` (8,866 vectors, 10,728 B) and the lower-vector flags
   are excluded by the same byte budget.  They are recorded, not hidden.

## 6. Reproduce

```sh
cd /Volumes/Code/Wiz/cwiz_os
make integrated                 # payload + two blobs + relocation + package + verify + manifest
make verify-integrated           # standalone size/checksum/payload/blob re-check
python3 -c 'import hashlib,pathlib; p=pathlib.Path("build/cwiz-os-integrated.rom"); \
  print(hashlib.sha256(p.read_bytes()).hexdigest().upper())'
```

The manifest's `included[]`/`excluded[]` lists, module sizes, evidence strings
and reasons are the authoritative machine-readable record of this decision.
