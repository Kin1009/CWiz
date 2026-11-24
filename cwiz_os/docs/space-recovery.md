# Space recovery — shipping more verified conversions from the bank-3 blob

This record explains how the integrated ROM was given real slack without
editing a cwiz tree, a ledger, a packager, the stock ROM or git state.  Only
`Makefile`, this document and the new read-only `tools/space_report.py` were
changed; `main.cwiz` was deliberately left byte-identical so the four
keep_custom authorities and the SHIFT+7 / SHIFT+8 boot chords are untouched.

## 1. What the space problem actually was

The final-port build (`docs/final-port-integration.md`) was razor-thin in two
independent places:

| region | before | limit | headroom |
|---|---:|---:|---:|
| boot payload (`main.cwiz` + OS/apps/icons) | 31,496 B (8 B after a serial regeneration) | 31,502 B (`0:7B0E`, first protected key map) | 6–8 B |
| recovered linked/deep blob | 30,840 B | 61,824 B bank-3 gap shared with the idiomatic blob | — |
| idiomatic blob | 30,916 B | idem | — |
| combined `--blob` | 61,756 B | 61,824 B | **68 B** |

The 265 excluded conversions were excluded by the *combined blob* being full,
not by the payload.  A scan of the stock ROM confirms there is no third large
blank region: the only erased runs ≥64 B are bank 3 (61,824 B), `0:FC00..0:FDDF`
(480 B) and `0:FF02..0:FFEF` (238 B).  The two small bank-0 runs are below a
protected-region boundary and cannot host a relocated blob image, so a "third
blob region" is not safe; the recovery had to come from deduplication.

### Largest reducible costs (identified, not all acted on)

Boot payload (`tools/space_report.py` lists them with byte counts):

* `launcher/menu_icons.cwiz` — `rom8 char menu_icons[3456]`: **3,456 B**, by far
  the largest single table.  It is read by `launcher.cwiz::draw_icon` through
  `menu_icons[app*2+selected]` and by diagnostics' SUM.  Both consumers are
  outside this task's ownership, so the table cannot be compressed, moved to
  bank 3 or regenerated in a smaller form without silently changing the
  launcher art or the SUM.  **Left intact.**
* 12 menu labels + 12 app-name labels + diagnostics/serial strings in
  `launcher.cwiz` / `diagnostics`: each 10–33 B.  Removing or deduplicating them
  would change visible screens.  **Left intact.**
* `apps/recovered/recovered.cwiz` embedded diagnostic path strings (84/80 B)
  and the generated serial/version constants: part of the SHIFT+8 evidence
  screen and the four authorities.  **Left intact.**
* `store_serial_number()` in `main.cwiz` is already a 12-iteration `poke` loop;
  no reducible slack.

Blobs: the two images independently carry the same `cw` boot template and
runtime (~1.4 KB each, unavoidable without a toolchain change), and — more
importantly — **38 linked/deep modules define functions that also have a
curated idiomatic port in the idiomatic blob**.  Keeping both copies wasted
4,512 B.  This is the reducible cost that was acted on.

## 2. Safe recovery plan and tradeoffs

### 2.1 Deduplicate structural copies of converted functions (4,512 B freed)

`blob-source` now filters `RECOVERED_DUP_EXCLUDE`, a list of 38 `deep/unknownA`
files, out of the generated recovered source.  The excluded files are:

```
fw_09B90 fw_0A9DE fw_0AC60 fw_0AF1A fw_0BF12 fw_0BF22 fw_0C7B0 fw_0E5E6
fw_0EA4A fw_0ED88 fw_13AB4 fw_13ABC fw_13AC8 fw_13ACC fw_13AD4 fw_13B5A
fw_13B6E fw_13B86 fw_13B96 fw_143D6 fw_1443A fw_14442 fw_14448 fw_14450
fw_14458 fw_14460 fw_14468 fw_14470 fw_14476 fw_14BCC fw_153A6 fw_153B2
fw_153BC fw_153CE fw_17026 fw_17030 fw_17B16 fw_17B28   (all .cwiz)
```

Why this is safe and honest:

* Every dropped file's functions are still present, in higher-evidence form,
  in the idiomatic blob.  Four of them (`fw_0AC60`, `fw_0A9DE`, `fw_0E5E6`,
  `fw_0EA4A`) correspond to Tier-A tails with a real wizrun differential; the
  rest have a `proven` concrete-interpreter differential or first-wave
  instruction-mapping evidence.  We keep the better copy, not the worse one.
* Each dropped file was checked to define **only** duplicated functions; no
  other linked/deep module calls them.  `wizc` assembles the reduced source
  with no undefined-symbol error, and both relocation verifiers pass.
* The recovered blob remains a preserved artifact, not an in-place executable
  (see `docs/blob-relocation.md`); none of the four keep_custom authorities or
  the SHIFT+7/8 UI lives in it — they are all compiled into the boot payload.
* No cwiz tree was edited.  The filter is applied only to the Makefile-generated
  `build/recovered-blob-source.cwiz`.
* The two remaining cross-blob duplicates (`disable_key_filter`,
  `enable_key_filter`) are intentionally kept: their structural source files
  also define non-duplicated functions, so removing them would require editing
  a cwiz tree.  They cost a few bytes and are documented rather than hidden.

Measured effect: recovered blob **30,840 → 26,328 B (−4,512 B)**.

### 2.2 Reinvest the slack in higher-evidence conversions (9 modules added)

The freed 4,512 B were spent on nine flagged (`flags/`) modules ranked by the
read-only ledger's `analysis/idiomatic_flags.json → equivalence.vectors`.  All
nine are `proven` by the concrete-interpreter vs rendered-C differential and
compile clean; none is a sweep mismatch, an inconclusive case or a duplicate
definition.  Each one's byte cost was measured independently before selection.

| id | module | vectors | bytes |
|---|---|---:|---:|
| `f_108F2` | `math_helper_108F2.cwiz` | 8,088 | 600 |
| `f_127BC` | `math_helper_127BC.cwiz` | 8,088 | 370 |
| `f_127D8` | `math_helper_127D8.cwiz` | 8,088 | 492 |
| `f_17B5A` | `math_helper_17B5A.cwiz` | 8,088 | 456 |
| `f_10DFA` | `ui_10DFA.cwiz` | 8,088 | 1,340 |
| `f_17B48` | `ui_block_17B80_caller_17B48.cwiz` | 8,088 | 112 |
| `f_16C7C` | `ui_math_helper_helper_16C7C.cwiz` | 8,088 | 480 |
| `f_11980` | `ui_table_thunk_11980.cwiz` | 8,088 | 602 |
| `f_1624E` | `ui_block_16952_helper_1624E.cwiz` | 256 | 54 |

Total added: **4,506 B**, leaving 6 B of the 4,512 B recovered plus the original
68 B = **74 B of bank-3 headroom**.  `IDIOMATIC_SIZE_CAP` was raised from
30,920 to 35,432 B (61,824 − 26,328 − 64 B safety reserve).

Honest ranking note: the single highest-evidence excluded module,
`hub_1A9FC` (`f_1A9FC`, 8,866 vectors), compiles to **10,728 B** because it
embeds a table; it cannot fit even after deduplication, so it remains excluded.
Several other 8,088-vector modules with individual costs of 2,174–31,320 B
(`ui_2657E`, `math_19AFA`, `math_1A760`, `math_1D9AC`, `math_1BE8A`, …) were
also rejected by the byte budget even though their evidence ties the admitted
set.  The prior manifest's prose claimed a strict descending-vector policy but
its admitted set was not strictly ordered that way; this change re-ranks by the
ledger's actual vector count and records the byte-driven rejections here.

### 2.3 What was deliberately *not* done

* No icons/strings were moved to bank 3 and no third blob region was added:
  both would need bank-aware data reads (`DSRI #3`) or edits to `launcher.cwiz`
  / `diagnostics/`, which are outside ownership.  See blocker 3 below.
* No tier-D (frame-certificate-only) stackframe modules were added: their
  evidence is strictly lower than the `proven` flag modules that consumed the
  budget.
* The four sweep mismatches (`f_20DD2`, `f_26FFA`, `f_0D38E`, `f_2F5E8`), the
  three inconclusive cases (`f_1B818`, `f_1B83A`, `f_1B84E`) and the two bank-0
  duplicate definitions stay excluded exactly as before.
* `main.cwiz` was not edited at all: `store_serial_number`, `diagnostic_menu`,
  `app_launcher`, `clean_ram`/`wipe()` and the SHIFT+7/SHIFT+8 sampling are
  byte-identical to the final-port build.

## 3. Exact measurements, before and after

| item | before | after | delta |
|---|---:|---:|---:|
| boot payload | 31,496 B (6 B free)¹ | 31,494 B (8 B free)¹ | −2 B (serial regeneration only) |
| recovered blob | 30,840 B @ `3:0E40` | 26,328 B @ `3:0E40` | **−4,512 B** |
| idiomatic blob | 30,916 B @ `3:86B8` | 35,422 B @ `3:7518` | +4,506 B |
| combined blob | 61,756 B | 61,750 B | −6 B |
| bank-3 headroom | 68 B | **74 B** | +6 B |
| included conversions | 230 | **239** | **+9** |
| excluded conversions | 265 | 256 | −9 |
| ROM size | 262,144 B | 262,144 B | 0 |
| ROM checksum | `0x9E7D` | `0x1E9A` | changed with bytes |
| ROM SHA-256 | `6C1E68D9EB7D1595CD3A2DC317172EB2462FCC51A0C45318C4C4145BCBE95F61` | **`C857B24E779F04BF6E93BCA14C7B2D2251FBBB13B3A732AE3BBB861CEC77E78B`** | build-specific |

¹ `tools/generate_serial.py` draws a fresh random serial on every `make
integrated`, so payload bytes vary by ±2 and the ROM hash is build-specific.
The module selection, layout and blob sizes are deterministic.

Post-build verification (from `build/space-recovery-evidence/make-integrated.log`):

* relocation recovered: 1,485 branches + 23 data refs, PASS (4,501 bytes rewritten);
* relocation idiomatic: 1,588 branches + 9 data refs, PASS (4,782 bytes rewritten);
* each blob relocated at its **own** base before concatenation, exactly as before;
* `package_integrated.py --verify`: 262,144 B, checksum `0x1E9A`, payload matches,
  both blobs match at `0x30E40` / `0x37518`, placement within `3:0E40..3:FFBF`;
* RAM layout: 1,592 allocated refs, `0xD000..0xD111`, 3,265 B free, no overlap.

`build/final-manifest.json` was regenerated by `make integrated`: 239 included
(206 first-wave + 10 tails + 6 real-dynamic stackframes + **17** flag modules),
256 excluded.  Its read-only generator (`apps/recovered/gen_final_manifest.py`,
not edited) still hard-codes the old `30920 B / 30984 B` text in `policy.fit`;
the authoritative numbers are in `blobs` and `counts` above and in
`build/space-recovery-evidence/space-report.json`.

## 4. Emulator evidence (`build/space-recovery-evidence/`)

Headless CasioEmuMsvc via `casioctl`, same ROM SHA-256 as above, integrated model
directory `build/fx-580-cwiz-os-integrated-model`.  Boot chords are held with
`key matrix <ki> <ko> press` before `reset` (K_SHIFT = `(7,0)`, K_8 = `(2,1)`,
K_7 = `(2,0)`), then `resume`, 2 s settle, `status` + `regs` + `screenshot`,
then `key releaseall`.

| scenario | PC after 2 s | observed screen | evidence |
|---|---:|---|---|
| plain boot | 30,906 | launcher `1:Calculate` | `plain.png`, `plain_report.json` |
| SHIFT+8 recovered | 31,018 | `REC`, `KEY 1`, `RAW 12B 1`, `DSP 7` | `recovered.png`, `recovered_report.json` |
| SHIFT+7 diagnostics | 30,968 | `4+1=?`, `00/00`, `Press AC` | `diagnostics.png`, `diagnostics_report.json` |

The recovered screen still shows live `KEY`/`RAW`/`DSP` values from the
boot-payload idiomatic key path; diagnostics still wins the first boot-authority
check.  PC values depend on the 2 s settle window; the screens and ROM hash are
the authoritative evidence.  `summary.json` aggregates all three reports.

## 5. Reproduce

```sh
cd /Volumes/Code/Wiz/cwiz_os
make integrated
python3 tools/space_report.py --baseline build/space-recovery-evidence/baseline-manifest.json
python3 tools/space_report.py --baseline build/space-recovery-evidence/baseline-manifest.json --json
python3 -c 'import hashlib,pathlib; print(hashlib.sha256(pathlib.Path("build/cwiz-os-integrated.rom").read_bytes()).hexdigest().upper())'
```

The chord-capture helper used for §4 lives only in the scratch area
(`/Users/kin/Desktop/Wiz/temp/space-recovery/capture_evidence.py`) and is not a
repository file; its method is fully specified above.

## 6. Remaining blockers

1. **256 conversions still excluded.** 84 lower-evidence/large flag modules,
   163 frame-certificate-only stackframes, 2 bank-0 duplicate definitions,
   4 sweep mismatches and 3 inconclusive cases.  Exact reasons are in
   `build/final-manifest.json → excluded`.
2. **6–8 B payload headroom.**  The 3,456-B icon table cannot be reduced
   without editing `launcher.cwiz`/diagnostics, which are outside ownership;
   no payload-side module can be added.  This is unchanged from final-port.
3. **DSR for ROM-table readers.**  The relocator fixes data *offsets* but does
   not insert `DSRI #3`; a bank-aware loader/compiler change is still required
   before the relocated blobs execute in place.  See `docs/blob-relocation.md`.
4. **No third blob region.**  The only other blank stock-ROM runs are 480 B and
   238 B in bank 0, too small and not safely relocatable; moving OS data into
   bank 3 would need bank switching in the payload.
5. **Evidence limits.**  Flag equivalence remains a semantic-model differential,
   not a stock-ROM trace; the newly added modules inherit that limitation.  No
   new equivalence claim is made for the 38 structural copies that were dropped
   — their higher-evidence idiomatic twins are what ship.
