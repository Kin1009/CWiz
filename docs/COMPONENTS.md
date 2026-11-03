# Component map — how the preview app works

Status: active reference
Last verified against source: 2026-11-02
Companion documents: [`PLAN.md`](PLAN.md) (status and order), [`PREVIEW.md`](PREVIEW.md)
(product phases), [`UI_REWRITE.md`](UI_REWRITE.md) (target shell)

This is a map of the code as it exists, not the target architecture. Each section gives purpose,
key files, entry points, data flow, extension points, known issues and the plan items that touch it.
Paths are relative to the emulator repo unless prefixed with `compiler/` (Wiz repo).

Read this together with [`ARCHITECTURE.md`](ARCHITECTURE.md) for the compiler pipeline; this file
covers the **app**, not the language toolchain internals.

---

## 0. Top-level picture

```
main()                                   CasioEmuMsvc/src/casioemu.cpp
  ├─ ThemeManager::LoadSettings()        theme.bin
  ├─ sui_loop()                          StartupUi: pick model / ROM / online model
  ├─ Emulator emulator(...)              one global emulator, chipset, ROM, renderer
  │    ├─ LoadModelDefinition()          config.bin / config.json / SVG board
  │    ├─ tick thread                    chipset.Run / timer callback
  │    └─ Frame()                        face + LCD -> composite texture
  ├─ test_gui()                          ImGui context + SDL window + dock
  │    └─ CreateDebuggerGuiWindows()     every UIWindow instance
  └─ while (emulator.Running())
       ├─ SDL_WaitEventTimeout(16/24ms)
       ├─ gui_loop()                     ImGui frame, all windows, status bar
       ├─ emulator.Frame()               render the calculator
       └─ route SDL events               ImGui vs calculator keyboard
```

The preview shell described in `UI_REWRITE.md` replaces the middle of this diagram. Today the app is
one OS window (desktop) with an ImGui dock space; the calculator is a dock tab.

---

## 1. Startup and StartupUi

**Purpose.** Pick a model (and optional ROM/flash/board overrides) before the emulator exists, show
the local model library, download online models, check for updates, and optionally create desktop
shortcuts.

**Key files.**
- `CasioEmuMsvc/src/StartupUi/StartupUi.{h,cpp}` — UI and `sui_loop()`.
- `StartupUi/OnlineModelClient.{h,cpp}`, `OnlineModelPackage.{h,cpp}`, `OnlineLoopbackServer.{h,cpp}`
  — online catalogue and device-login flow.
- `StartupUi/UpdateChecker.{h,cpp}` — GitHub release check.
- `StartupUi/OnlineBuildConfig.h{,.in}` — build-time API keys.

**Entry points.**
- `casioemu.cpp:279` calls `sui_loop()` when `argv_map["model"]` is empty.
- `StartupSelection` (`StartupUi.h:8`) returns `{ model_path, resources }`; `resources` is a
  `MemoryModelResourceStore` for downloaded/in-memory models.
- `sui_loop()` returns an empty model path when the user quits.

**Data flow.** Scans the model directory, resolves `config.json`/`config.bin`, writes the selection
back to `main()`. Downloaded models are unpacked into the model library or into an in-memory store.
`Emulator` accepts the store and reads ROM/interface/flash through `ModelResourceStore`.

**Extension points.**
- Add a model source by extending the scan list and the `StartupSelection` contract.
- Online operations use `std::future` (`StartupUi.cpp:812`) so the UI stays responsive.
- `UpdateChecker` is toggled by `ThemeSettings::enableUpdateChecker`.

**Known issues.**
- It is a **separate SDL window**, not the in-shell welcome page required by `PREVIEW.md` §3.4/§4.
- It owns model selection *and* update/online UI; the future welcome page should not duplicate these
  responsibilities.
- There is no session concept: it returns one model path per app run.

**Plan items.** 5.1, Phase 2 welcome page; "StartupUi vs welcome page" decision in `PLAN.md` §5.

---

## 2. Main loop and event routing (`casioemu.cpp`)

**Purpose.** Own process startup, SDL/ImGui initialization, the global emulator, the frame cadence,
and the split between calculator input and ImGui input.

**Key files.**
- `CasioEmuMsvc/src/casioemu.cpp` — `main()`, event switch, renderer fallback.
- `CasioEmuMsvc/src/Config.hpp` — desktop `SINGLE_WINDOW` build; `TEST_BUILD`/web variants.
- `CasioEmuMsvc/src/Emulator.cpp` — `Emulator::UIEvent`, `ModelMouseEvent`, `Frame`.

**Entry points.**
- `main()` at `casioemu.cpp:184`.
- Renderer fallback: `ReadRendererHint`/`NextRendererDriver` (`:70-101`) and `.crash.switch_renderer`.
- `LoadPlugins()` at `:352` (Apple branch → `PluginRegistry`).
- Main loop at `:354`; `SDL_WaitEventTimeout` at `:364` (16 ms, 24 ms in low-performance mode).
- Event routing switch at `:450`; calculator-vs-ImGui gate at `:500-526`.
- Window close semantics at `:455-484`: closing the calculator sets `reopen_startup`, shuts the
  emulator, and the outer `for (;;)` reopens `StartupUi`.

**Data flow.**
1. `SDL_WaitEventTimeout` returns an input event or a synthetic `frame_event`.
2. `frame_event`: clear, optional background, `gui_loop()`, `emulator.Frame()`, present.
3. Key/text events: if the event targets the separate calculator window **or**
   `calculator_panel_focused`, send to `Emulator::UIEvent`; otherwise to ImGui.
4. Mouse/finger events: ImGui first when over an ImGui window, otherwise the emulator.
5. Screen-mirror window events are remapped to the main window (`:443-449`).

**Extension points.**
- Add a platform event case in the switch; route via `emulator.UIEvent` or `ProcessImGuiEvent`.
- The future shell needs a shell-level shortcut handler *before* ImGui consumes keys
  (`UI_REWRITE.md` §8); today there is none.
- `F12` screenshot / `Ctrl+F12` recording are handled inside `Emulator::UIEvent`.

**Known issues.**
- Input fixes from `SHELL-FIXES.md` are in place (10.3 Return alias, 10.4 multi-touch release,
  10.5 focus timing). Keep them covered by the manual regression pass; they are recent changes.
- `top_bar_size` is always `0` (`Gui/Ui.cpp:267`), so the top-bar hit test is effectively disabled.
- One global emulator and one global `windows` vector; no session routing (Phase 3).

**Plan items.** 7.5, 5.2, Phase 3.

---

## 3. Ui / window shell

**Purpose.** Provide the `UIWindow` base class, register every debugger window, render the dock
space and status bar, and persist geometry/tab visibility.

**Key files.**
- `CasioEmuMsvc/src/Gui/Ui.hpp` — `UIWindow`, globals (`windows`, `calculator_panel_focused`,
  `code_viewer`, `injector`, `membp`, `snapshot_window`).
- `Gui/Ui.cpp` — `gui_loop()`, `RenderDebuggerGuiWindows()`, `RenderDockSpace()`,
  `SetupDefaultDockLayout()`, `CreateDebuggerGuiWindows()`, `test_gui()`, `UIHelpers`.
- `Gui/Theme.cpp` — `SettingsWindow` and `MakeSettingsWindow()`.
- `Gui/ThemeManager.*` — style and font application.

**Entry points.**
- `test_gui()` (`Ui.cpp:373`) creates the desktop window, ImGui context and windows.
- `CreateDebuggerGuiWindows()` (`:298`) builds the global window list.
- `gui_loop()` (`:270`) is called once per frame from `casioemu.cpp`.
- `UIWindow::Render()` (`Ui.hpp:41`) calls `RenderCore()`; `GotoMemoryAddress()` is the optional
  cross-window navigation hook.

**Window list (desktop, NX models).** Variable, HwController, Labels, Watch, Funcs, Code, Injector,
Breakpoints, Addrs, Rop Compiler, Plugin Manager, Snapshot, QR Code, Settings, Bitmap, editors
(ROM/RAM/All/PRAM/Flash), Wiz Compiler, Calculator. Some are model-gated (EPS, TI, 5800P).

**Data flow.**
- `windows` is a `std::vector<UIWindow*>`; each window is rendered every frame in insertion order.
- First-run dock layout splits 24% to the right and docks `Calculator` there (`:171-186`).
- `RenderStatusBar()` draws paused/running, PC, breakpoint count and model name (`:82-136`).
- Tab visibility is stored in `ThemeSettings::hiddenTabs` and applied by `SettingsWindow`.

**Extension points.**
- Add a window: subclass `UIWindow`, implement `RenderCore()`, push a new instance in
  `CreateDebuggerGuiWindows()`, give it a stable `name` (ImGui persistence key).
- `BringToFront()`, `GotoMemoryAddress()` and `Debug*` free functions are the existing integration
  seams used by plugins and MCP.
- `UIHelpers::ClickableAddress` standardizes code/memory jump gestures.

**Known issues.**
- No shell regions: sidebar, bottom panel, session tab strip and welcome page do not exist.
- Windows are global, not per session; `m_emu` is a single global pointer.
- `SetupDefaultDockLayout` runs once per process (`static bool checked`); users with an old layout
  keep it.
- Settings is forced open (`UIWindow::Render`) as the recovery point for hidden tabs.

**Plan items.** 5.1–5.4, 2.2, 2.4, Phase 2/3.

---

## 4. Calculator / CalcScreenWindow

**Purpose.** Show the complete calculator (face, key labels, LCD, status sprites) as a dock tab and
map mouse clicks back to model keys.

**Key files.**
- `CasioEmuMsvc/src/Gui/CalcScreenWindow.cpp` — `CalcScreenWindow`, `DrawCentred`,
  `ForwardClicks`, `DrawControls`.
- `Emulator::Frame()` (`Emulator.cpp:727+`) — renders the model into `composite_texture`.
- `Peripheral/Screen*`, `Peripheral/EpsScreen.*`, `Peripheral/TiMultiViewScreen.*` — screen
  providers.
- `Gui/CalcScreenWindow.cpp` fallback uses `IScreenFrameProvider`.

**Entry points.**
- `CreateCalcScreenWindow()` → `UIWindow("Calculator")` (`:63`).
- `CalcScreenWindow::RenderCore()` reads `m_emu->GetCompositeTexture()`.
- `Emulator::GetCompositeTexture*()` and `ModelMouseEvent()` are the public seams.

**Data flow.**
1. The tick thread runs the chipset; `Chipset::Frame` updates the LCD/status state.
2. `Emulator::Frame()` renders the interface image and screen into `composite_texture`.
3. `CalcScreenWindow` draws that texture with `Fit` or explicit zoom (0.25–8.00x).
4. Mouse position is mapped from the drawn rectangle to model coordinates and sent through
   `ModelMouseEvent` → `chipset.UIEvent` → `Keyboard`.
5. Closing the tab selects the model's main menu and reopens the tab (`Ui.hpp:76-86`).

**Extension points.**
- Models without a face use `IScreenFrameProvider` (`Peripheral/Screen.hpp`) and can supply their
  own frame pixels/ink color.
- The same texture path is used by screenshots/recording (`Emulator` capture flags).
- `Fit`/zoom are window-local; a future shell can host the same window in session content.

**Known issues.**
- One calculator instance per process.
- Focus is still a global flag (10.5 fixed the timing, not the scope).
- The close behavior is "reboot + reopen", not "close a session tab" (5.2).
- Keyboard clicks use mouse events even in docked mode; touch is a separate path (`Keyboard.cpp`).

**Plan items.** 2.4, 5.2, 7.5 regression.

---

## 5. Keyboard, focus and multi-touch

**Purpose.** Translate SDL keyboard/mouse/finger events into the model's KI/KO key matrix, including
stuck keys, ghosting recalculation and multi-finger holds.

**Key files.**
- `CasioEmuMsvc/src/Peripheral/Keyboard.{hpp,cpp}` — `Keyboard::UIEvent`, `PressAt`, `ReleaseAt`,
  `ReleaseAll`, `RecalculateEmuInput`, `RecalculateGhost`.
- `Emulator::UIEvent` / `ModelMouseEvent` (`Emulator.cpp:552-611`).
- `casioemu.cpp:496-526` — routing gate.
- `Gui/Ui.cpp:273` and `Gui/Ui.hpp:65-70` — focus flag.
- `Gui/CalcScreenWindow.cpp` — docked click mapping.
- `Gui/TouchMouseTranslator.*` — Android single-window touch routing.

**Data flow.**
1. `ModelInfo.buttons[]` maps `kiko` codes and key names to SDL keycodes (`Keyboard.cpp:535-630`).
2. `UIEvent` handles `SDL_FINGERDOWN/UP`, `SDL_MOUSEBUTTONDOWN/UP`, `SDL_KEYDOWN/UP`.
3. `PressAt`/`ReleaseAt` find the button rectangle, set `pressed`/`pressingFingerId`, then
   `RecalculateEmuInput` or `RecalculateGhost`.
4. Focus loss calls `ReleaseKeyboardKeys()`; `ReleaseAll()` clears non-stuck holds.
5. The main loop only forwards keys to the calculator when the calculator window is focused or the
   event belongs to a separate calculator window.

**Extension points.**
- Add a key binding by adding a `buttons[]` entry in `config.json` / `ModelInfo`.
- `IKeyboard` in `Plugin/PluginApi.h` exposes `Key`, `PressCode` and `ReleaseAll` to plugins.
- `TouchMouseTranslator` is where a future per-session touch target should live.

**Known issues.**
- 10.3 is fixed: Return aliasing is gated by `Keyboard::ReturnAliasActive()` and advertised as
  `Ret=Exe` (`Peripheral/Keyboard.cpp:637-641`, `Gui/CalcScreenWindow.cpp:184-186`).
- 10.4 is fixed: `active_fingers` plus `ReleaseAllIfNoInputHeld()` keep finger/keyboard holds alive
  (`Peripheral/Keyboard.cpp:125,757-767,819,1393-1406`).
- 10.5 is fixed: focus is cleared only when the Calculator panel is gone
  (`Gui/Ui.cpp:280-290`); `UIWindow::Render` still sets it from `ImGui::IsWindowFocused()`.
- Chinese/Vietnamese comments in `Keyboard.cpp` document several abandoned approaches; treat the
  comments as history, not behavior.

**Plan items.** 7.5 regression pass, Phase 3 input routing.

---

## 6. Memory viewers

All are `UIWindow`s. Hex windows share `MemoryEditor` (`Gui/hex.hpp`) and the
`DrawColumnSelector` helper (`Gui/Editors.cpp:25`).

| Component | Purpose | Key files | Entry / data flow | Known issues | Plan |
| --- | --- | --- | --- | --- | --- |
| `AddressWindow` ("Addrs") | Bookmark arbitrary addresses; lock values (read/write hooks) | `Gui/AddressWindow.{h,cpp}` | `CreateAddressWindow()`; `SetupHooks(on_memory_*)`; `Debug*` free functions for plugins | Locks are per-window, not persisted; address list is empty on start | 4.1? (memory grouping) |
| `VariableWindow` | Show known BCD variables (real/imag), hex and addresses | `Gui/VariableWindow.cpp`, `CwiiHelp.h`, `Models.h` | `CreateDebuggerGuiWindows` gates by model; reads `n_ram_buffer` + `GetVariableOffsets()` | No editing; layout uses debug hex labels | 2.1 |
| `WatchWindow` ("Watch") | Register watch and a small memory editor; EPS trace toggle | `Gui/WatchWindow.{hpp,cpp}` | `WatchWindow::RenderCore`, `PrepareRX/UpdateRX` | NX register set assumptions; trace capacity fixed at 4096 | — |
| `Breakpoints` | Execution-adjacent memory breakpoints, data compare, skip count, hit records | `Gui/MemBreakPoint.{hpp,cpp}`, `Chipset/CoreDebug.hpp` | `SetMemBp()` from hex context menu; `SetupHooks()`; `IDebugger` mirrors it | Separate from CodeViewer execution breakpoints; UI is dense | 3.2 (merge with injection?) |
| `Injector` ("Rop") | Load `hc-inj.txt`-style payloads, edit bytes, inject into RAM | `Gui/Injector.{hpp,cpp}` | `CreateDebuggerGuiWindows`; async file reload thread; `ApplyInjection` | No safe mode; still titled "Rop" while the compiler window is "Injection" (3.1); file schema is legacy | 3.1–3.4 |
| `SnapshotWindow` | Snapshot tree, save/load/export, autosave, preview image | `Gui/SnapshotWindow.{h,cpp}`, `Snapshot.{h,cpp}` | `CreateSnapshotWindow()`; `SnapshotManager`; `chipset.SaveStateAll/LoadStateAll` | Snapshot is process-global, not per session; autosave path is model-derived | 5.2 |

**Shared behavior.** Hex editors (`ROM`, `RAM`, `All`, `PRAM`, `Flash`) are created in
`Gui/Editors.cpp:216-270`. `RAM`/`PRAM` use `MMU_Hex` so reads/writes go through `me_mmu`; `All` is
a 1 MB flat view. Each window has its own column count (1–64 bytes/line) and a context menu to set
a memory breakpoint.

**Extension points.**
- Add a memory window by subclassing `MemoryEditor` + `UIWindow` (see `HexEditor`).
- Expose a new view through `Debug*` functions in `Ui.hpp` for MCP/plugins.
- `UIHelpers::JumpToMemory()` picks the `RAM` window first, then a fallback.

**Plan items.** 4.1 (merge ROM/All), 3.1–3.4 (injection UI), 4.3 (done), Phase 2 tool dock.

---

## 7. Disassembler, CodeViewer, CallAnalysis, LabelViewer

**Purpose.** Inspect and navigate code, execution breakpoints, call records and labels.

**Key files.**
- `CasioEmuMsvc/src/Gui/CodeViewer.{hpp,cpp}` — disassembly list, breakpoints, `JumpTo`.
- `Gui/CallAnalysis.{h,cpp}` — function-call recording and filters.
- `Gui/LabelViewer.{h,cpp}` — searchable common labels (`GetCommonMemLabels`).
- `Ext/U8Disas.{h,cpp}`, `Ext/ePSDisas.{h,cpp}` — decoders.
- `Chipset/T4xCore.*`, `Chipset/T4xDebugger.cpp` — TI multi-view debug path.
- `Gui/Hooks.{h,cpp}` — hook registration.

**Entry points and data flow.**
- `CodeViewer` is created once (`code_viewer`) and refreshed by
  `m_emu->chipset.on_rom_changed = [] { code_viewer->PrepareDisasm(); }` (`Ui.cpp:323`).
- `UIHelpers::ClickableAddress` calls `code_viewer->JumpTo(addr)` for code targets.
- Breakpoints live in `CodeViewer`; the status bar reads `GetBreakpointCount()`.
- `CallAnalysis` installs `on_call_function` / `on_eps_call_function` / `on_t4x_call_function`
  hooks and records `{pc, lr, xr0, backtrace}`; the UI filters by caller/callee and can record.
- `LabelViewer` reads model labels (`labels.txt` parsed in `Ui.cpp:303-305`) and common memory
  labels; `g_labels` is global.

**Extension points.**
- Add an architecture decoder by extending `U8Disas`/`ePSDisas` and the `CoreDebug` interface.
- Register a new hook in `SetupHooks` and consume it from a window.
- `IDebugger::GetDisassembly` (`Plugin/PluginApi.h`) exposes the same decoder to MCP/plugins.

**Known issues.**
- Four separate breakpoint models (code, memory, address lock, register compare) can confuse users;
  `UI_REWRITE.md` groups Code/Breakpoints under State/Analysis but does not unify them.
- `g_labels` is a global vector, not per session.
- Call recording stores full backtraces and can grow large; no cap in the window.

**Plan items.** 1.4? (editor navigation), 3.2, 5.2, Phase 3.

---

## 8. ROM / RAM / QR windows

**Purpose.** Raw memory inspection plus QR payload capture from the emulated firmware.

**Memory windows.**
- Files: `Gui/Editors.{h,cpp}`, `Gui/hex.hpp` (`MemoryEditor`).
- Windows: `ROM`, `RAM`, `All`, `PRAM`, `Flash`; EPS models add `ROM`/`Flash`/`RAM` with word
  addressing (`Editors.cpp:248-251`).
- Data flow: `ReadFn`/`WriteFn` either read the flat buffer or call `me_mmu`.
- Extension: a new view is a `MemoryEditor` subclass; `GotoMemoryAddress` implements cross-window
  jumps.
- Issues: `ROM` and `All` overlap (4.1); `All` is 1 MB regardless of model; no per-session view.

**QR.**
- Files: `QrCode.{h,cpp}`, `Gui/QrCodeWindow.{h,cpp}`.
- Capture: `QrCodeCapture::HandleStop` (official emulator ROM stop event) and `Poll` (real ROM RAM
  scan), state in `QrCodeState` with history, version and real-ROM page progress.
- UI: `QrCodeWindow::RenderCore` polls, shows active/complete/version/length/history, copy/open URL,
  current page text.
- Export: `Emulator`/MCP/`CASIOEMU_CORE_WEB` read the same state (`docs/QR.md`).
- Extension: add a firmware QR signature in `QrCode.cpp`; add a field to `QrCodeState` and the MCP
  `get_qr_code` schema.
- Issues: desktop window only appears for NX models (`Ui.cpp:341-342`); history is per process.
- Plan items: 4.1, Phase 2 tool dock "App/QR".

---

## 9. WizToolchain

**Purpose.** In-process IDE for Wiz (`.wz`) and CWiz (`.cwiz`): edit, build, lint, preprocess, run,
patch a ROM slot, and compile/inject ROP payloads.

**Key files.**
- `CasioEmuMsvc/src/Gui/WizToolchain.{h,cpp}` — the `Wiz Compiler` window.
- `CasioEmuMsvc/third_party/wiz/` — vendored `wizcore` (compiler, assembler, runtime, targets,
  ROP backend); linked by `CasioEmuMsvc/CMakeLists.txt:397-419`.
- `compiler/src/rop.cpp`, `compiler/src/rop_data.hpp` — upstream ROP backend used by the window.
- `Gui/imgui/TextEditor.{h,cpp}` — editor widget.
- `Gui/Rop/RopCompilerUI.cpp` — separate `Injection` window (the legacy Rop Compiler UI).

**Entry points.**
- `CreateToolchainWindow()` → `UIWindow("Wiz Compiler")` (`WizToolchain.cpp:237`); added only when
  the model name contains `580` or `880` (`Ui.cpp:352-359`).
- `RenderCore()` (`:246`) → menu bar, toolbar, source/output table, diagnostics.
- Build: `DoBuild()` (`:388`) → `wiz::build_cwiz`/`build_source`; ROP: `DoRopBuild()` (`:438`);
  inject: `InjectRop()` (`:530`).
- Lint: `RunLint()` (`:563`) wraps `wiz::lint_source`.
- Examples: `RenderExamplesMenu()` (`:1032`) walks `WizExamplesDir()`.

**Data flow.**
1. Target comes from the running model (`PickTargetFromModel`, `:351`), overridable in the toolbar.
2. Language comes from the file extension or the manual chooser (`EffectiveLang`, `:336`).
3. `wiz::set_data_dir(WizDataDir())` points the compiler at `runtime.asm` and boot templates. Search
   order: `$WIZ_DATA_DIR` → bundle `wiz/` → bundle `Resources/wiz/` → compile-time
   `CASIOEMU_WIZ_DATA_DIR` (`:99-120`).
4. Build writes `wiz/<name>.bin`, `.asm` and the source next to the model; Build & Run either
   patches a ROM slot (`cwapp`) or writes a session image and reloads the ROM.
5. ROP: the UI padding slider overrides `#pad`; `wiz::build_rop` returns payload, launcher, listing;
   Inject writes and verifies both blocks in RAM.
6. Diagnostics: compiler errors (`CompileError.line/col`) become editor error markers and a list;
   the linter adds style/robustness findings.

**Extension points.**
- Add a target choice in `kTargets` (`:178`) and ensure `HardwareTarget()` maps it.
- Add an example by dropping a file under the examples directory; categories are folders.
- Add a language by extending `EffectiveLang` and `TextEditor` definitions (10.6).
- Build hooks belong in `wiz::build_*`; the window should stay a thin adapter.

**Known issues.**
- Item 10.6: editor uses `TextEditor::LanguageDefinition::CPlusPlus()` (`:245`), so no Wiz/CWiz
  highlighting.
- 1.1/1.2/1.4: no autocomplete, doc snippets, gutter or command palette.
- 1.5: the header shows the chosen target, not a target parsed from `#for`/`#cwapp`.
- 1.6: only dark/light palette swap (`RefreshEditorPalette`, `:1044`); no font-token sync.
- The ROP UI is split between `Wiz Compiler` (source/target selection, no separate ROP target —
  the backend is chosen by `#rop`) and `Injection` (`Rop/RopCompilerUI.cpp`); 3.2–3.3 want one
  panel, and `Gui/Injector.cpp` is still titled `Rop`.
- `third_party/wiz` is a copy of the Wiz `compiler/` tree; it must be refreshed manually
  (`docs/WIZ-TOOLCHAIN.md`).

**Plan items.** 10.6, 1.1–1.6, 3.1–3.4, 8.3.

---

## 10. Plugin host (high level)

**Purpose.** Discover, validate, trust, load and manage `.plugin` packages, and expose the host API
to native plugins.

**Key files.**
- `CasioEmuMsvc/src/Plugin/PluginManifest.{h,cpp}` — schema 1 manifest, id/semver/permission rules.
- `Plugin/PluginPackage.{h,cpp}` — zip extraction, traversal guard, SHA-256 cache.
- `Plugin/PluginRegistry.{h,cpp}` — discovery, trust store, enable/disable/remove, declarative and
  native load.
- `Plugin/PluginApi.h`, `Plugin/PluginApi_Impl.cpp` — `PluginApi`, `IEmulator`, `ICPU`, `IMMU`,
  `IChipset`, `IDebugger`, `IKeyboard`, `IPlatformHost`, `QueryInterface`.
- `Gui/PluginLogWindow.{hpp,cpp}` — `Plugin Manager` UI.
- `examples/plugins/` — five fixture packages.

**Entry points and data flow.**
- `LoadPlugins()` (`Plugin/PluginMan.cpp`, Apple branch) → `PluginRegistry::Initialize()`.
- Discovery order: user `<SettingsDir>/Plugins`, `<exe>/plugins`, `<exe>/../Resources/plugins`
  (`PluginRegistry.cpp:96-145`). Portable mode uses `<exe>/settings` when `portable.flag` exists.
  `PLUGIN_SPEC.md` §8 documents the same order.
- Package → manifest validation → SHA-256 → cache directory → trust record
  (`plugin-trust.json`) → load.
- Declarative: `LoadDeclarative()` reads `entry_point`; themes go through
  `ApplyThemeDocument()`, `ui_revamp` through `PluginHostGui::ApplyShellLayout()`, and `inject`
  documents register with `PluginPayloads` for the Payload view.
- Native: `LoadNative()` `dlopen`s `nativeEntry`, calls `fPluginLoad(g_pluginapi)` and the ABI
  handshake; unload calls `fPluginUnload`.
- Manager: Installed/Developer tabs, permissions, state, enable/disable/review/remove/reveal and
  **Install Plugin...** (`InstallFromDialog` → `InstallFromFile`). Drag-and-drop is not wired.

**Extension points.**
- Add a declarative type in `PluginManifest.cpp` and a handler in `LoadDeclarative`.
- Add a host interface in `PluginApi.h` and implement it in `PluginApi_Impl.cpp`; new capabilities
  are additive via `QueryInterface` (contract in `PLUGIN_SPEC.md`).
- Add a native example under `examples/plugins/` and a package rule test.
- Permission checks live in the guarded facades in `PluginApi_Impl.cpp`
  (`PlatformHostGuard`, `EmulatorGuard`, `MmuGuard`, `ChipsetGuard`, `HooksGuard`, …), each calling
  `Allow(capability, api)`; a new host method needs a guard, not only a manifest permission.

**Known issues (short list; details in the plugin docs).**
- `IPluginShell` (full-shell native takeover) does not exist; declarative `ui_revamp` is the only
  layout mechanism.
- No file watcher: a changed package needs **Reload Declarative** or a restart. Native plugins can
  be unloaded/reloaded, but libraries with registered hooks are retained by design.
- `RegisterPlugin`'s return value is ignored — it cannot veto a load.
- Drag-and-drop install is not wired (the Install button is).
- The startup font rebuild can visually override a plugin theme while the plugin stays `Loaded`
  (recorded 2026-11-02; not re-verified).
- Trust compares SHA-256 and the permission set; the manifest **version is stored but not
  compared**.
- Native plugins run in-process; there is no sandbox (documented policy, not a bug).

Author-facing details are deliberately **not duplicated here**. See
[`PLUGIN_SPEC.md`](PLUGIN_SPEC.md), [`PLUGIN-GUIDE.md`](PLUGIN-GUIDE.md),
[`plugins/TROUBLESHOOTING.md`](plugins/TROUBLESHOOTING.md) and
[`plugins/COMPONENTS.md`](plugins/COMPONENTS.md). `PLAN.md` §2.11 is the promised-vs-implemented
table.

**Plan items.** 6.1–6.4, 10.1, 10.2, Phase 1/4.

---

## 11. Settings and themes

**Purpose.** Persist appearance, language, scale, geometry and integration toggles; apply ImGui
style and fonts; accept JSON theme tokens from plugins.

**Key files.**
- `CasioEmuMsvc/src/Settings.h` — empty `Settings` stub (`// TODO`). Not the real settings path.
- `Gui/ThemeManager.{h,cpp}` — `ThemeSettings` binary format, `theme.bin` read/write, fonts, scale,
  background tint.
- `Gui/Theme.{h,cpp}` — `SettingsWindow` (`General`, `Tabs`) and `MakeSettingsWindow()`.
- `Plugin/ThemeTokens.{h,cpp}` — JSON theme document → `ImGuiStyle` (colors + metrics).
- `examples/plugins/theme-vscode-dark/` — example token file.

**Data flow.**
1. `ThemeManager::LoadSettings()` at `casioemu.cpp:216` reads `./theme.bin` (or
   `/persist/theme.bin` on web) with magic `0x47464354` and a field-marker format; it also accepts a
   legacy struct (`ThemeManager.h:164-219`).
2. `SettingsWindow` edits `ThemeSettings`, calls `SaveSettings()`, `SetDarkMode/SetLightMode`,
   `SetFontScale`/`RequestFontRebuild`.
3. `ThemeTokens.cpp` builds a fresh `ImGuiStyle` from `schema:1` + `tokens.colors` +
   `tokens.metrics`, then assigns it to `ImGui::GetStyle()`.
4. `g_plugin_theme_active` prevents saved built-in settings from overwriting an active plugin theme.

**Extension points.**
- A new setting must be added to `THEME_CONFIG_MAP` in `ThemeManager.h`, the Settings UI, and (once
  implemented) the JSON schema.
- A new theme token maps a JSON key to one or more `ImGuiCol_*`/style fields in `ThemeTokens.cpp`.
- Built-in presets should live in `resources/themes/*.json` (not yet present).

**Known issues.**
- 10.7 / Phase 2: settings are still `theme.bin`; `settings.json`, portable override, schema
  version and `theme.bin` migration are not implemented.
- 2.2: only `General` and `Tabs`, not Theme/Editor/Emulator/Plugins/System.
- 2.3: only dark/light, no Midnight Blue/Solarized/Mono Green.
- `Settings.h` is dead code and can mislead a new contributor.
- The Settings window is forced open on every frame (`Ui.hpp:41-45`) to prevent a lockout.

**Plan items.** 10.7, 2.1–2.3, 8.5, Phase 2.

---

## 12. MCP plugin

**Purpose.** Expose debugger automation over the Model Context Protocol: status, pause/resume,
registers, memory, disassembly, breakpoints, screenshots, QR, snapshots, key injection.

**Key files.**
- `McpPlugin/dllmain.cpp` — tool definitions and dispatch (`ToolDefinitions()` at `:228`).
- `McpPlugin/PluginApi.h` (copy) and `McpPlugin/README.md`.
- `CasioEmuMsvc/src/Plugin/PluginApi.h` — canonical host interfaces used by the implementation.
- `CasioEmuMsvc/src/Plugin/PluginApi_Impl.cpp` — `IDebugger`, `IKeyboard`, `IEmulator` services.

**Build and entry.**
- Root `CMakeLists.txt:86` adds `McpPlugin` only on `WIN32` with `BUILD_MCP_PLUGIN`.
- The DLL exports `fPluginLoad` through the normal plugin loader (`CasioEmuMsvc.Plugin.*.dll`).
- On macOS there is no MCP build target; the host interfaces exist but no transport is built.

**Data flow.** MCP request → JSON-RPC handler → `PluginApi::QueryInterface<IDebugger>()` etc. →
chipset/CPU/MMU state → JSON response. The plugin uses the same `IDebugger` surface that a native
`.plugin` would use.

**Extension points.**
- Add a tool in `ToolDefinitions()` and a handler branch; keep the input schema in sync.
- Reuse `IDebugger` rather than reaching into globals.
- A macOS port needs a POSIX transport (the current code uses Winsock/httplib on Windows).

**Known issues.**
- **Not built on macOS**, yet `PREVIEW.md` §10 lists an MCP smoke test in the definition of done.
  Decide whether to port it or drop it from the release gate (`PLAN.md` §5).
- The copy of `PluginApi.h` under `McpPlugin/` must track the canonical header.
- `McpPlugin/json.hpp` is also used by the app (`ModelConfig.cpp`, `ThemeTokens.cpp`), so it is a
  shared dependency despite living in the plugin folder.

**Plan items.** Phase 1/5 tests, "MCP on macOS" decision.

---

## 13. Model loading

**Purpose.** Turn a model directory (or in-memory package) into `ModelInfo`: hardware ID, ROM/flash,
interface image, SVG board, buttons, status sprites, screen geometry.

**Key files.**
- `CasioEmuMsvc/src/ModelConfig.{h,cpp}` — `config.json`/`config.bin` read/write and validation.
- `ModelResourceStore.{h,cpp}` — file-backed vs in-memory resources, session writes.
- `SvgBoardModelLoader.{h,cpp}` — SVG board parsing and button/status extraction.
- `ModelInfo.h`, `Models.h` — data structures and per-hardware tables.
- `Emulator::LoadModelDefition()` (`Emulator.cpp:625`) and `GetModelFilePath`.

**Entry points and data flow.**
1. `Emulator` constructor stores `model_path`/`model_resources` and calls `LoadModelDefition()`.
2. `LoadModelInfoFromFolder()` (`ModelConfig.cpp:472`) prefers `config.bin` when it is newer than
   `config.json` and any referenced board SVG; otherwise it parses JSON.
3. JSON with `board_path` → `LoadSvgBoardModelInfo()` (`SvgBoardModelLoader.cpp:708`) reads the SVG,
   extracts buttons/status rects, and merges `config.json` overrides.
4. JSON without `board_path` → legacy sprite/button model (`ApplyModelInfoJsonValue`).
5. After a successful JSON read, `config.bin` is rewritten as a cache.
6. `Emulator` opens `rom_path`, optional `flash_path`, and the interface image through the resource
   store; `Chipset` owns the MMU/ROM.

**Extension points.**
- Add a hardware ID/descriptor in `Models.h`/`ModelInfo` tables.
- Add a JSON field through the `ApplyModelInfoJsonValue` reader/writer pair and `RequireBaseModelFields`.
- Add an SVG board feature in `SvgBoardModelLoader.cpp`; keep `config.json` overrides explicit.

**Known issues.**
- `config.bin` is a cache with a timestamp check; editing `config.json` without touching the board
  SVG usually regenerates it, but a stale cache can still surprise users (documented in
  `docs/svg-json-model-adaptation.md`).
- `config.bin`/`config.json` both being absent is a hard failure (`PANIC` in
  `Emulator::LoadModelDefition`).
- The model loader is process-global; multi-session will need one `ModelInfo`/resource store per
  session.

**Plan items.** Phase 3 session resources; `docs/svg-json-model-adaptation.md` is the detailed
reference.

---

## 14. Build, packaging and tests

**Purpose.** Configure/build the app, bundle the Wiz data and models, and provide test coverage.

**Key files.**
- `CMakeLists.txt` (root) — SDL/SDL_image/discord-rpc, app subdirectory, optional `McpPlugin`
  (`WIN32` only).
- `CasioEmuMsvc/CMakeLists.txt` — sources, macOS bundle (`:344-357`), Wiz toolchain (`:397-419`),
  post-build copy of `Resources/wiz` and models (`:421+`).
- `build-mac.sh` — clean configure + Ninja build; **does not copy an app bundle**.
- `CasioEmuMsvc/tests/plugin_loader_smoke.cpp`, `eps6800_adapter_smoke.cpp` — smoke tests, not
  registered with CTest.
- Wiz repo: `build.sh`, `tests/`, `compiler/CMakeLists.txt` → `build/wiztests`.

**Data flow.**
- CMake globs `src/**` into the target; `CASIOEMU_WIZ_TOOLCHAIN=ON` links `wizcore` and defines
  `CASIOEMU_WIZ_DATA_DIR`.
- The macOS post-build step copies `third_party/wiz/data` to `Contents/Resources/wiz` and the
  examples to `Contents/Resources/wiz/examples`.
- `build-mac.sh` is the documented developer entry; CI/release packaging is not represented in the
  repo.

**Extension points.**
- Add a source file and it is picked up by the glob; add a third-party dependency as a submodule and
  an `add_subdirectory`.
- Register smoke tests with `enable_testing()` + `add_test()`; today the emulator has no `ctest`
  suite.
- The Wiz test suite is the authoritative compiler/assembler/simulator coverage.

**Known issues.**
- `wiztests` is a Wiz-repo target, not an emulator target; plan 7.4 conflates them.
- `plugin_loader_smoke` is a `main()` program with `assert`; it is not built by CMake and not run.
- `build-mac.sh` prints paths but does not copy the `.app`; plan 7.3 says it does.
- MCP is not built on macOS (`CMakeLists.txt:86`).

**Plan items.** 7.3, 7.4, Phase 5.

---

## 15. Emulator core (context only)

The core is reused, not rewritten (`PREVIEW.md` §2). It is worth knowing where the boundaries are:

- `Chipset/Chipset.cpp` — hardware dispatch, `Frame`, `UIEvent`, `Run*Frame`, ROM reload, state
  save/load.
- `Chipset/CPU*.cpp`, `MMU.*`, `InterruptSource.*`, `T4xCore.*`, `ePSCpu.*` — CPUs.
- `Peripheral/*` — keyboard, LCD/screen providers, timers, flash, RTC, power, audio, UART.
- `Emulator.cpp` — window/renderer, tick thread, model resources, screenshot/recording flags.
- `Snapshot.cpp`, `QrCode.cpp` — cross-cutting state.

Extension points are the `PluginApi` interfaces and the `on_*` hooks (`Gui/Hooks.h`), not direct
writes into peripheral internals. Multi-session work must move the `Emulator`/`Chipset` ownership
out of globals; that is the Phase 3 risk called out in `PREVIEW.md` §9.

---

## 16. Component → plan map

| Component | Open plan items |
| --- | --- |
| StartupUi | 5.1, Phase 2 welcome |
| Main loop / event routing | 5.2, Phase 3 (10.3–10.5 fixed; regression only) |
| Ui / shell | 2.2, 2.4, 5.1–5.4, Phase 2 |
| Calculator | 2.4, 5.2 |
| Keyboard / focus | 7.5 regression |
| Memory viewers | 4.1, 3.1–3.4 |
| Disassembler / analysis | 3.2, 5.2, Phase 3 |
| ROM/RAM/QR | 4.1 |
| WizToolchain | 10.6, 1.1–1.6, 3.1–3.4, 8.3 |
| Plugin host | 6.1–6.4, 10.1, 10.2, Phase 1/4 |
| Settings / themes | 10.7, 2.1–2.3, 8.5 |
| MCP | Phase 1/5 tests, macOS decision |
| Model loading | Phase 3 per-session resources |
| Build / tests | 7.3, 7.4, Phase 5 |
