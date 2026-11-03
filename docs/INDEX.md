# Documentation index

Status: active
Last verified against source: 2026-11-03

One line per document, with a status:

- **Current** — matches the code or is the active design/spec.
- **Stale** — contains statements that no longer match the code; read with the note.
- **Historical** — kept for context; superseded or describing an older snapshot.

## How paths work in this file

- Wiz-repo documents are linked relatively; they all live in `docs/`.
- The **emulator application** lives in a separate checkout, referred to below as
  `$CASIOEMU` (default: a sibling of this repo, `<workspace>/CasioEmuMsvc-Mac`).
  Its documents are listed as code paths rather than links because they are
  outside this repository. Set `CASIOEMU=/path/to/CasioEmuMsvc-Mac` if yours is
  elsewhere.
- `emulator/` in this repo is a **vendored build snapshot** (git-ignored, see
  `.gitignore`). It is not the development tree: it predates the plugin
  registry. Two emulator documents exist only in that snapshot (`docs/plan.md`,
  `docs/DEBUGGER-UI.md`) and are marked as such below.

The plugin author track owns `PLUGIN-GUIDE.md`, `plugins/TROUBLESHOOTING.md`,
`plugins/COMPONENTS.md` and `plugins/STATUS.md`. `plugins/STATUS.md` is the
canonical answer for "is this plugin promise implemented?"; other files defer to
it.

---

## 1. Wiz docs (`docs/`, 26 `.md` + 3 `.txt` + 2 `.rtf`)

| Document | Status | What it is | Notes |
| --- | --- | --- | --- |
| [INDEX.md](INDEX.md) | **Current** | This file | Statuses below were re-derived from source, not from `PLAN.md`. |
| [PLAN.md](PLAN.md) | **Current** | Plan of record: status, evidence, milestones | Plugin rows were stale against the host; §2.11 now defers to `plugins/STATUS.md`. Shell/editor phases are unchanged. |
| [COMPONENTS.md](COMPONENTS.md) | **Current** | How the preview app works, component by component | Companion to this index; paths resolve under `$CASIOEMU`. Plugin gap list reconciled 2026-11-03. |
| [PREVIEW.md](PREVIEW.md) | **Stale** | Product decisions, shell/plugin phases 0–5 | §2 still says plugin loading is "compiled out on macOS"; the loader ships (`PluginMan.cpp` Apple branch). Phase 1 is mostly done — see `plugins/STATUS.md`. |
| [UI_REWRITE.md](UI_REWRITE.md) | **Current** (design) | Target shell layout, settings schema, visual system | Design source with an inline implementation-override note; not a description of shipped code. |
| [SHELL-REVAMP.md](SHELL-REVAMP.md) | **Current** | Shell implementation record for the preview branch | Paths parameterized (`$CASIOEMU`, repo-relative `preview/`, `temp/`). |
| [SHELL-FIXES.md](SHELL-FIXES.md) | **Historical** | Implementation notes for legacy items 10.3–10.5 and the 3.1 rename | The fixes landed and are restated in `PLAN.md`; kept as the work log. |
| [SESSION_TABS.md](SESSION_TABS.md) | **Historical** | Early session-tab sketch | Header marks it unimplemented; superseded by `PREVIEW.md` §3–4 and `PLAN.md` 5.1–5.4. |
| [PLUGIN_SPEC.md](PLUGIN_SPEC.md) | **Current** | Frozen `.plugin` v1 contract | §8 discovery and §9 example layout corrected 2026-11-03 to match `PluginRegistry.cpp:143-145` and `examples/plugins/`. |
| [PLUGIN-GUIDE.md](PLUGIN-GUIDE.md) | **Current** (owned elsewhere) | Plugin author guide | Verified against the host; §11 is the security/ABI limit list. |
| [plugins/STATUS.md](plugins/STATUS.md) | **Current** (owned elsewhere) | Plugin platform: promised vs implemented | **Canonical** plugin status. Verified against `Plugin/PluginPermissions.cpp`, `PluginRegistry.cpp:132,290,657`, `PluginPackage.cpp:186,224`, `PluginVersion.h`. |
| [plugins/COMPONENTS.md](plugins/COMPONENTS.md) | **Current** (owned elsewhere) | Plugin host internals and structural gaps | §9 is the remaining host gap list (`IPluginShell`, hook lifetime, no sandbox). |
| [plugins/TROUBLESHOOTING.md](plugins/TROUBLESHOOTING.md) | **Current** (owned elsewhere) | Plugin failure modes, causes and fixes | Includes the Install Plugin flow (§17) and unload/library-retained states (§18). |
| [plugins/EXAMPLES.md](plugins/EXAMPLES.md) | **Current** | Five example packages, build/install steps | Install paragraph corrected 2026-11-03; dylibs are universal arm64+x86_64 (`file $CASIOEMU/examples/plugins/*/lib/*.dylib`). |
| [ROP.md](ROP.md) | **Current** | ROP payload syntax, ABI, launchers, key tables | Key-table paragraph matches `compiler/src/rop.cpp:1021-1075` (zeroed inline slot, no `di,rt`). Address grammar and the keymap reference corrected 2026-11-03. |
| [EDITOR.md](EDITOR.md) | **Current** (design) | Directive/target/language table, planned autocomplete | Header states 1.1/1.2/1.4 are not implemented and the editor still uses the C++ definition. Target list corrected 2026-11-03. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | **Current** | Compiler/assembler/simulator pipeline | Matches `compiler/src` and `compiler/runtime.asm`; driver count corrected 2026-11-03. |
| [WIZ-COMPILER.md](WIZ-COMPILER.md) | **Current** | Compiler internals, diagnostics, test map | `goto` row corrected 2026-11-03 (`cwiz.cpp:1418`, covered by `tests/test_cwiz.cpp`). |
| [C-CONFORMANCE.md](C-CONFORMANCE.md) | **Current** | CWiz C23 conformance matrix | Each row names its proving test. |
| [WIZ-UPSTREAM.md](WIZ-UPSTREAM.md) | **Current** | Comparison with the Python reference (pkdev-en/Wiz) | — |
| [DIRECTIVES.md](DIRECTIVES.md) | **Current** | Canonical directive and custom-type grammar | Shared by `wizc`, `wizlint`, the ROP backend and the decompiler (`compiler/src/directives.cpp`). |
| [GUIDE.md](GUIDE.md) | **Current** | Wiz/CWiz language guide | Example paths and sample output corrected 2026-11-03 against the current `examples/` layout. |
| [API.md](API.md) | **Current** | Built-ins, memory map, targets, C++ API, assembler, simulator | Tool list, `wizlint`, `ticks`/`delay_ms`, `fwnx` and the two README links corrected 2026-11-03. |
| [RUNNING.md](RUNNING.md) | **Current** | Build/run the toolchain, simulator, emulator, firmware patch | Example paths corrected; records the current `wizrun` 880 default-model caveat. |
| [STATUS_ICONS.md](STATUS_ICONS.md) | **Current** | LCD status icon layouts and `setStatus` contract | Matches the runtime status/`renderStatus` implementation. |
| [CLI-DEBUGGER.md](CLI-DEBUGGER.md) | **Current** | `casioctl` CLI debugger and control channel | Describes an **emulator** tool (`$CASIOEMU/tools/casioctl`); scratch paths parameterized 2026-11-03. |
| [rop-notes/rop-580-guide.txt](rop-notes/rop-580-guide.txt) | **Historical** | Community ROP guide (Vietnamese, UTF-8) | Source material, not project documentation. |
| [rop-notes/rop-basics.txt](rop-notes/rop-basics.txt) | **Historical** | Community ROP basics (Vietnamese, UTF-8) | Same. |
| [rop-notes/rop-ez.txt](rop-notes/rop-ez.txt) | **Historical** | Short community ROP cheat sheet (Vietnamese, UTF-8) | Same. |
| [rop-notes/rop-tap-1.rtf](rop-notes/rop-tap-1.rtf) | **Historical** | Community notes, legacy macOS rich-text export | **Binary container, not plain text and not valid RTF** (`{\rtf` never appears). Renamed from `.txt` on 2026-11-03 so editors stop treating it as UTF-8; it has no cleanly recoverable prose (2084 locale markers, no body text). Open with a rich-text/attributed-text viewer. |
| [rop-notes/rop-tutorial.rtf](rop-notes/rop-tutorial.rtf) | **Historical** | Community ROP tutorial (English), legacy macOS rich-text export | **Binary container, not valid RTF.** Renamed from `.txt` on 2026-11-03. It holds prose split into per-language runs (`en-US`, `pt-BR`) plus embedded font and certificate blobs; source material only, excluded from text search. |

---

## 2. Emulator docs (`$CASIOEMU`)

Not part of this repository; listed so the cross-references in `PLAN.md`,
`COMPONENTS.md` and the plugin docs resolve. `$CASIOEMU` defaults to the sibling
`CasioEmuMsvc-Mac` checkout.

| Document | Status | What it is | Notes |
| --- | --- | --- | --- |
| `$CASIOEMU/BUILD-MACOS.md` | **Current** | macOS build recipe and source layout | Build entry point. |
| `$CASIOEMU/README.md` | **Current** | Fork overview: macOS build, toolchain, ROP, themes | Describes the fork, not the preview shell (`PLAN.md` 8.6). |
| `$CASIOEMU/SNAPSHOT.md` | **Historical** | Stable-snapshot manifest (upstream commit, tracked/untracked) | Describes the stable snapshot, not the preview branch. |
| `$CASIOEMU/docs/WIZ-TOOLCHAIN.md` | **Stale** | Wiz/CWiz toolchain window | Calls the window "Toolchain" and mentions a "Sample" button; the window is `Wiz Compiler` (`Gui/WizToolchain.cpp:241`) and samples open from the File menu. The data-dir macro is `CASIOEMU_WIZ_DATA_DIR` (`CasioEmuMsvc/CMakeLists.txt`), not `WIZ_VENDORED_DATA_DIR`. Its `DEBUGGER-UI.md` link is dead in the dev tree. |
| `$CASIOEMU/docs/QR.md` | **Current** | QR capture from emulator/real ROMs, desktop window, MCP/web API | Matches `QrCode.cpp` and `Gui/QrCodeWindow.cpp`. |
| `$CASIOEMU/docs/svg-json-model-adaptation.md` | **Current** | `config.json`/`config.bin`, SVG board models, platform behavior | Matches `ModelConfig.cpp` and `SvgBoardModelLoader.cpp`. |
| `$CASIOEMU/docs/README.en.md` | **Historical** | Upstream English README | Describes Windows/Linux/Android; this fork is macOS-first. |
| `$CASIOEMU/docs/README.vi.md` | **Historical** | Upstream Vietnamese README | Same platform mismatch. |
| `$CASIOEMU/docs/README.zh-CN.md` | **Historical** | Upstream Chinese README | Same platform mismatch. |
| `emulator/docs/plan.md` (vendored snapshot only) | **Historical** | Legacy condensed checklist (10.x, 1.x–8.x) | Superseded by `PLAN.md`. Its item statuses predate the plugin host. The dev tree ignores `/docs/plan.md` (`.gitignore:421`), so it is local-only by design; the vendored snapshot is the only copy in this repo. |
| `emulator/docs/DEBUGGER-UI.md` (vendored snapshot only) | **Historical** | Debugger UI notes from the snapshot | Not present in the dev tree, which is why `WIZ-TOOLCHAIN.md`'s link is dead there. |

---

## 3. Verification notes

Checked against source on 2026-11-03 unless a row says otherwise.

| Claim | Evidence | Result |
| --- | --- | --- |
| Plugin permissions are enforced | `Plugin/PluginPermissions.cpp`, `PluginRegistry.cpp:290`, `PluginApi_Impl.cpp` `ScopedPluginApi` | Confirmed |
| Unload/reload and `Shutdown()` exist | `PluginRegistry.cpp:132` (`atexit`), `:657` (`fPluginUnload`) | Confirmed |
| Package extraction is bounded | `PluginPackage.cpp:186` (ratio), `:224` (`S_ISREG`) | Confirmed |
| Example dylibs are universal | `file $CASIOEMU/examples/plugins/*/lib/*.dylib` → `arm64` + `x86_64` | Confirmed |
| Install Plugin... exists | `Gui/PluginLogWindow.cpp:32` → `InstallFromDialog()` | Confirmed |
| Host API version | `PluginVersion.h` → `kPluginHostApiVersion = 3`, manifest accepts 1..3 | Confirmed |
| ROP key slot is zeroed, no interrupts-off | `compiler/src/rop.cpp:1021-1075` | Confirmed |
| `goto` is implemented | `compiler/src/cwiz.cpp:1418`; `tests/test_cwiz.cpp` | Confirmed |
| Targets are `cw`, `cwii`, `cwapp`, `fwnx` | `compiler/src/targets.cpp` | Confirmed (no `cwrop`/`cwiirop`) |
| `ticks()` / `delay_ms()` exist | `compiler/src/wiz.cpp:77-78`, `compiler/runtime.asm:530` | Confirmed |
| `wiztests` is green | `build/wiztests` → 547 checks, 0 failures | Confirmed |
| `wizrun <source>` simulates on the model it compiled for | `compiler/src/main_wizrun.cpp:243-245` canonicalises the default `cwii`; verified by `wizrun examples/hello/01_hello.wz` → `(cwii, stopped: halt …)` | Confirmed (the 2026-11-03 default-model bug is fixed) |

---

## 4. Known open documentation gaps

These are real, and deliberately not papered over:

- `PREVIEW.md` §2 and its Phase 1 wording still describe the plugin host as
  future work; `plugins/STATUS.md` is the current answer.
- `EDITOR.md` items 1.1/1.2/1.4 (directive completion, snippets, status badge)
  and 10.6 (Wiz language definition) are design only; the editor still uses
  `TextEditor::LanguageDefinition::CPlusPlus()` (`Gui/WizToolchain.cpp:245`).
- `vscode-wiz` implements none of the compiler's `preproc/noncanonical-*` lint
  rules and has no completion provider; see `vscode-wiz/README.md`.
- `DIRECTIVES.md` has a "Known lint gaps" section: `#org`/`#pad`/`#launcher`
  aliases are not reported, and `#for cw`/`#for cwii` are misreported as unknown
  targets. These are lint defects, not documentation ones.
