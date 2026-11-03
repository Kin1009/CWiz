# Shell fixes (legacy plan 10.1–10.5, 3.1)

Batch implemented against the CasioEmuMsvc preview tree (the sibling
`CasioEmuMsvc-Mac` checkout; `$CASIOEMU` in `INDEX.md`).
The Wiz compiler, `third_party/wiz` and `Gui/WizToolchain.cpp` were left to the
agent that owns them.

| Item | Result |
| --- | --- |
| 10.3 Return alias | Explicit `return_alias_active` flag; alias gated on it; decision logged; `Ret=Exe` hint in the Calculator controls strip |
| 10.4 Multi-touch release | Conditional global release restored; active-finger set added; keyboard hold counters reconciled only when no fingers and no host keys are down |
| 10.5 Focus timing | No unconditional clear at the top of `gui_loop()`; flag cleared only when the Calculator panel is gone/closing/not rendered, still set from `ImGui::IsWindowFocused()` |
| 3.1 Window rename | User-visible window title is now `Injection`; class/file/API identifiers untouched |
| 10.1/10.2 Plugin loader | Verified genuinely implemented end to end on macOS; no code change (no provable bug found) |

---

## 10.3 — RETURN alias is now explicit

### Problem

Every model that did not define its own Return binding received the implicit
alias `SDLK_RETURN -> SDLK_SPACE` (EXE). The alias was invisible: the code, the
logs, and the UI could not distinguish "this model really binds Return" from
"Return was silently borrowed from Space". That also made it impossible to
advertise the behaviour to the user.

### Change

Files/functions:

- `CasioEmuMsvc/src/Peripheral/Keyboard.cpp`
  - `Keyboard::Initialise()` (`init_kbd` block)
  - new member `bool return_alias_active`
  - new override `Keyboard::ReturnAliasActive() const`
- `CasioEmuMsvc/src/Peripheral/Keyboard.hpp`
  - `IKeyboardAutomation::ReturnAliasActive()` (default `false`)
- `CasioEmuMsvc/src/Gui/CalcScreenWindow.cpp`
  - `CalcScreenWindow::DrawControls()`

The flag is reset to `false` on every keyboard initialisation. It becomes
`true` only inside the branch that installs the Return alias:

```cpp
if (keyboard_map.find(SDLK_RETURN) == keyboard_map.end()) {
    auto exe_key = keyboard_map.find(SDLK_SPACE);
    if (exe_key != keyboard_map.end()) {
        keyboard_map[SDLK_RETURN] = exe_key->second;
        return_alias_active = true;
        SDL_Log("[Keyboard][Info] No explicit Return binding; Return aliases to Space (EXE)");
    }
}
```

The keypad-enter alias is unchanged and is not represented by this flag; the
legacy plan only asked about Return. A model with an explicit Return binding
therefore keeps that binding and reports `ReturnAliasActive() == false`.

The app has no existing user setting for keyboard mappings (Settings covers
theme, language, background/injection and performance), so the flag stays
internal. The decision is logged once per keyboard initialisation (model load),
not per key event.

When the flag is true, the Calculator controls strip shows a muted `Ret=Exe`
label after the resolution, with a tooltip explaining that Return acts as EXE
because the model has no explicit Return binding.

### Before / after

- Before: Return silently mapped to Space for models without Return; no log, no
  UI, no way to know why Return "did the wrong thing" on a model that binds it.
- After: the alias exists only when the model has no explicit Return binding;
  the log states the decision; the Calculator panel advertises `Ret=Exe`.

### Manual test

1. Launch a model whose EXE button uses `Space` and whose config has no Return
   key name. Look for `[Keyboard][Info] No explicit Return binding; ...` in the
   terminal/log and for `Ret=Exe` in the Calculator controls strip.
2. Press Return on the Calculator panel; the calculator should execute as EXE.
3. Launch a model that binds `Return` explicitly. The log line and `Ret=Exe`
   hint must be absent, and Return must trigger the model's own binding.
4. Reload/switch models and confirm the hint follows the newly loaded model.

### Risk

Low. Only the Return alias branch changed; the keypad-enter alias and all
explicit model bindings are untouched. The UI hint is read-only and optional.

---

## 10.4 — Multi-touch release and keyboard hold counters

### Problem

`ReleaseAll()` in the `SDL_MOUSEBUTTONUP` path had been commented out when
multi-touch support was added, because a global release could destroy a button
another finger was holding. The consequence is that input can stay latched:

- a finger that slides off a button before lifting generates `SDL_FINGERUP`
  away from the button, so `ReleaseAt()` finds nothing and the button remains
  pressed forever;
- the same happens for a mouse released away from the key it pressed;
- `keyboard_button_holds` can become stale if a `KEYUP` is missed while the
  window is unfocused, so the next `KEYDOWN` sees a phantom second holder and
  never calls `PressButton()`.

### Change

File/functions: `CasioEmuMsvc/src/Peripheral/Keyboard.cpp`

- new member `std::unordered_set<SDL_FingerID> active_fingers`
- `Keyboard::UIEvent()` — insert on `SDL_FINGERDOWN`, erase on `SDL_FINGERUP`
- `Keyboard::ReleaseKeyboardKeys()` — clears `active_fingers` on focus loss
- new `Keyboard::ReleaseAllIfNoInputHeld()`
- `SDL_FINGERUP` calls it after `ReleaseAt()`
- `SDL_MOUSEBUTTONUP` (left button, release not matched to a button) calls it
  where the old `ReleaseAll()` was commented out

Exact condition chosen:

```cpp
void Keyboard::ReleaseAllIfNoInputHeld() {
    if (!active_fingers.empty() || !held_keyboard_keys.empty())
        return;
    std::fill(std::begin(keyboard_button_holds),
              std::end(keyboard_button_holds), 0);
    ReleaseAll();
}
```

Why this condition:

- `active_fingers` answers "is any finger still on the glass?" using SDL finger
  IDs, not button state. Button state alone cannot detect a finger that slid
  off its key, because the stale `pressingFingerId` still points at it.
- `held_keyboard_keys` is the set of host keys currently down. If it is empty,
  no host key can be holding a calculator key, so clearing the per-button hold
  counters and calling `ReleaseAll()` cannot break a single-key or multi-key
  keyboard hold (`keyboard_button_holds` counts several host keys on one
  calculator key; those counters are only decremented by `HandleKeyboardKey`).
- Both must be empty. Releasing the last finger while a host key is down does
  nothing, and vice versa, so mixed keyboard + touch/mouse holds survive.
- When both are empty, any nonzero `keyboard_button_holds` entry is stale
  bookkeeping. Zeroing it prevents a later `KEYDOWN` from being ignored as if
  another host key still held the key. `ReleaseAll()` then releases any button
  orphaned by the finger/mouse that lifted away from it.
- `ReleaseAll()` still skips stuck buttons, so right-click latching and
  finger-latched keys are preserved.

### Before / after

- Before: lifting a finger/mouse away from a key could leave it pressed; a lost
  `KEYUP` could make the next press of that key a no-op; the commented
  `ReleaseAll()` meant no fallback cleanup.
- After: once all fingers and all host keys are up, orphaned presses are
  released and counters are reconciled. Single-finger, multi-finger, single-key
  and multi-key holds all remain intact while any holder is still down.

### Manual test

1. Press a calculator key with the mouse, drag the cursor off the key (and off
   the calculator image), release. The key must not stay latched.
2. Hold two fingers on two keys (touch device), lift one. The other stays
   pressed. Lift the second; both are clear.
3. Hold a host key mapped to a calculator key, click and release a mouse key,
   then release the host key. The host-held key must stay pressed until its own
   `KEYUP`.
4. Hold two host keys mapped to the same calculator key, release one; the key
   stays pressed until the second is released.
5. Alt-tab away while holding a key, return, press the same key again. It must
   press normally (no phantom hold counter).

### Risk

Medium-low. Global release now runs in two additional places, guarded by the
condition above. The one untracked case is a mouse button physically held while
the last finger lifts: `ReleaseAll()` does not know about that mouse hold and
could release its key. Mixed touch + mouse-hold on two different keys at the
same instant is unusual on this desktop app; keyboard holds are explicitly
protected.

---

## 10.5 — Calculator focus timing

### Problem

`gui_loop()` began by forcing `calculator_panel_focused = false` and then let
the Calculator render path set it from `ImGui::IsWindowFocused()`. The flag is
read by the SDL event loop after `gui_loop()`, so the unconditional clear made
the value depend on the render ordering: whenever the Calculator window was not
rendered in that frame (closed, collapsed, not the selected dock tab) the event
loop saw `false` even if it had been focused a moment earlier; conversely a
stale value could survive frames where the panel did not render at all.

### Change

Files/functions:

- `CasioEmuMsvc/src/Gui/Ui.cpp`
  - new `HasOpenCalculatorPanel()`
  - `gui_loop()` no longer clears unconditionally
- `CasioEmuMsvc/src/Gui/Ui.hpp`
  - `UIWindow::Render()` for the window named `Calculator`

`gui_loop()` now clears only when there is no open Calculator window:

```cpp
if (!HasOpenCalculatorPanel())
    calculator_panel_focused = false;
```

The Calculator render path still sets the flag while its window is visible:

```cpp
if (visible) {
    if (is_calculator)
        calculator_panel_focused = ImGui::IsWindowFocused();
    RenderCore();
}
else if (is_calculator) {
    calculator_panel_focused = false;
}
```

The `else` is required because a docked but non-selected tab (or a collapsed
window) makes `ImGui::Begin()` return `false` and `RenderCore()` never runs;
without it a previously true value would stick. The close-button path also
explicitly sets the flag to `false` before resetting to the main menu.

### Before / after

- Before: flag cleared at the top of every GUI frame, then rebuilt only if the
  Calculator happened to render; event routing could observe a value that did
  not reflect the panel the user was actually in.
- After: the flag is only cleared when the Calculator is missing, closing, or
  not visible; otherwise it carries the latest value produced by
  `ImGui::IsWindowFocused()`. The SDL event loop, which runs after `gui_loop()`
  in `casioemu.cpp`, uses that latest value.

### Manual test

1. Click inside the Calculator panel; keys must reach the emulator.
2. Click the Code Viewer / an editor tab; typing must go to the editor and not
   the calculator. Click back into Calculator; keys reach the emulator again.
3. Drag the Calculator tab out of its dock node or hide it behind another tab;
   key events must not reach the calculator while it is not visible/focused.
4. Close the Calculator tab with its X: the model resets to the main menu and a
   key event immediately afterwards must not be routed to the calculator.
5. With two OS windows (non-single-window builds), focus the calculator OS
   window and type; focus the debugger OS window and type; routing must follow
   the focused window/panel.

### Risk

Low-medium. The focus flag is the routing switch for keyboard events. The
visible-window `else` branch is deliberately conservative (clear when not
rendered), matching the old per-frame clear for that case, while preserving the
"latest value" behaviour while the panel exists.

---

## 3.1 — "Rop Compiler" window renamed to "Injection"

### Problem

The compiled-injection window was still labelled `Rop Compiler` even though it
is the generic injection editor.

### Change

Files:

- `CasioEmuMsvc/src/Rop/RopCompilerUI.cpp` — `CreateRopCompilerWindow()`'s
  `UIWindow("Rop Compiler")` constructor argument is now `UIWindow("Injection")`.
- `examples/plugins/layout-compact/assets/layout.json` — the sample layout's
  `hidden` list now references `"Injection"` so the example keeps hiding the
  renamed window.

Deliberately unchanged: the file name, class name, `CreateRopCompilerWindow()`,
the `RopCompiler.*` localization keys and all internal identifiers. Comments in
`Gui/WizToolchain.cpp`/`docs/WIZ-TOOLCHAIN.md` still say "Rop Compiler"; those
files are owned by another agent and the comments are not user-facing.

No repository test asserted the old title (searched the whole tree, excluding
`.git` and `build`).

### Before / after

- Before: dock tab/window menu showed `Rop Compiler`.
- After: dock tab/window menu shows `Injection`; the plugin layout example
  hides `Injection`.

### Manual test

1. Open the dock/window menu; the tab must read `Injection`.
2. Open the sample `layout-compact` plugin; it must still hide the injection
   window (the name in its layout config matches).
3. Confirm the window still compiles/injects and all `RopCompiler.*` strings
   remain translated normally.

### Risk

Low for functionality; any third-party plugin layout that hardcodes the old
window name `Rop Compiler` will need to use `Injection`. The shipped example
was updated. ImGui's saved dock layout stores the window name, so an existing
`imgui.ini` may place the old-named window and the new `Injection` window
separately until the layout is rebuilt; no data is lost.

---

## 10.1 / 10.2 — macOS plugin loader verification

The macOS loader is genuinely implemented. No rewrite or fix was made because
no provable bug was found in the reviewed paths.

### Discovery

`CasioEmuMsvc/src/Plugin/PluginRegistry.cpp`

- `Initialize()` is called from `LoadPlugins()` in
  `CasioEmuMsvc/src/Plugin/PluginMan.cpp` (the `__APPLE__` branch), which is
  called from `casioemu.cpp` before the main loop.
- `ScanDirectory()` walks, in order:
  1. `UserPluginsDir()` = `~/Library/Application Support/CasioEmuMsvc/Plugins`
  2. `AppPluginsDir()` = `<executable dir>/plugins`
  3. `<executable dir>/../Resources/plugins` (the `.app` Resources folder)
- Only regular files with the case-insensitive extension `.plugin` are read.
- Duplicate manifest IDs are ignored with a warning; the first scan wins (user
  plugins before bundled ones).

### Zip / package handling

`CasioEmuMsvc/src/Plugin/PluginPackage.cpp`

- `ComputeFileSha256()` hashes the package with CommonCrypto SHA-256.
- `ExtractPackage()` caches by hash under
  `~/Library/Application Support/CasioEmuMsvc/plugin-cache/<sha256>/`.
- Uses miniz to enumerate and extract entries.
- Rejects absolute paths, backslashes, `.`/`..` components and symlink entries
  via `IsSafeRelativePath()` and the zip external attributes.
- Extracts into a temp directory and renames it into place; a cached package is
  reused when `plugin.json` already exists.
- Loads and validates `plugin.json` with `LoadManifestFile()`.

### Manifest

`CasioEmuMsvc/src/Plugin/PluginManifest.cpp`

- Requires schema 1, a reverse-DNS-ish ID, name, semver version, type,
  `entry_point`, and validates `native_entry`, assets, permissions,
  `api_version` (1..`kHostApiVersion` = 2), `min_host` against 2.1.0, and
  `platforms` (only `macos`).
- Native code is only allowed for `tab`, `tool` and `ui_revamp` manifests;
  themes and inject plugins must not ship native code.

### Trust

`CasioEmuMsvc/src/Plugin/PluginRegistry.cpp`

- Trust store: `~/Library/Application Support/CasioEmuMsvc/plugin-trust.json`
  (schema 1, records keyed by plugin ID with version, SHA-256, permissions,
  enabled/blocked).
- First sight of a plugin waits for approval in the `Plugin Trust` modal
  (`RenderTrustModal()`).
- A changed hash or permission set re-prompts. A native plugin with a changed
  hash always re-prompts; declarative plugins may be auto-accepted after a hash
  change if permissions did not change (explicit, logged policy).
- Blocked plugins are not loaded and stay blocked until reviewed.

### Native loading

`CasioEmuMsvc/src/Plugin/PluginRegistry.cpp`, `LoadNative()`

- Resolves `package.root / manifest.nativeEntry`, checks existence.
- `dlopen(..., RTLD_NOW | RTLD_LOCAL)`.
- `dlsym(handle, "fPluginLoad")`; missing symbol closes the handle and marks the
  entry as an error.
- Calls `fPluginLoad(g_pluginapi)` inside try/catch; failures are logged and the
  handle is closed.
- Successful handles are stored on the entry and `dlclose`d in `Shutdown()`.
- Native `ui_revamp` failures fall back to the declarative `entry_point`.

### How to test manually

1. Zip a plugin directory containing `plugin.json` with the top-level files at
   the archive root, name it `something.plugin`, and drop it in
   `~/Library/Application Support/CasioEmuMsvc/Plugins`.
2. Start the app. The Plugin Log window should log the discovery path, cache
   extraction, trust state, and finally `Loaded ... plugin <id>`.
3. Accept the trust prompt; restart and confirm it loads without prompting.
4. Change a file in the zip, rebuild it, restart: a declarative plugin with
   unchanged permissions is auto-accepted and logged; a native plugin must
   prompt again.
5. Put a malformed/`../`-escaping package in the folder and confirm it is
   rejected with `package.path` / `package.invalid_zip` and never extracted.
6. For a native plugin, confirm the built `.dylib`/bundle is inside the package
   and exports `fPluginLoad`.

No loader code was changed in this batch.

---

## How keyboard, focus and event routing work today

### Event loop and ordering

`CasioEmuMsvc/src/casioemu.cpp`

- The main loop blocks in `SDL_WaitEventTimeout(&event, frame_wait_ms)` (16 ms
  normally, 24 ms in low-performance mode).
- On timeout it synthesises a `frame_event`, renders the emulator, and calls
  `gui_loop()` (in `SINGLE_WINDOW` builds before `emulator.Frame()`).
- Real SDL events are handled one at a time and immediately; they are not
  batched behind the render.
- For key/text/mouse events the loop decides the target:
  - key/text events go to the emulator if the event belongs to a separate
    calculator OS window, or if `calculator_panel_focused` is true (the
    single-window docked Calculator case);
  - otherwise the event is offered to ImGui (`ProcessImGuiEvent`), and mouse
    events are also forwarded to the emulator when ImGui does not want the
    mouse.
- `SDL_TEXTINPUT` is deliberately not forwarded to the emulator; only
  `SDL_KEYDOWN`/`SDL_KEYUP` reach `Keyboard::UIEvent()`. This prevents editors
  from receiving printable text meant for the calculator and avoids double
  character insertion.

### Focus flag

`CasioEmuMsvc/src/Gui/Ui.cpp`, `Ui.hpp`

- `calculator_panel_focused` is a global owned by the UI thread.
- `UIWindow::Render()` sets it only for the window whose name is exactly
  `Calculator`, from `ImGui::IsWindowFocused()` while `ImGui::Begin()` returned
  visible. It clears the flag when that window is present but not visible
  (unselected dock tab, collapsed) and when it is closing.
- `gui_loop()` clears the flag only when no open Calculator window exists, then
  renders all windows. The SDL loop reads the value afterwards, so it routes
  events using the latest computed focus.

### Keyboard input

`CasioEmuMsvc/src/Peripheral/Keyboard.cpp` / `Keyboard.hpp`

- `Keyboard::UIEvent()` handles `SDL_KEYDOWN`/`SDL_KEYUP` (plus F11/F12
  shortcuts) and mouse/touch events.
- `keyboard_map` is built in `Initialise()` from each model button's
  `keyname`, plus the automatic keypad/Return aliases. Key codes are looked up
  through SDL's key-name parser.
- `held_keyboard_keys` maps each currently down host keycode to the calculator
  button slot it holds. `keyboard_button_holds[slot]` counts how many host keys
  hold that same calculator key.
  - On key down: insert into `held_keyboard_keys` (rejects SDL auto-repeat),
    increment the slot counter, and call `PressButton()` only on the 0 -> 1
    transition.
  - On key up: erase the host key, decrement the slot counter, and call
    `TryReleaseButton()` only on the 1 -> 0 transition. This is what preserves
    multi-key holds and host-key aliases.
- `ReleaseKeyboardKeys()` is called on `SDL_WINDOWEVENT_FOCUS_LOST`; it clears
  the held-key map and all slot counters, releases the affected buttons (not
  stuck ones), and now also clears `active_fingers`.
- Mouse/touch use a separate path: `PressAt()`/`ReleaseAt()` map coordinates to
  a button and record `pressingFingerId` (-1 for mouse). Right-click (`stick`)
  latches a button. `active_fingers` tracks SDL finger IDs so the global
  release in `ReleaseAllIfNoInputHeld()` can tell when the last finger is gone.
- `ReleaseAll()` releases every pressed, non-stuck button. Stuck (latched)
  buttons require another stick press to clear.

### Persistence and plugin automation

- `Keyboard` exposes `IKeyboardAutomation` via `QueryInterface`, used by the
  web core and `Plugin/PluginApi_Impl.cpp` (`Key`, `ReleaseAll`, `PressCode`);
  `ReturnAliasActive()` is an added read-only query on the same interface.
- Snapshots save the low-level keyboard registers (`SaveState`/`LoadState`),
  not UI press state; the UI press state is rebuilt from live input.
