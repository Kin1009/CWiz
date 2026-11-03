# Documentation index

Status: active
Last verified against source: 2026-11-02

One line per document in the Wiz docs folder and the emulator docs folder, with a status:

- **Current** — matches the code or is the active design/spec.
- **Stale** — contains statements that no longer match the code; read with the note.
- **Historical** — kept for context; superseded or describing an older snapshot.

The plugin author track owns `PLUGIN-GUIDE.md`, `plugins/TROUBLESHOOTING.md` and
`plugins/COMPONENTS.md`. Those files are linked from the index when present; they are not
duplicated here.

Emulator paths below point at the checkout used for this audit. If the two repositories are
siblings, replace the `../../../../../private/...` prefix with the sibling path.

---

## 1. Wiz docs (`/Users/kin/Desktop/Wiz/docs`)

| Document | Status | What it is | Notes |
| --- | --- | --- | --- |
| [PLAN.md](PLAN.md) | **Current** | Single plan of record: status, evidence, milestones | Replaces the legacy `plan.md` checklist. Read first. |
| [SHELL-FIXES.md](SHELL-FIXES.md) | **Current** | Implementation notes for 10.3–10.5 and the 3.1 rename | Source of the input fixes now reflected in `PLAN.md` and `COMPONENTS.md`. |
| [COMPONENTS.md](COMPONENTS.md) | **Current** | How the app works, component by component | Companion to this index. |
| [INDEX.md](INDEX.md) | **Current** | This file | — |
| [PREVIEW.md](PREVIEW.md) | **Current** | Product decisions, shell/plugin phases 0–5 | Product source of truth. §2 still says plugin loading is compiled out on macOS; that sentence is stale (the loader ships). Phase 1 is partly done; see `PLAN.md`. |
| [UI_REWRITE.md](UI_REWRITE.md) | **Current** | Target shell layout, settings schema, visual system, order | Design only; almost none of it is implemented yet. |
| [PLUGIN_SPEC.md](PLUGIN_SPEC.md) | **Current** | Frozen `.plugin` v1 contract | §8 discovery paths do not match the code: actual scans are `<user>/Plugins`, `<exe>/plugins`, `<exe>/../Resources/plugins` (`PluginRegistry.cpp:118-134`). Several runtime promises (permission enforcement, unload, `IPluginShell`, bounded extraction) are not implemented; see `PLAN.md` §2.11 and `PLUGIN-GUIDE.md` §11. |
| [PLUGIN-GUIDE.md](PLUGIN-GUIDE.md) | **Current** (owned elsewhere) | Plugin author guide | Verified against the host; §11 is the security/ABI limits list. |
| [plugins/EXAMPLES.md](plugins/EXAMPLES.md) | **Current** | Five example packages, build/install steps | Matches `examples/plugins/`; note the checked-in dylibs are x86_64-only. |
| [plugins/TROUBLESHOOTING.md](plugins/TROUBLESHOOTING.md) | **Current** (owned elsewhere) | Plugin failure modes | Covers permissions, theme override, no-op layouts/injection, dylib arch. |
| [plugins/COMPONENTS.md](plugins/COMPONENTS.md) | **Current** (owned elsewhere) | Plugin host internals and structural gaps | §9 is the host gap list; do not duplicate it elsewhere. |
| [ROP.md](ROP.md) | **Current** | ROP payload syntax, ABI, launchers, key tables | Key-table paragraph corrected 2026-11-02: the emitter uses a zeroed inline slot and no `di,rt`. Verified against `compiler/src/rop.cpp:1021-1152`. |
| [EDITOR.md](EDITOR.md) | **Current (design)** | Directive/target/language table and planned autocomplete | Header now states 1.1/1.2/1.4 are not implemented and the editor still uses the C++ definition. Treat the completion section as a target, not shipped behavior. |
| [SESSION_TABS.md](SESSION_TABS.md) | **Stale** | Early sketch of session tabs | Superseded by `PREVIEW.md` §3–4 and `PLAN.md` 5.1–5.4. Header marks it not implemented; kept for history. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | **Current** | Compiler/assembler/simulator pipeline | Matches `compiler/src` and `compiler/runtime.asm`. |
| [GUIDE.md](GUIDE.md) | **Current** | Wiz/CWiz language guide | — |
| [API.md](API.md) | **Current** | Built-ins, memory map, targets, assembler, simulator | — |
| [RUNNING.md](RUNNING.md) | **Current** | Build/run the toolchain and load programs | — |
| [STATUS_ICONS.md](STATUS_ICONS.md) | **Current** | LCD status icon layouts and API contract | Matches the screen/status implementation. |
| [rop-notes/rop-580-guide.txt](rop-notes/rop-580-guide.txt) | **Historical** | Community ROP guide (Vietnamese) | Source material, not project documentation. |
| [rop-notes/rop-basics.txt](rop-notes/rop-basics.txt) | **Historical** | Community ROP basics | Same. |
| [rop-notes/rop-ez.txt](rop-notes/rop-ez.txt) | **Historical** | Short community ROP cheat sheet | Same. |
| [rop-notes/rop-tap-1.txt](rop-notes/rop-tap-1.txt) | **Historical** | Community notes | Same. |
| [rop-notes/rop-tutorial.txt](rop-notes/rop-tutorial.txt) | **Historical** | Community tutorial | Same. |

---

## 2. Emulator docs (`CasioEmuMsvc-Mac`)

Prefix used below: `../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/`

| Document | Status | What it is | Notes |
| --- | --- | --- | --- |
| [docs/plan.md](../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/docs/plan.md) | **Historical** | Legacy condensed checklist (10.x, 1.x–8.x) | Superseded by `PLAN.md`. Its statuses are out of date (plugin loader/format, ROP, theme tokens, 4.3). It also claims it is excluded from git, but the emulator `.gitignore` has no `plan.md` entry. |
| [docs/WIZ-TOOLCHAIN.md](../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/docs/WIZ-TOOLCHAIN.md) | **Stale** | Wiz/CWiz toolchain window | Links to a missing `DEBUGGER-UI.md`; calls the window "Toolchain" and mentions a "Sample" button, while the UI is "Wiz Compiler"; the compile-time data-dir macro is `CASIOEMU_WIZ_DATA_DIR`, not `WIZ_VENDORED_DATA_DIR`. Content is otherwise close to `Gui/WizToolchain.cpp`. |
| [docs/QR.md](../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/docs/QR.md) | **Current** | QR capture from emulator/real ROMs, desktop window, MCP/web API | Matches `QrCode.cpp` and `Gui/QrCodeWindow.cpp`. |
| [docs/svg-json-model-adaptation.md](../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/docs/svg-json-model-adaptation.md) | **Current** | `config.json`/`config.bin`, SVG board models, platform behavior | Matches `ModelConfig.cpp` and `SvgBoardModelLoader.cpp`. |
| [docs/README.en.md](../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/docs/README.en.md) | **Historical** | Upstream English README | Describes Windows/Linux/Android support; this fork/snapshot is macOS-only. |
| [docs/README.vi.md](../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/docs/README.vi.md) | **Historical** | Upstream Vietnamese README | Same platform mismatch. |
| [docs/README.zh-CN.md](../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/docs/README.zh-CN.md) | **Historical** | Upstream Chinese README | Same platform mismatch. |
| [README.md](../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/README.md) | **Current** | Fork overview: macOS build, toolchain, ROP, themes | Describes the fork features, not the preview shell. Update when `PLAN.md` Phase 2 lands. |
| [SNAPSHOT.md](../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/SNAPSHOT.md) | **Historical** | Stable snapshot manifest (upstream commit, tracked/untracked) | Describes the stable snapshot, not the preview branch. Mentions a "Toolchain" window and a "Dock screen" option that no longer match the UI. |
| [BUILD-MACOS.md](../../../../../private/var/folders/5z/hwddh1v15pv2dnppsz9xqthc0000gn/T/opencode/CasioEmuMsvc-Mac/BUILD-MACOS.md) | **Current** | macOS build recipe and source-layout notes | Outside `docs/`; included because it is the build entry point. |

---

## 3. Stale content found during this audit

These are the statements most likely to cause wrong work. Each was checked against source.

| Location | Stale statement | Reality / evidence |
| --- | --- | --- |
| `ROP.md` key-table paragraph | `slot: 0x3030`; `keytable` emits `setlr; di,rt` | Fixed in place. `compiler/src/rop.cpp:1021-1053` emits a zeroed `00 00` slot; `:1137-1148` emits `setlr` then `sp=er6,pop er8`, no `di,rt`. The code comments at `rop.cpp:1028-1034` and `:1143-1146` still describe the old pattern — documentation-only pass, code untouched. |
| `SESSION_TABS.md` | Reads as an implemented behavior list | Fixed in place: status header says design sketch / not implemented, superseded by `PREVIEW.md` and `PLAN.md`. |
| `EDITOR.md` | Describes autocomplete and status-bar snippets as working | Fixed in place: status header marks 1.1/1.2/1.4 unimplemented; the snippets/status sections are now labeled planned. |
| `docs/plan.md` 10.1/10.2 | "Plugin loader empty", "no `.plugin` format" | Both ship: `Plugin/PluginMan.cpp` Apple branch, `Plugin/PluginManifest.*`, `Plugin/PluginPackage.cpp`, `Plugin/PluginRegistry.cpp`. |
| `docs/plan.md` 7.2 | "`plan.md` excluded from git — keep" | The emulator `.gitignore` has no `plan.md` entry. |
| `PREVIEW.md` §2 | "Plugin loading is compiled out on macOS" | False since the registry landed; see `Plugin/PluginMan.cpp`. |
| `PREVIEW.md` Phase 1 | Implies the whole plugin foundation is future work | Most of it exists; only declarative `ui_revamp`/`inject` application and test registration are missing (`PluginRegistry.cpp:357-412`). |
| `WIZ-TOOLCHAIN.md` | Links `DEBUGGER-UI.md`; "Toolchain" window; "Sample" button | Missing file; the window is `Wiz Compiler` (`WizToolchain.cpp:237`); the sample is `File → New / sample`. |
| `WIZ-TOOLCHAIN.md` data dir | `WIZ_VENDORED_DATA_DIR` | The build defines `CASIOEMU_WIZ_DATA_DIR` (`CasioEmuMsvc/CMakeLists.txt:406`). |
| `PLAN.md` old 7.4 | "`wiztests` passes" as an emulator check | `wiztests` is a Wiz-repo target; the emulator has `CasioEmuMsvc/tests/*.cpp` but no CTest registration. |
| `PREVIEW.md` §10 / root CMake | MCP smoke test is in the release gate | `McpPlugin` is only built on `WIN32` (`CMakeLists.txt:86`); no macOS transport. |
| `PLUGIN_SPEC.md` §8 | Discovery is `<app bundle>/plugins` and `<app bundle>/../plugins` | Actual: user `Plugins`, `<exe>/plugins`, `<exe>/../Resources/plugins` (`PluginRegistry.cpp:118-134`). |
| `PLUGIN_SPEC.md` / `PREVIEW.md` §7 | Permissions, unload and `IPluginShell` implied as available | Permissions are never enforced; `Shutdown()` has no call site and `fPluginUnload` is never called; `IPluginShell` is absent. See `PLAN.md` §2.11. |
