# Shell revamp — implementation record

Status: implemented (layout revamp + JSON settings + live multi-session + terminal)
Branch: preview
Last updated: 2026-11-03

Paths are repo-relative to the Wiz workspace unless prefixed with `$CASIOEMU`,
the emulator checkout (default: the sibling `CasioEmuMsvc-Mac`). See
`INDEX.md` for the convention.

This document records what was built, how the pieces fit, and the verification
loop. It also records two explicit overrides of the earlier design docs:

1. **Layout is VS Code-based; components keep the original CasioEmuMsvc skin.**
   The region model (title bar, tab strip, breadcrumb, activity bar, sidebar,
   content, tool dock, bottom panel, status bar) follows VS Code. Fonts,
   buttons, tab fill, colours, and widget rendering are the original
   CasioEmuMsvc component style. The VS Code palette presets remain selectable
   (`vscode-dark-plus`, `vscode-dark` / Dark Modern, `vscode-light`, plus
   Midnight Blue / Solarized / Mono Green) but `classic` is the default.
2. **Activity bar and breadcrumb are removed.** They had no real
   destinations; the shell uses the file explorer, tool-document tabs,
   calculator side panel, bottom panel and menu bar instead.

---

## 1. What the shell looks like

```
+--------------------------------------------------------------------------+
| traffic lights | CasioEmuMsvc  File View Session | actions             | 35 px chrome
+--------------------------------------------------------------------------+
| session tab 1 | session tab 2 | +                        | ... ||        | 35 px tabs
+----------------+---------------------------------------------------------+
| EXPLORER sidebar 300 px | tool-document tabs (Code/Watch/…/Settings) | CALCULATOR |
| (file tree, 8 px inner) | Variables / Hardware / … (full labels)     | side panel |
|                         +--------------------------------------------+ (resizable) |
|                         | OUTPUT | PROBLEMS | TERMINAL              |            | 200 px
+-------------------------+--------------------------------------------+------------+
| status bar (classic theme colour, 22 px)                                  |
+--------------------------------------------------------------------------+
```

- One frameless SDL window; native macOS traffic lights stay in the top-left.
  `Ext/MacWindow.mm` sets `FullSizeContentView`, transparent title bar, hidden
  title and keeps the standard buttons.
- The title text starts 12 px after the traffic-light area.
- The main editor column is the **tool-document area**: a full-label document
  tab strip (Code Viewer, Functions, Variables, Watch, Hardware, Labels,
  Memory, Breakpoints, Bitmap Viewer, Snapshots, Injection, Payload, Wiz
  Compiler, Plugin Manager, Settings, QR Code, Files, …) with one tool's
  `RenderCore()` below. Labels are never abbreviated and the strip scrolls
  when it overflows.
- The calculator is a dedicated right side panel (default 360 px, resizable,
  toggleable from View > Toggle Calculator Panel or its Hide button). It is
  rendered through `session::ActiveTexture()`; the calculator face is untouched
  and keeps its aspect ratio; keyboard input is routed when the panel has
  focus.
- There is no activity bar and no breadcrumb: they had no real destinations.
- The bottom panel stays under the editor column only; the calculator panel
  spans the body height.

## 2. Multi-session runtime

`Gui/Shell/SessionHost.{h,cpp}` owns the live sessions:

- Session 0 is the emulator created by `main()` (registered by `test_gui`).
- "Open Model" creates further `Emulator` instances (`models/` chooser) and
  keeps them running on their own worker threads.
- `Activate()` points `m_emu` / `me_mmu` at the active session, applies
  `SetClockSpeed(1.0)` to the active session and `SetClockSpeed(throttle)` to
  inactive sessions (default 0.25).
- Secondary sessions render into their own offscreen composite texture. The
  shell copies the active secondary's pixels back into a streaming texture on
  the main renderer (`PrepareActiveFrame` + `ActiveTexture`).
- Input is routed to the active session (`active_emulator->UIEvent`).
- Debugger tools read `m_emu`/`me_mmu` per frame, so tools follow the active
  tab.
- Sessions are persisted in `settings.json` (`sessions[]`, including
  `modelPath`) and restored at launch; the saved active tab is re-selected.
- On exit the shell stops and joins every emulator worker. Secondary emulator
  objects are released rather than destroyed after SDL audio/renderer teardown;
  this avoids a macOS multi-audio-device teardown abort. This is a deliberate
  process-exit leak, not a runtime leak.

Limitations: the debugger window set is per-process, so switching between
models reuses the same tool objects. Tools that cache model geometry are reset
when the ROM changes. A full per-session tool ownership refactor is the next
structural step.

## 3. Settings

Files:

- `src/Settings/Settings.h/.cpp`
- Primary path: `~/Library/Application Support/CasioEmuMsvc/settings.json`
- Portable override: `<executable-dir>/settings/settings.json` when
  `portable.flag` exists.
- Writes are atomic (temp file in the same directory + rename). A parse or I/O
  failure logs a warning and leaves defaults active; startup never fails.

Schema 1 groups (all optional on read):

```json
{
  "schema": 1,
  "appearance": { "theme": "classic", "pluginTheme": null,
                  "uiScale": 1.0, "uiScaleOverridden": false,
                  "darkMode": true, "autoTint": false, "seedColor": [.5,.5,.5,1],
                  "themeExplicit": true, "skinVersion": 2 },
  "ui": { "hiddenTabs": [] },
  "shell": { "sidebar": {"visible":true,"width":300},
             "bottom": {"visible":true,"height":200,"active":"output"},
             "tools": {"visible":true,"width":360},
             "window": {"x":0,"y":0,"width":1600,"height":1000,"fullscreen":false} },
  "workspace": { "folder": null, "recentFolders": [], "recentFiles": [] },
  "sessions": [ { "id":"session-1", "model":"fx-580VN X",
                  "modelPath":"models/fx-580VN X", "rom":"rom.bin",
                  "title":"fx-580VN X", "active":true, "documents":[] } ],
  "editor": { "showLineNumbers": true, "wordWrap": false,
              "fontSize": 13, "tabSize": 4 },
  "calculator": { "injectionFilePath": "./hc-inj.txt", "scale": 1.0 },
  "emulator": { "backgroundThrottle": 0.25, "lowPerformanceMode": false },
  "integrations": { "discordRPC": true, "updateChecker": true },
  "plugins": { "enabled": [], "disabled": [], "trustPath": "plugin-trust.json" },
  "language": "en_US"
}
```

Compat: `calculator.scale` stays a mirror of `appearance.uiScale`;
`appearance.skinVersion` performs the one-time migration from the VS Code-skin
default back to `classic`.

### Migration from `theme.bin`

`ThemeManager::LoadSettings()` reads the binary theme store once when
`settings.json` is missing (or unreadable) and converts: `isDarkMode`, `scale`,
`language`, `injectionFilePath`, window geometry, `lowPerformanceMode`,
`enableAutoTint`, `seedColor`, `enableDiscordRPC`, `enableUpdateChecker`, and
`hiddenTabs`. The document is written atomically; `theme.bin` is kept as a
compatibility mirror for one release (Saved by `ThemeManager::SaveSettings`).

## 4. Themes

Themes are data in `resources/themes/*.json` (copied into the app bundle's
`Resources/themes` and next to non-bundle executables):

- `classic` (built in code: original CasioEmuMsvc dark palette) — default
- `vscode-dark-plus` (classic VS Code Dark+ look, blue status bar)
- `vscode-dark` (VS Code Dark Modern)
- `vscode-light` (VS Code Light Modern)
- `midnight-blue`, `solarized`, `mono-green`

`Plugin/ThemeTokens.cpp` parses the shared token schema (colors, metrics,
typography) for built-ins and theme plugins. Typography supports
`uiFamily`, `monoFamily`, `scale` (clamped 0.8–2.0); metrics and unknown color
keys remain backward compatible. `ThemeManager::ApplyThemeByName()` is the one
path for built-in presets; `PluginRegistry` uses the same parser for plugin
themes.

`classic` is not a JSON file: `ThemeManager::BuildClassicStyle()` builds the
original dark/light style and palette so the original component look always
works even if resources are missing.

## 5. Terminal

`src/Terminal/Terminal.{h,cpp}` implements a real PTY:

- `forkpty`, login shell (`$SHELL`, `/bin/zsh` fallback), workspace as cwd.
- Non-blocking reads on the UI thread; `TIOCSWINSZ` on resize.
- VT parser: cursor movement, erase display/line, SGR 16/256/truecolour
  (truecolour collapsed to nearest cube entry), alternate screen, bracketed
  paste, save/restore, scrollback (10k lines), UTF-8 best effort.
- Keys: Enter/Backspace/Tab/Esc/arrows/Home/End/Delete/PageUp/PageDown,
  Ctrl+C/D/Z/L/A/E/K/U/W/R, Cmd+V paste. Cmd+` focuses the terminal tab.
- Rendered with a dedicated 13 px monospace face.

## 6. Command palette and quick open

- Cmd+Shift+P: command palette (sessions, model/folder/plugin, view toggles,
  settings, build/run, pause/reset, themes, plugin manager, terminal).
- Cmd+P: quick open over the workspace (recursive walk, hidden/build folders
  skipped, 4k cap), Enter/double-click opens with the OS default handler.
- Both are headless-verifiable because they are plain ImGui overlays.

## 7. Fonts and glyph policy

- UI font is the **original** face: `GetMonospaceFontPath()` at 15 px × UI
  scale, with the original merge chain (Vietnamese, Cyrillic, Greek, Thai, CJK
  when enabled). `g_mono_font` and `g_small_font` alias that UI face so the
  terminal and chrome do not introduce a second font stack.
- No symbol font or emoji fallback is loaded. UI text uses ASCII only:
  `>`, `[+]`, `[-]`, `|`, `x`, `v`; the earlier geometric glyphs were removed
  to eliminate tofu boxes.
- The layout report asserts that the UI font and monospace face exist and that
  ASCII glyphs are present.

## 8. Verification

Headless (no display, no screenshots of the desktop):

```
build/.../CasioEmuMsvc --shot-startup=temp/shots/welcome.png --frames=20
build/.../CasioEmuMsvc --shot-shell=temp/shots/shell.png \
    --frames=30 "model=$CASIOEMU/models/fx-580VN X"
```

The shell mode writes `<shot>.layout.txt` with region rects and automated
checks: chrome/tab overlap, body/status overlap, sidebar/activity gutter,
panel-in-editor bounds, non-zero regions, status bar height, font presence and
ASCII glyph checks. `RESULT PASS` is required.

Functional self-check:

```
CASIOEMU_SELFTEST=/path/report.txt \
build/.../CasioEmuMsvc --shot-shell=/path/shot.png --frames=100 \
    "model=$CASIOEMU/models/fx-580VN X"
```

It opens a second live model, runs the Wiz build (Build succeeded), focuses the
Terminal and types `echo SELFTEST_OK`, then reports sessions/tabs/active/build
status/terminal and `RESULT PASS`.

`CasioEmuMsvc/tests/settings_smoke.cpp` covers settings round-trip, malformed
JSON fallback, atomic write, theme.bin migration, and structural validation of
all theme presets.

Last verified results:

- Build: green (`cmake --build build --target CasioEmuMsvc`).
- Layout report: PASS.
- Self-check: sessions=2, tabs=2, build=Build succeeded, terminal=PASS.
- Persistence: two sessions saved and restored (`sessions=2 tabs=2`).
- Startup screenshot: `Wiz/temp/shots/pass-welcome2.png`.
- Tool session: `Wiz/temp/shots/pass-tools.png` / `tools-final5.png`.
- Editor + lint/build overlay: `Wiz/temp/shots/pass-editor.png` / `release-editor.png`.
- Dirty close prompt: `Wiz/temp/shots/pass-dirty.png`.

## 9. VS Code fidelity checklist

Legend: **Done** = implemented and verified; **Partial** = implemented with a
documented limitation; **Blocked** = proven platform limitation.

| Item | Status | Notes / reference element |
| --- | --- | --- |
| Frameless window, native traffic lights | Done | `MacWindow.mm`; 35 px chrome row. |
| Chrome → tabs → breadcrumb stack | Done | 35 / 35 / 22 px; report checks no overlap. |
| Session tab strip, active tab connected | Done | Active fill = content colour, close on hover, `+`, right actions. |
| Session tabs full labels | Done | No abbreviation/truncation; tab width follows the label; strip overflows. |
| Activity bar | Removed | Had no real destinations (search/source-control/run/extensions do not exist). The sidebar starts at the window edge with 8 px inner padding. |
| Sidebar 300 px with 8 px gutter | Done | Explorer header, file tree, Open Folder, recent folders. |
| File tree chevrons/indent/selection | Done | ASCII `[+]`/`[-]`, 12 px indent steps, selection/hover. |
| Breadcrumb row | Removed | It looked clickable but had no destination. |
| Content: tool documents | Done | Full-label document tabs, scrollable; tool `RenderCore()` below. |
| Bottom panel Output/Problems/Terminal | Done | 200 px default, tab underline, close/collapse, resize drag. |
| Status bar left/right groups | Done | 22 px, classic colour, build/PC/encoding/lang/theme; no fake cursor fields. |
| Welcome page Start/Recent/Walkthroughs | Done | `final-welcome.png`; settings-session recents fallback. |
| Command palette Cmd+Shift+P | Done | Full command list including themes/build. |
| Quick open Cmd+P | Done | Workspace walk + filter. |
| Multi-session, two live models | Done | SessionHost; self-check sessions=2. |
| Inactive session throttling | Done | `SetClockSpeed(0.25)` on switch. |
| Input/tools follow active tab | Done | Active emulator routing + `m_emu`/`me_mmu` update. |
| Session persistence/restore | Done | `sessions[]` with `modelPath`; verified restore. |
| Real PTY terminal, job control | Done | forkpty, resize, SGR, Ctrl keys; self-check terminal=PASS. |
| Live theme switching | Done | Built-ins + plugin themes share `ApplyThemeDocument`. |
| Original UI font | Done | Original 15 px mono face restored; no system font. |
| No tofu / clipped chrome text | Done | ASCII-only chrome; atlas/glyph report; status text offset. |
| No orphan OS-level tool windows | Done | Tool dock hosts `RenderCore`; no ImGui dock nodes. |
| Calculator side panel | Done | Right 360 px default, resizable, toggleable from View/menu/tab-strip Calculator button, persisted in `shell.calculator`. |
| Wiz build & run in shell | Done | Self-check `build_status=Build succeeded`. |
| Plugin manager / trust modal | Done | Existing windows hosted in tool dock. |
| Native macOS app menu | Done | `MacWindow.mm::InstallNativeMenu` wires App/File/Edit/View/Session/Window menus to shell actions; in-window labels remain. |
| Theme plugin shell takeover | Done | Declarative theme/layout hooks are applied by `PluginRegistry::ApplyShellLayout`; native `ui_revamp` still requires a plugin fixture (host path exists). |
| Per-session tool ownership | Partial | Tools follow active session via globals; a tool does not keep per-session scroll state. |
| Full xterm edge cases | Partial | Common CSI/SGR/alt-screen/scrollback implemented; mouse reporting and selection search are not. |

## 10. Round-1 and round-2 feedback

Round 1 (layout/visual):

| Item | Status | Fix |
| --- | --- | --- |
| Sidebar flush against window | Pass | 48 px activity bar + 8 px inner gutter; report check. |
| Font glitches / tofu | Pass | Original font restored, ASCII chrome, symbol ranges removed. |
| Chrome/tab overlap | Pass | 35/35/22 stack; automated overlap check. |
| Stray window | Pass | Tool dock hosts `RenderCore`; calculator in content; single OS window. |
| Information architecture | Pass | EXPLORER sidebar, full-label tool-document tabs, calculator side panel, panel tabs, status groups. |
| Crash / no relaunch | Pass | Preview relaunched and left running; headless multi-session teardown uses a joined then released worker set to avoid the SDL-audio abort. |

Round 2 (component skin), verified against `Wiz/temp/shots/final2-welcome.png`
and `final2-shell.png`:

| Item | Status | Fix |
| --- | --- | --- |
| Original UI font, not SF Pro | Pass | `RebuildFont` restored to mono 15 px; `g_mono/g_small` alias. |
| Text glitches | Pass | ASCII-only chrome, original atlas, status offset, centered tabs. |
| Invisible tab text | Pass | Active foreground, inactive 72% foreground, vertical centering. |
| Top bar text too far right | Pass | Title at 12 px after traffic lights; menu labels follow. |
| Keep original component skin | Pass | `classic` theme is default; VS Code presets opt-in. |
| Remove fake icons | Pass | Activity bar removed; tabs/panel/status use text only. |

## 10b. Removed VS Code-clone stubs (2026-11-03)

Nothing in the UI is a placeholder. Removed because there is no real
CasioEmuMsvc capability behind it:

- Activity bar and its `EX / SR / SC / RN / PL / SET` labels. Search, Source
  Control, Run and Debug, and Extensions do not exist as real tools.
- Breadcrumb row (`CasioEmuMsvc > session > workspace`): it looked clickable
  but did nothing.
- VS Code welcome "Walkthroughs" links and the launcher "Install Plugin..."
  message-only entry.
- Sidebar "OPEN EDITORS / No open editors" empty state.
- Top bar `Edit` and `Help` menus (no real actions; the native Edit menu is the
  standard responder chain, and Help had only a palette shortcut).
- Status bar `Ln/Col` and `LF` (no editor cursor or EOL model exposed).
- Settings > Editor tab (line numbers/wrap/font size were not consumed by the
  embedded editor). The JSON fields remain for the future editor work.
- The VS Code skin as the default; `classic` is restored. VS Code presets
  remain selectable themes.

Real destinations kept: File > Open Model/Folder/Settings/Quit, View toggles and
command palette, Session > Build / Build & Run / Pause / Reset, native App/Edit/
File/View/Session/Window/Help menus, session tabs, tool-document tabs, explorer,
Open Folder/Refresh, calculator Hide/toggle, Output/Problems/Terminal,
Plugin Manager, Settings, About, Documentation URL.

## 10c. Round-3/4 tab/layout/no-dead-UI feedback

| Rule | Status | Evidence |
| --- | --- | --- |
| Every tab shows its full text label | Pass | Session tabs use model titles; document tabs use `UIWindow::name` (Code Viewer, Functions, Memory, …). No `min(240)` cap; width = text + padding. |
| Label contrast, no clipping, vertical centering | Pass | Active foreground, inactive 72 % foreground, centred with `(35 - textHeight)/2`; strip scrolls. |
| No ellipsis/truncation | Pass | Tab width is computed from the full label; document strip has a horizontal scrollbar. |
| No placeholder icons | Pass | Activity bar removed; tabs/menus/buttons have no invented glyphs. |
| No dead UI (every item acts) | Pass | Removed activity bar, breadcrumb, Walkthroughs/Install-Plugin stubs, Open Editors state, Edit/Help menus and Ln/Col/EOL status fields; see §10b. |
| Main content = old tools/documents | Pass | `RenderToolDocuments` hosts tool `RenderCore()` in the editor column with a full-label scrollable document strip. |
| Calculator in a side area | Pass | `RenderCalculatorPanel`, right side, aspect-preserving, keyboard focus, toggle + width persisted in `shell.calculator`. |
| Sidebar/bottom panel/session tabs retained | Pass | Explorer left; Output/Problems/Terminal bottom; sessions top. |
| Original tab/menu/button style | Pass | `classic` palette and original mono UI font; VS Code presets opt-in. |
| No fake activity abbreviations | Pass | Activity bar removed entirely. |
| Consistent horizontal padding on lists | Pass | Explorer rows at 12 px, recent rows at 8–10 px with reduced selectable width so the selection background is inset. |

## 11. File map

- `src/Gui/Shell/Shell.{h,cpp}` — layout, welcome, sidebar, panels, palette,
  quick open, selftest hooks.
- `src/Gui/Shell/SessionHost.{h,cpp}` — live sessions, activation, throttling,
  offscreen mirror.
- `src/Settings/Settings.{h,cpp}` — JSON model, paths, atomic save, migration.
- `src/Plugin/ThemeTokens.{h,cpp}` — shared theme parser.
- `src/Gui/ThemeManager.{h,cpp}` — style application, classic/preset palettes.
- `src/Gui/Theme.cpp` — five settings categories.
- `src/Terminal/Terminal.{h,cpp}` — PTY + VT parser + renderer.
- `src/Ext/MacWindow.{h,mm}` — frameless Cocoa bridge.
- `src/Ext/Screenshot.{h,cpp}` — headless PNG capture.
- `src/Gui/Ui.cpp` — shell hook, font globals, plugin shutdown hook.
- `src/casioemu.cpp` — headless shot/selftest flags, active-session routing,
  shell shutdown.
- `src/StartUpUi/StartupUi.cpp` — welcome launcher.
- `src/Gui/WizToolchain.cpp` — build log/diagnostic bridge, build requests.
- `resources/themes/*.json`, `CasioEmuMsvc/locales/en_US.lc`,
  `CasioEmuMsvc/tests/settings_smoke.cpp`, `CasioEmuMsvc/CMakeLists.txt`.

## 12. Original shell/theme/settings behaviour (before this rewrite)

- One SDL window per emulator + a separate debugger window; `test_gui` created
  the dock space and every tool as a floating/docked `UIWindow`.
- `windows` was a global vector; `m_emu`, `me_mmu`, `calculator_panel_focused`
  were process-wide.
- `ThemeSettings` (`theme.bin`, magic `0x47464354`) held dark/light ImGui
  styles, scale, language, injection path, geometry, low-performance,
  Discord/update toggles and hidden tabs. `ThemeManager` was a singleton that
  saved the binary blob at `./theme.bin`.
- Startup was a separate SDL/ImGui window (`sui_loop`) with a flat model table.
- No JSON settings, no session model, no PTY, no command palette.


## 13. Preview build

- Bundle: `preview/CasioEmuMsvc.app` (repo-relative)
- Launch log: `temp/logs/preview-launch.log`
- Launch command: `CasioEmuMsvc.app/Contents/MacOS/CasioEmuMsvc "$CASIOEMU/models/fx-580VN X"`
- Last verified running instance: PID 80585 (left running for the user). It
  restores the saved sessions from `settings.json`. Log:
  `temp/logs/preview-launch.log`.

The plugin work added a deterministic shutdown hook:
`gui_cleanup()` calls `casioemu::plugin::PluginRegistry::Instance().Shutdown()`
before ImGui/SDL teardown, so native plugins unload first (idempotent; the
registry also has an `atexit` fallback).

## 14. File explorer and editor

Explorer (`RenderSidebar`/`RenderSidebarTree`):

- Root at the persisted workspace; Open Folder uses the native picker.
- Live directory scan (changes on disk appear on the next frame); Refresh clears
  the quick-open cache and error state.
- Expand/collapse directories, double-click text files to open them in the
  editor, other files use the OS default handler.
- Context menu: New File, New Folder, Rename, Delete (confirmation modal),
  Reveal in Finder, Copy Path.
- Keyboard: Enter rename, Cmd+Backspace delete, Cmd+N new file,
  Cmd+Shift+N new folder (when the explorer is hovered).
- Errors are reported in Output and the sidebar string; no silent failures.

Editor (`RenderFileEditor`):

- `TextEditor` is reused; a Wiz/CWiz language definition supplies keywords,
  preprocessor/directive highlighting, strings, numbers and comments.
- Files open as tabs in the **same top tab strip** as sessions. Each document
  keeps its own `TextEditor` (cursor, scroll, undo history). Dirty tabs show
  ` *`; close is X-on-hover, middle-click, Cmd+W or the tab X.
- Dirty close shows a Save / Discard / Cancel modal (headless screenshot
  `pass-dirty.png`).
- Save, Save As, Undo, Redo, Find, Ln/Col; Cmd+S and Cmd+F.
- Lint on open/save and after a 650 ms typing debounce using
  `wiz::lint_source`; markers are set on the editor and diagnostics replace
  that file's entries in Problems. Clicking a diagnostic opens the file and
  jumps to line/column.
- `.wiz`/`.cwiz` files show a build overlay: Target, Language, Lint, Build,
  Build & Run, Preprocess (four compact toolbar rows). Build transfers the
  buffer to the existing WizToolchain engine, writes output next to the model
  and streams logs to Output/Problems.
- Wiz Compiler is no longer a tool-document tab.

## 15. Visual pass and duplicate audit

- Global ImGui style: 8 px window padding, 8×4 frame padding, 8×6 item
  spacing, 8×3 cell padding, 16 px indent, 1 px borders, 4/6 px rounding,
  10 px scrollbars. Shell regions use explicit 8–16 px insets.
- Session and tool-document tab bars are 26 px, full labels, 12–15 px label
  inset, 1 px separators, top-corner rounding, accent line on active tabs,
  hover, X on hover for file/session tabs.
- Checkbox flow helpers wrap instead of clipping; Variables puts its four
  checkboxes on two rows. Hardware/Watch/Injection rows use `SameLineOrWrap`.
- Display Address / Display Hex / Show ImP / Always Show ImP are persisted in
  `ui.*` and reload on startup; the settings self-check covers the JSON
  round-trip.
- Duplicate audit on the visible screen:
  - Calculator toggle kept in the View menu + the calculator's Hide button;
    removed from the tab strip.
  - Settings kept as one document tab; removed the extra top toolbar button.
  - Build/Run kept in the editor overlay when a file is active; hidden from the
    top toolbar on that screen.
  - Session `+` is the only visible new-session entry; File menu is the native
    fallback.
  - Startup Recents and All Models no longer repeat the same model.
  - Removed activity bar, breadcrumb, Walkthroughs, Install-Plugin stub,
    Open Editors stub, Edit/Help menus, Ln/Col/LF fake status fields.
- Terminal startup skips user rc files and installs a `%1~ %#` prompt so the
  workspace path does not wrap or leave stray status markers.

## 16. Final interaction/alignment pass

- Splitters: visible 1 px sidebar/calculator vertical dividers and bottom panel
  horizontal divider with resize cursors, min/max clamps, double-click reset
  (sidebar 300, calculator 360, panel 200), persisted in `settings.json`.
  Document tool tabs can be dragged to reorder; order is kept in memory.
- Selection: active tabs use the lighter `tabActive` colour; tree/list/menu
  selections use the lighter `selection` colour, not a darker shade.
- Tool tab strip: fixed 30 px strip, widths from full labels, tabs outside the
  viewport are not partially painted, horizontal slider (drag + wheel) with
  auto-scroll to the active tab.
- Tab close: the X hit area is inside the tab item (no overlap with the tab
  selectable), middle-click closes, dirty files open Save/Discard/Cancel.
- Explorer: explicit New File / New Folder / Refresh / Open Folder buttons,
  context menu targets the row it was opened on, selection highlight, keyboard
  shortcuts. Workspace is scanned live.
- Top chrome is full width and 38 px; title/menu baseline at 8 px from the top,
  right actions right-aligned with a 12 px margin and vertically centred.
- Problems: responsive two-line rows on narrow editor columns (severity + code,
  wrapped message, file:line:col), table columns at wide widths; counted
  summary, status-bar error/warning counts, click-to-jump, hint tooltips.
- Update available is a compact 460 px card (title, version, one-line note,
  Open release page / Later).
- Lint uses `wiz::lint_source` for both `.cwiz` and `.wz` (all compiler + style
  rules), captures rule/code/severity/hint/end column, runs on open/save and a
  650 ms debounce, marks the gutter line, and reports a visible `linter failed`
  diagnostic if the engine throws. The linter is linked in-process
  (`wizcore`), so no external `wizlint` lookup is required.
- Build stamp: `CASIOEMU_BUILD_COMMIT` + `CASIOEMU_BUILD_TIME` from CMake is
  shown in the status bar and welcome status, e.g. `build 563130a 11-03 14:23`.

### Launch pipeline

The preview bundle is replaced with `ditto`, then SHA-256 of the built and copied
executables is compared before launch. Current verified executable:
`/Users/kin/Desktop/Wiz/preview/CasioEmuMsvc.app/Contents/MacOS/CasioEmuMsvc`
SHA-256 `4a6610d5c45db26a033862e5c62a1d78e76e8dd00ce8281576039c4adbbbc8cf`,
mtime `2026-11-03 21:31:18`, PID `23741`.

### Evidence

- `Wiz/temp/shots/final-update.png` — compact update card.
- `Wiz/temp/shots/final-welcome3.png` — Settings tab, aligned Recents.
- `Wiz/temp/shots/final-recents-crop.png` — Recents alignment crop.
- `Wiz/temp/shots/final-topleft-crop.png` — top-left chrome crop (native
  traffic lights are Cocoa-drawn and are not present under the headless SDL
  dummy driver; title/menu baseline is verified in the drawn chrome).
- `Wiz/temp/shots/final-tools4.png` / `final-explorer-crop.png` — explorer
  header, tool tabs, calculator side panel.
- `Wiz/temp/shots/probs-final.png` — Problems columns/counts and status counts.
- `Wiz/temp/logs/end-selftest.txt` — full functional self-check `RESULT PASS`.
