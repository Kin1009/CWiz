# Stock-visible key-code contract in CWiz OS

Status: **implemented and measured**. The recovered app now publishes a real
scancode word through the stock firmware key globals `D150`/`D13A`, using the
recovered `scan_raw_keycode()` primitive. The differential tool
`firmware-re/fx580/tools/stock_equiv_key.py` still reports `differs` in 7/7
scenarios, because the untouched framebuffer/port/run-mode and code-address
fields dominate the verdict, but 5 captured fields changed from `differs` to
`identical` and the "zero `D150`/`D13A` writes" gap is closed on every boot
chord path that enters the recovered app.

This document is new. Only `cwiz_os/main.cwiz`,
`cwiz_os/apps/recovered/recovered.cwiz`, and this file were edited; no tool,
Makefile, other cwiz tree, compiler, ROM source, stock ROM, emulator, analysis
file or git state was touched.

---

## 1. What the stock contract is

Decoded in `firmware-re/fx580/docs/keys.md`, `getkey-deep.md` §2/§4 and
`rop-keymap.md`:

| firmware global | address | stock meaning |
| --- | --- | --- |
| `D150` | `0xD150` (2 bytes, little endian) | last raw scancode word `(1<<KI) \| ((1<<KO)<<8)` |
| `D13A` | `0xD13A` (1 byte) | bit 7 = key-wait active, bit 3 = long auto-repeat |
| `D139` | `0xD139` (1 byte) | commit / screen-buffer flag |

Stock `getscancode` (`1:F24C`, commit site `1:F390/F394`) writes the captured
word to `D150` and sets `D139 = 1`; its D13A bit helpers at `0:9FE0/0:9FF2/
0:9FF6/0:9FFE/0:A006` set and clear bits 7 and 3. The recorded memory watches
in the baseline see the stock D13A clears at PCs `0x9FFC` and `0x9A7A`.
A no-press scan leaves `D150` unchanged (`getkeycode`, `2:F5E8`).

The CWiz allocator currently ends at `0xD115`
(`tools/verify_ram_layout.py`: 1582 references, `0xD000..0xD115`), so these
firmware globals are **not** inside the CWiz variable window and can be
written safely.

## 2. What was implemented

### `cwiz_os/apps/recovered/recovered.cwiz`

* `recovered_publish_key(code_addr)` reconstructs the little-endian word from
  the two bytes written by `scan_raw_keycode()`, then performs the stock
  publish sequence at the exact stock addresses:

  ```
  poke(0xD150, word & 0xFF)
  poke(0xD151, (word >> 8) & 0xFF)
  poke(0xD13A, 0x88)     /* bit7 wait-active + bit3 long-repeat */
  poke(0xD13A, 0x00)     /* release, matching the settled stock state */
  ```

  The word is the **output of the recovered scan**, not an invented key code.
  `D139` is deliberately not written: its settled value is already `0x00` on
  both images, it is not a harness watch point, and the boot payload has only
  26 bytes of headroom.
* `recovered_scan_publish()` clears the 2-byte slot (the stale-slot rule in
  `getkey-deep.md` §2.3), calls the recovered `scan_raw_keycode()`, and only
  publishes when a key was actually captured.
* The recovered app's display still renders readable text; the former
  `REC`/`KEY`/`DSP` rows were reduced to one `RAW <word>` row to fit the fixed
  `0x7B0E` boot-payload budget (see §5).
* The `cvt_key` raw→FCxx step (`f_29892`, `2:9892`) is *not* a `D150` producer:
  `D150` holds the raw word, and the key map at `0:7B0E` stays in preserved
  ROM. Embedding a second map copy would itself overflow the payload. The
  harness derives the key ID from `0:7B0E` using the *captured* `D150` bytes,
  so a correct `D150` word also makes that derived field match.

### `cwiz_os/main.cwiz`

* One call to `recovered_scan_publish()` was added after `init()` and
  `store_serial_number()`, before the boot-chord dispatch. This puts the
  SHIFT / SHIFT+7 / SHIFT+8 chords into the stock-visible globals before the
  chosen app is entered.
* The four `keep_custom` authorities are unchanged: `store_serial_number()`,
  the SHIFT+7 `diagnostic_menu()` dispatch, the `app_launcher()` dispatch, and
  the runtime `wipe()` builtin. SHIFT+7 still enters diagnostics and SHIFT+8
  still enters the recovered app.

---

## 3. Build verification (`make integrated`)

| item | value |
| --- | --- |
| boot payload | 31,476 B (`0x7AF4`) / 31,502 B (`0x7B0E`) budget, 26 B free |
| recovered blob | 61,722 B at `3:0E40`, ends `3:FF59` (102 B gap left) |
| relocation | PASS (recovered 1,485 branches / 23 data refs / 4,501 B changed; idiomatic 1,848 / 22 / 5,588 B) |
| integrated ROM | 262,144 B |
| fx-580 checksum at `0x3FFF6` | `0x7F76` (bytes `76 7F`), recomputed by `package_integrated.py --verify` |
| RAM layout | OK, `0xD000..0xD115`, no reserved-span overlap |
| ROM SHA-256 | `E97F38792E0F1F1C2BF31E56DE52DAE11AE20D20E2D327687A60419B5BAFD639` |

Previous (pre-change) ROM SHA-256 was
`7603C6938F47C5CFABF42FD334E6FF4911E2D0E84BE11037D10C5D582856AED1`.
Note: `make integrated` regenerates the serial with `secrets.token_hex(4)`
(`tools/generate_serial.py`), so each build has a different ROM hash and
checksum even with identical sources. The hash above is the exact ROM that
the AFTER differential run in §4 exercised; a later rebuild changes only the
serial bytes, not the key-contract code.

## 4. Differential evidence (live headless CtlServer)

BEFORE report: `firmware-re/fx580/analysis/stock_equiv_key.json`, generated
`2026-11-20T12:35:53+00:00`, custom ROM `7603c693…aed1`.
AFTER report: `/Users/kin/Desktop/Wiz/temp/key-contract/after.json`, generated
`2026-11-20T14:23:49+00:00`, custom ROM `e97f3879…fd639` (same harness SHA,
same normalised `ram.dmp` source, same app `2c21f5e`).

Both runs: 14/14 scenarios `pass` and `captured`, 0 fail, 0 blocked.
Verdict counts unchanged: **0 `identical-captured-fields`, 7 `differs`,
0 `inconclusive`**. Field-level, of 91 compared fields the number that
matched rose from **30/91 to 33/91** (5 gained, 2 lost).

### 4.1 Key-code contract fields, before → after

| scenario | stock `D150` | custom `D150` BEFORE | custom `D150` AFTER | key ID derived from captured `D150` AFTER |
| --- | --- | --- | --- | --- |
| idle | `0101` | `0101` (same) | `0101` (same) | `0x0031` (same) |
| key_1 | `0101` | `0101` (same) | `0101` (same) | `0x0031` (same) |
| key_arrow_up | `8004` | `0101` (diff) | `0101` (diff) | `0x0031` vs `0xFC1C` (diff) |
| key_ac | `0410` | `0101` (diff) | `0101` (diff) | `0x0031` vs `0xFC26` (diff) |
| shift7_entry | `8001` | `0101` (diff) | `8401` (diff) | `0xFC29` (**gained**) |
| shift8_entry | `8001` | `0101` (diff) | `8001` (**gained**) | `0xFC29` (**gained**) |
| exec_bp_probe | `8001` | `0101` (diff) | `8001` (**gained**) | `0xFC29` (**gained**) |

Custom recorded writes (BEFORE → AFTER):

| scenario | `D150` writes | `D13A` writes | custom commit/watch PC |
| --- | --- | --- | --- |
| all 7 | 0 → 0 (idle, key_1, arrow_up, key_ac, exec_bp) | 0 → 0 | — |
| shift7_entry | 0 → **1** | 0 → **1** | `0x7928` (inside `recovered_publish_key`) |
| shift8_entry | 0 → **1** | 0 → **1** | `0x7928` |
| exec_bp_probe | 0 → 0, but final `D150` changed `0101`→`8001` during boot (the exec BP perturbs timing, as in stock) | 0 | no commit captured |

`D13A_raw` and `D139_raw` are `0x0` on both images in every scenario, before
and after; the custom now *reaches* the D13A write path but its final value is
still `0x00`.

Two fields regressed in `shift8_entry`: `D150_keycode_commits` and
`mem_watch_PCs[D150-keycode-commit]` were both empty on each side BEFORE and
are now custom-only (`1` hit at `0x7928`), so they are honestly reported as
`differs` AFTER. This does not change the scenario verdict.

### 4.2 Fields still differing (all 7 scenarios)

* `final_pc` — stock `40826` (`0x9F7A`, STOP loop); custom `~31000`
  (payload loop). The custom OS never enters CPU STOP.
* `final_run_mode` — stock `stop`, custom `run`.
* `ports_F040_F046_raw` — stock `ff00ff0080037f00` (settled all-columns latch);
  custom `ff0000008003…` with a live one-hot `F046` sample.
* `mem_watch_PCs[F040-KI-read]` / `[F046-KO-write]` — stock preserved leaves
  (`0xA168/0xE83E/…`), custom payload/runtime addresses (`0x78B0/0x7908/…`).
  Reached via the CWiz runtime key builtins, which live in the compiler
  runtime and are out of scope here.
* `mem_watch_PCs[D13A-status-write]` — stock `0x9FFC/0x9A7A`, custom
  `0x7928`.
* `exec_bp_hit_addresses` — stock reaches `0x1F24E`, `0x2F5EA`; custom reaches
  none (it runs the payload-resident recovered scan).
* `D150_keycode_commits` for key_1/arrow_up/key_ac/shift7 — PC/LR differ
  (`0x1F394`/`0x23C44` vs `0x7928`); for arrow_up/key_ac the custom word is
  still `0x0101` (see §5).
* Screenshot hashes (corroborating only) differ in all scenarios; e.g. AFTER
  `shift8_entry` custom = `876169ffa231bf10…`, stock =
  `4300b90dc8e3087f3…`.

### 4.3 Screenshots

Fresh headless screenshots were captured with `--keep-scratch` and the two
representative frames retained under
`/Users/kin/Desktop/Wiz/temp/key-contract/screenshots/`:
`recovered-shift8.png` shows a readable `RAW 384` (decimal 384 = `0x0180`,
SHIFT) on the recovered-app screen; `launcher-idle.png` shows the untouched
4×2 launcher grid. The bulky harness run trees and trial artifacts were
deleted after capture; `after.json` is kept as the report evidence.

---

## 5. Unavoidable divergences

1. **Mid-run launcher key presses are not published.** In `key_1`,
   `key_arrow_up` and `key_ac` the matrix key is injected *after* boot, while
   `app_launcher()` owns the key loop. Its `key_hit()`/`key()` calls are CWiz
   compiler runtime builtins (`compiler/runtime.asm`), which are outside this
   task's ownership and cannot be hooked from `main.cwiz`. Making those
   scenarios publish `D150` would require replacing `app_launcher()` (a
   `keep_custom` authority) or editing the compiler runtime, both forbidden.
   The contractual write is therefore implemented on the recovered raw-scan
   path (boot chord + recovered app) only.
2. **Commit PCs cannot match.** Stock commits from preserved bank routines
   (`0x1F394`, `0x23C44`) and clears D13A at `0x9FFC`/`0x9A7A`. The custom
   code is a bank-0 boot payload and commits at `0x7928`; matching the PCs
   would require executing the preserved banked routines, which the CWiz call
   ABI cannot reach from the payload.
3. **RUN mode / final PC / port latch.** The stock image settles in CPU STOP
   with `F046 = 0x7F`; CWiz OS intentionally keeps running its UI loop and
   leaves a live KO strobe sample. Changing that is a display/runtime design
   decision, not part of the key-code contract.
4. **SHIFT+7 chord word.** The recovered scan sees both F1 (KI7/KO0) and 7
   (KI2/KO0) in the same column and publishes `0x0184`; stock publishes
   `0x0180`. Both decode through the `0:7B0E` map to `0xFC29` (SHIFT), which is
   why `expected_key_id_from_stock_map` matches even though the raw word does
   not. Forcing `0x0180` would mean discarding a pressed row, i.e. inventing a
   key code — explicitly not done.
5. **UI rows reduced.** The boot payload has a hard 31,502-byte ceiling and
   started with only 6 bytes free. Adding the publish path required removing
   the `display_helper_17094()` include and the `REC`/`KEY`/`DSP` rows; the
   single `RAW` row remains readable and shows the real scancode word.
6. **No `D139` write.** Its settled value already matches (`0x0`) on both
   images and adding two pokes would exceed the payload budget.
7. **FR-09 stays OPEN.** As the harness itself states, matching captured
   fields in a small host-driven matrix is not full keyboard equivalence.
   Cycle-exact debounce/auto-repeat and M8 real-device acceptance remain
   outside this change.

---

## 6. Files changed

* `cwiz_os/main.cwiz` — one `recovered_scan_publish()` call after init/serial,
  before the SHIFT+7 / SHIFT+8 dispatch.
* `cwiz_os/apps/recovered/recovered.cwiz` — stock D150/D13A publish helpers,
  recovered-scan integration, single-row RAW display.
* `cwiz_os/docs/key-contract.md` — this document (new).

Generated build outputs under `cwiz_os/build/` were refreshed by
`make integrated`; no source outside the three files above was edited.
