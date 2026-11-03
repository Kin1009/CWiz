# Plugin platform status: promised vs implemented

Host: `2.2.0`, API `3`, manifest schema `1`.
Source of truth: this working tree (`CasioEmuMsvc/src/Plugin/`, `examples/plugins/`).
Verification: `build-plugin/` (private build dir) and the two CTest targets
`plugin_loader_smoke` / `plugin_registry_headless`.

Status legend: **Closed** = implemented, documented and covered by a test or a
reproducible command; **Partial** = implemented but with a documented host limitation;
**Blocked** = cannot be completed in this tree (only with a proven external blocker).

| # | Promised behavior | Status | What actually happens now | Evidence |
| --- | --- | --- | --- | --- |
| 1 | Permissions are enforced | **Closed** | `PluginPermissions.cpp` maps the ten permissions to capabilities (`ui.shell`, `ui.window`, `theme`, `inject`, `emulator.read/write/code`, `filesystem.read/write`, `network`, `process`, `clipboard`, `native`). Loader gates check type capabilities; every `PluginApi` entry point goes through `ScopedPluginApi`/guards. Denials return null/default, log the API/capability, and set the entry to `Loaded (limited)` with a tooltip. | `plugin_loader_smoke` (mapping/denial), `plugin_registry_headless` (loader denial, `Loaded (limited)`), manager tooltip. |
| 2 | Unload/reload a native plugin | **Closed** | `PluginRegistry::Unload` calls `fPluginUnload`, removes only plugin-tracked windows, drops payloads/state, and `dlclose`s when safe. Disable/Remove unload too. Enable reloads. Libraries with registered `Hooks` are retained (documented, no dangling callbacks). `Shutdown()` exists and is registered with `std::atexit`; safe twice. | `plugin_registry_headless` (unload marker, window removal, re-enable, library-retained path), `plugin_loader_smoke` (`fPluginUnload` dispatch). |
| 3 | `inject` payloads are parsed and registered | **Closed** | `ParseInjectDocument` validates `id`, `name`, `description`, `target`, `address`, `bytes`, `safe`; invalid documents fail with `inject.invalid` (no silent `Loaded`). Valid payloads go to the host registry and render under **Plugin payloads** in the Payload window with Apply. | `plugin_loader_smoke` (valid/invalid payloads), `plugin_registry_headless` (registry + Apply path), `Gui/Injector.cpp`. |
| 4 | Declarative `ui_revamp` actions | **Closed** | Sidebar/bottom/tools visibility+size persisted via `PluginHost::ApplyShellLayout`; hidden tools closed; tool order applied; session tabs accept `top`; built-in theme selection applied. Unsupported actions (`bottom` tabs, `showClose:false`, unknown shell key/theme, non-default shell) fail with `layout.unsupported`; bad values fail `layout.invalid`. `IPluginShell` remains out of scope (see note). | `plugin_loader_smoke` (parser), `plugin_registry_headless` (sidebar width, hidden window, order, theme selection, unsupported error). |
| 5 | Install flow | **Closed** | `PluginRegistry::InstallFromFile(path, &error)` copies into the user folder and loads; duplicate/wrong-extension/invalid packages return a readable error. **Install Plugin...** button calls `InstallFromDialog()` → native file picker. Manual copy + restart remains documented and working. | `plugin_registry_headless` (install, duplicate refusal), `Gui/PluginLogWindow.cpp`. |
| 6 | Trust hardening | **Closed** | `trusted_at` ISO-8601 timestamp, per-hash `history` (bounded 16), permission sets compared order-insensitively, permission changes re-prompt, native hash changes re-prompt, declarative hash changes append history without prompting. v1 records migrate on read. | `plugin_loader_smoke` (timestamps/history/policy/round-trip), `plugin_registry_headless` (prompt/auto-accept cases). |
| 7 | Package hardening | **Closed** | Case-insensitive collision check, duplicate names, symlink and hard-link/special-mode rejection plus post-extraction `lstat` (`S_ISREG`, `st_nlink == 1`), entry-count/per-entry/total/compression-ratio limits, `package.invalid_zip`/`package.path`, plus the documented `native.*` and `plugin.blocked` codes. | `plugin_loader_smoke` (traversal, case collision, size, ratio). |
| 8 | API version negotiation | **Closed** | Single constant `kPluginHostApiVersion` = 3 in `PluginVersion.h`, exposed as `PLUGIN_API_VERSION`/`kHostApiVersion`; manifest accepts `1..3` (`kHostApiMinVersion..kHostApiVersion`); loader compares `api_version`/`fPluginApiVersion()`; `GetVersion()` returns 3; stale header comment fixed. v1/v2 packages still load (with ABI warning if they cannot prove layout). | `plugin_loader_smoke` (api_version 3 accepted, 4 rejected), `plugin_registry_headless` (ABI handshake on the fixture, `native.api` rejection for a newer API and a wrong signature). |
| 9 | ABI hazard (`UIWindow`) | **Closed** | SDK `UIWindow` now matches `Gui/Ui.hpp` member order, helper (`BringToFront`) and vtable order (`Render`, `RenderCore`, `GotoMemoryAddress`, dtor). Host has a compile-time mirror `static_assert`; plugins export `fPluginAbiSignature()`/`fPluginApiVersion()` via `PLUGIN_EXPORT_ABI()`; mismatch fails with `native.api`. | Build-time `static_assert` in `PluginApi_Impl.cpp`; `plugin_registry_headless` loads the ABI-exporting fixture and rejects a bad API/signature; examples export it. |
| 10 | Universal example dylibs | **Closed** | `tool-clock` and `tab-notes` build with `-arch arm64 -arch x86_64` (override with `ARCHS=`), export the ABI handshake, and the checked-in dylibs are verified with `lipo -info`. | `lipo -info examples/plugins/*/lib/*.dylib` output in the report. |
| 11 | Loader smoke test | **Closed** | `plugin_loader_smoke` (manifest, permissions, traversal, case, limits, trust, payload, layout, unload) and the end-to-end `plugin_registry_headless` are built by `CASIOEMU_PLUGIN_TESTS` and registered with CTest. | `ctest -j1 -R plugin_` in `build-plugin/`. |
| 12 | Theme load order | **Closed** | `PluginRegistry::PumpFrame()` (driven by the existing per-frame `RenderTrustModal()` call) detects `ThemeManager::IsFontRebuildRequested()`, marks the theme stale and re-applies the document after the rebuild; manual `ReapplyActiveTheme()` exists. No ThemeManager edit required. | `plugin_registry_headless` (pending rebuild → no reapply; rebuild done → style/document re-applied). |

## Deliberate non-goals in this pass

| Item | Status | Reason |
| --- | --- | --- |
| `IPluginShell` full shell takeover (spec §6.3) | Not part of the 12 gaps | The preview shell is being replaced by the shell-revamp work; a shell interface without a stable `Shell.cpp` ABI would be a second incompatible contract. Declarative layout actions are implemented and native plugins can still add windows. |
| Drag-and-drop install | Not part of the 12 gaps | Requires SDL event handling in `Gui/Ui.cpp`; the file picker + `InstallFromFile` API cover the flow. |
| Hook unregistration on unload | Documented limitation | `Hooks` has no unregister method; the host keeps the library mapped instead of leaving dangling callbacks. |

## Verification (2026-11-03)

- Configure: `build-plugin/` (Ninja, Release, sentry off, `CASIOEMU_PLUGIN_TESTS=ON`).
- `cmake --build build-plugin --target casioemu_plugin_loader_smoke casioemu_plugin_registry_headless` — succeeded.
- `ctest --test-dir build-plugin -j1 -R plugin_ --output-on-failure` — **100% tests passed, 2/2**
  (`plugin_loader_smoke` ~0.5 s, `plugin_registry_headless` ~0.2 s).
- `cmake --build build-plugin --target CasioEmuMsvc` — **succeeded** (only SDK
  deployment-target linker warnings from bundled SDL/discord-rpc).
- `examples/plugins/*/build.sh` + `lipo -info`:
  `tool_clock.dylib` and `tab_notes.dylib` are `x86_64 arm64`.
- Test scratch: `CASIOEMU_PLUGIN_TEST_ROOT=/Users/kin/Desktop/Wiz/temp/plugin-tests`
  (set by CTest); the tests remove their run directory on success.

## Build/test commands

```console
$ cmake -S . -B build-plugin -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_EXECUTABLE=ON -DCASIOEMU_ENABLE_SENTRY=OFF -DCASIOEMU_PLUGIN_TESTS=ON
$ cmake --build build-plugin --target \
    CasioEmuMsvc casioemu_plugin_loader_smoke casioemu_plugin_registry_headless
$ ctest --test-dir build-plugin -j1 -R plugin_ --output-on-failure
```

`build-plugin/` is private to the plugin work; the shell revamp keeps `build/`.
