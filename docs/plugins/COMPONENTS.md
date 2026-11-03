# Plugin host internals

How the CasioEmuMsvc plugin host is put together, what each component owns, and where
to extend it. Author-facing behavior is in [`../PLUGIN-GUIDE.md`](../PLUGIN-GUIDE.md);
failure modes are in [`TROUBLESHOOTING.md`](TROUBLESHOOTING.md); the promised-vs-
implemented matrix is in [`STATUS.md`](STATUS.md).

Source root: `CasioEmuMsvc/src/Plugin/` plus the manager UI in
`CasioEmuMsvc/src/Gui/PluginLogWindow.*`, the Injection view in
`CasioEmuMsvc/src/Gui/Injector.cpp`, and the frame hook in `CasioEmuMsvc/src/Gui/Ui.cpp`.

---

## 1. File map

| File | Responsibility |
| --- | --- |
| `PluginVersion.h` | Single host API version constant (`kPluginHostApiVersion` / `kPluginHostApiMinVersion`). |
| `PluginManifest.h/.cpp` | Manifest schema/host constants, `Manifest`, parsing, validation helpers (id/semver/path), permission names and descriptions. |
| `PluginPermissions.h/.cpp` | Permission → capability mapping, `PermissionSet`, denial recorder and readable denial messages. |
| `PluginPackage.h/.cpp` | `Package`, SHA-256 hashing, zip extraction with path/case/symlink/hard-link and size/ratio limits, cache management, manifest load from cache. |
| `PluginTrust.h/.cpp` | `TrustStore`, `TrustRecord`, per-hash `history`, `trusted_at`, pure `EvaluateTrust` policy. |
| `PluginRegistry.h/.cpp` | Singleton orchestrator: discovery, dedupe, trust decisions, modal, enable/disable/unload/remove, declarative/native loading, install, logging, theme reapply pump. GUI-free through `PluginHost`. |
| `PluginHost.h/.cpp` | Narrow shell interface (theme, windows, settings, file picker, PluginApi factory) and the process-wide host pointer. |
| `PluginHostGui.cpp` | Default `PluginHost`: ImGui/ThemeManager/Settings/FileDialog/`windows` adapters; registers itself at static init. |
| `PluginHostApi.h/.cpp` | `CreatePluginApi`/`DestroyPluginApi` declarations and `HostPluginApiVersion()`; `HostUiWindowAbiSignature()` is implemented by `PluginHostGui.cpp` (host layout) and by the headless harness (SDK layout). |
| `PluginApi.h` | Plugin-facing C++ interfaces, `fPluginLoad`/`fPluginUnload`, `PLUGIN_EXPORT_ABI`, SDK `UIWindow` matching the host. |
| `PluginApi_Impl.cpp` | `PluginApi_Impl` backend adapters and the permission-scoped `ScopedPluginApi` facade handed to each native plugin. |
| `PluginPayloads.h/.cpp` | Inject document validation and the host payload registry rendered by `Gui/Injector.cpp`. |
| `PluginLayout.h/.cpp` | Declarative `ui_revamp` parser/validator (`LayoutActions`); unsupported actions are hard errors. |
| `PluginResources.h/.cpp` | Per-plugin window/hook tracking used by unload. |
| `PluginNative.h/.cpp` | `fPluginUnload` discovery/invocation (GUI-free). |
| `ThemeTokens.h/.cpp` | `ApplyThemeDocument`: theme JSON → `ImGuiStyle`/`ThemePalette`. |
| `PluginMan.h/.cpp` | `PluginInfo`, `g_loadedPlugins`, `g_PluginLoadLog`, platform-dispatched `LoadPlugins()`. On macOS it calls `PluginRegistry::Initialize()`. |
| `DebuggerTypes.h` | POD structs returned by `IDebugger`. |
| `Gui/PluginLogWindow.*` | The **Plugin Manager**: Install/Open/Reload, Installed/Developer tabs, Unload/Disable/Enable/Remove, denied-capability tooltip. |
| `Gui/Injector.cpp` | Payload view; renders plugin payloads from `PluginPayloads` and applies them through the MMU. |
| `Gui/Ui.cpp` | Creates the manager window and calls `RenderTrustModal()` every frame (also the registry pump). |

Tests:

| Target | Covers |
| --- | --- |
| `plugin_loader_smoke` | Manifest/API version parsing, permission mapping/denials, zip traversal/case/size/ratio, trust history/`trusted_at`, hash-change policy, payload/layout parsing, `fPluginUnload`. |
| `plugin_registry_headless` | End-to-end registry with a fake `PluginHost`: theme/inject/layout/tool fixtures, trust decisions, permission denials, payload registration, layout application, unload/reload, install, theme-token application and font-rebuild reapply. |

Both are built by `CASIOEMU_PLUGIN_TESTS` (default ON) and registered with CTest.

---

## 2. Class responsibilities

### 2.1 `casioemu::plugin::PluginRegistry`

Meyers singleton (`Instance()`), non-copyable. Runtime state:

```cpp
std::vector<PluginEntry> m_entries;
TrustStore m_trust;
bool m_initialized;
int  m_modalIndex;
bool m_openModalRequested;
bool m_modalActive;
std::string m_activeThemeId, m_activeThemeDocument;
bool m_themeStale;
```

Public surface:

| Method | Role |
| --- | --- |
| `Initialize()` | One-shot: load trust, create folders, scan three locations, save trust, register the `std::atexit` unload hook. |
| `Shutdown()` | Calls `fPluginUnload`, hides windows, closes libraries when safe, clears state. Safe twice; also runs at exit. |
| `PumpFrame()` | Called by `RenderTrustModal()`; re-applies the active theme after font rebuilds. |
| `RenderTrustModal()` | Per-frame modal pump. |
| `InstallFromFile(path, &error)` | Copy into the user folder + `AddPackage`; returns a readable error. |
| `InstallFromDialog()` | `PluginHost::PickPluginFile` → `InstallFromFile`. |
| `Unload(id)` | `fPluginUnload`, hide/remove plugin windows, drop payloads, `dlclose` when safe. |
| `ReloadDeclarative()` | Re-reads cached documents for loaded declarative entries (also re-applies layout/payloads). |
| `SetEnabled(id,bool)` | Enable/disable; disabling a loaded native plugin unloads it. |
| `Remove(id)` | Unload first, then delete cache + trust (library retained if hooks exist). |
| `Review`, `Reveal`, `OpenPluginsFolder` | Manager helpers. |
| `ReapplyActiveTheme()` | Manual theme reapply after a font rebuild. |
| `Entries()`, `Log()`, `HostApiVersion()` | Read-only accessors for the UI. |

Private flow: `LoadTrust`, `SaveTrust`, `ScanDirectory`, `AddPackage`,
`ApplyTrustAndLoad`, `LoadEntry`, `LoadNative`, `LoadDeclarative`, `Approve`, `Reject`,
`RequireCapability`, `ApplyDenials`, `HidePluginWindows`, `UnloadEntry`,
`ShutdownInternal`, `ApplyLayout`, `ClearRuntimeState`, `LogLine`.

### 2.2 `PluginEntry` and `Package`

```cpp
struct PluginEntry {
    Package package;
    bool trusted, trustedThisSession, blocked, enabled, loaded, pendingTrust, removed;
    bool limited;                       // a host API call was denied
    std::vector<std::string> deniedCapabilities;
    bool libraryRetained;               // hooks prevented dlclose
    void* handle;                       // dlopen handle
    void* apiContext;                   // ScopedPluginApi
    std::string trustedAt, state, error, document;
};
```

`Package` is the ingestion result (`file`, `root` cache dir, `sha256`, `manifest`,
`valid`, `code`, `message`).

### 2.3 `Manifest`, `PluginPermissions`, `TrustStore`

`Manifest` mirrors `plugin.json`; constants: `kManifestSchema = 1`,
`kHostApiVersion` (= `kPluginHostApiVersion`), `kHostApiMinVersion`, `kHostVersion = "2.2.0"`.

`PermissionSet` resolves declared permission strings to capabilities. The mapping is in
`PluginPermissions.cpp`; `CapabilityPermissions(cap)` returns the permissions an author
can declare to obtain it. `RecordPermissionDenial`/`TakePermissionDenials` feed the
manager's `Loaded (limited)` state.

`TrustStore` reads/writes schema 1 `plugin-trust.json`, migrates v1 records (no
`history`/`trusted_at`) and compares permission sets order-insensitively.
`EvaluateTrust(record, native, sha256, permissions)` is pure and unit-tested.

### 2.4 `PluginApi_Impl` and the facade

`PluginApi* g_pluginapi = new PluginApi_Impl()` holds one adapter per interface. The
registry never hands this object to a plugin. `Host().CreateApi(id, manifest)` creates a
`ScopedPluginApi` that:

- checks `AddWindow`/`GetImGuiContext` against `ui.window`;
- returns capability-filtered interface wrappers (`PlatformHostGuard`, `EmulatorGuard`,
  `MmuGuard`, `ChipsetGuard`, `HooksGuard`, `DebuggerGuard`) or `nullptr`;
- records denials;
- tracks windows and hook registrations for unload.

`PluginApi_Impl::GetVersion()` returns `PLUGIN_API_VERSION` (3). A compile-time mirror
`static_assert` guards the SDK/host `UIWindow` size; the runtime
`fPluginAbiSignature()` handshake guards plugin/host ABI drift.

### 2.5 `PluginLogWindow`

A hidden `UIWindow` named `Plugin Manager`. It calls registry methods and renders
`Entries()`; no loading logic lives here. Install opens the native file picker; Unload
is offered for loaded native plugins; the state tooltip lists denied capabilities.

### 2.6 `ApplyThemeDocument` and the theme pump

Stateless JSON → `ImGuiStyle` mapping. `PluginRegistry` stores the raw document of the
last loaded theme plugin. `PumpFrame()` watches `ThemeManager::IsFontRebuildRequested()`:
when a rebuild has run it calls `PluginHost::ApplyPluginTheme` again, fixing the startup
order problem without touching `ThemeManager`.

---

## 3. Startup and load sequence

1. `ThemeManager::LoadSettings()` — queues a font rebuild when the ImGui context does
   not exist yet.
2. `test_gui()` — creates the ImGui context, windows (including the manager).
3. `LoadPlugins()` → `PluginRegistry::Initialize()`.
4. `Initialize`:
   1. `TrustStore::Load`.
   2. Create `UserPluginsDir()`/`CacheDir()`; register the exit hook.
   3. Scan user plugins, `<exe>/plugins`, `<exe>/../Resources/plugins`.
   4. `TrustStore::Save`.
5. Per package `AddPackage`:
   1. `ExtractPackage`: SHA-256 → cache hit or temp extraction with limits/path checks →
      manifest parse → rename into `<cache>/<sha>`.
   2. Invalid → `Invalid` row with `<code>: <message>`.
   3. Duplicate id → log and drop.
   4. Valid → `ApplyTrustAndLoad`.
6. `ApplyTrustAndLoad` uses `EvaluateTrust`:
   - `Blocked` → `Blocked`, `plugin.blocked` error;
   - `Disabled` → `Disabled`;
   - `Prompt` → `Awaiting approval`;
   - `Trusted` → load; a declarative hash change appends to `history` first.
7. `LoadEntry`:
   - native → capability `native`, ABI handshake, `Host().CreateApi`, `fPluginLoad`;
     failures may fall back to declarative for `ui_revamp`;
   - declarative → per-type capability gate, then theme/inject/layout/validation.
8. Denials are drained with `ApplyDenials`, producing `Loaded (limited)`.
9. `RenderTrustModal()` opens the first pending entry and calls `PumpFrame()`.

### 3.1 Sequence diagram

```
main()
 ├─ ThemeManager::LoadSettings()            font rebuild queued
 ├─ test_gui()                              ImGui context + manager window
 └─ LoadPlugins() → PluginRegistry::Initialize()
     ├─ TrustStore::Load
     ├─ ScanDirectory(user / exe / Resources)
     └─ per *.plugin: AddPackage
         ├─ ExtractPackage (limits, case/symlink/hardlink checks)
         ├─ duplicate id? → drop
         └─ ApplyTrustAndLoad
             ├─ EvaluateTrust → Blocked/Disabled/Prompt/Trusted
             └─ LoadEntry
                 ├─ native: Require(native) → ABI check → CreateApi → fPluginLoad
                 │           └─ ApplyDenials → Loaded (limited)?
                 └─ declarative: Require(type capability)
                     ├─ theme  → ApplyPluginTheme (host)
                     ├─ inject → ParseInjectDocument → RegisterPluginPayloads
                     └─ layout → ParseLayoutDocument → ApplyShellLayout/windows
```

---

## 4. State machine

```
Discovered ──trusted──► Loaded ──denial──► Loaded (limited)
    │                     │
    │                     ├─ disable ─────────► Disabled ──enable──► Loaded
    │                     ├─ unload ──────────► Unloaded ──enable──► Loaded
    │                     ├─ unload (hooks) ──► Unloaded (library retained)
    │                     └─ remove ──────────► Removed / Removed (library retained)
    ├─ pending ─────────► Awaiting approval
    │                        ├─ allow ─► Loaded
    │                        ├─ block ─► Blocked ──review──► Awaiting approval
    │                        └─ later ─► Deferred
    ├─ blocked record ──► Blocked
    ├─ disabled record ─► Disabled
    ├─ parse/load fail ─► Error
    └─ package invalid ─► Invalid
```

`trustedThisSession` is informational (Allow Once); the load decision always comes from
`EvaluateTrust` plus the session flags.

---

## 5. Trust store details

- File: `<SettingsDir>/plugin-trust.json`, schema 1, written temp+rename.
- Keys: `id`, `version`, `sha256`, `trusted_at`, `permissions`, `enabled`, `blocked`,
  `history[]` (`sha256`, `version`, `trusted_at`, `permissions`).
- History is bounded to 16 hashes; blocking clears it.
- Permissions compare as sets; version is stored but not a decision input.
- Blocking is by id, not hash. `plugin.blocked` is the entry error code.
- A malformed file logs `[WARN] Cannot read plugin trust store: ...` and starts empty.

---

## 6. Data and path inventory

| Item | Path |
| --- | --- |
| Settings root | `~/Library/Application Support/CasioEmuMsvc`, or `<exe>/settings` with `portable.flag` |
| User plugins | `<settings>/Plugins` |
| Cache root | `<settings>/plugin-cache` |
| Per-package cache | `<cache>/<sha256>/` |
| Trust store | `<settings>/plugin-trust.json` |
| App plugins | `<exe>/plugins` |
| Resource plugins | `<exe>/../Resources/plugins` |
| Load log | in-memory `g_PluginLoadLog` |
| Payload registry | in-memory `PluginPayloads()` |
| Plugin windows | tracked in `PluginResources`; registered in `Gui/Ui.cpp` `windows` |

---

## 7. Threading and lifetime

- Discovery, extraction, trust, `dlopen`, `fPluginLoad`, unload and layout application
  run on the main thread.
- `RenderTrustModal`/`PumpFrame` and window render run on the UI thread.
- Hook handlers run on the emulation thread; they must not touch ImGui.
- `PluginRegistry` has no mutex; `PluginResources`/payload/denial registries do.
- Unload removes windows before `fPluginUnload`; the host never deletes plugin-owned
  windows.
- If a plugin registered hooks, the library and facade are retained because the hook API
  has no unregister call. This is reported in the state and log.
- `Shutdown()` is registered with `std::atexit`; the GUI may also call it in
  `gui_cleanup()` for deterministic ordering.
- Theme application mutates global ImGui style; the registry tracks the last document
  and reapplies it after font rebuilds.

---

## 8. Extension points

### 8.1 Add a plugin type

1. Add an enumerator/string in `PluginManifest.h/.cpp`.
2. Add cross-field rules in `ParseManifest`.
3. Add a `LoadEntry` branch and a capability mapping in `PluginPermissions.cpp`.
4. Extend `PluginHost` if the type needs new shell services (and implement it in
   `PluginHostGui.cpp` + the fake host in `plugin_registry_headless`).
5. Add fixtures to both test targets.
6. Update the guide/spec/status docs.

### 8.2 Add a permission

1. Append to `kPermissionNames`/`kPermissionDescriptions`.
2. Add its capability grants in `PluginPermissions.cpp::kGrants`.
3. Check the capability at the operation site (`ScopedPluginApi`, loader, registry).
4. Add a test to `plugin_loader_smoke`.
5. Update the permission tables in the guide and status.

### 8.3 Add a native API interface

1. Declare the abstract class in `PluginApi.h` (append virtuals only).
2. Implement an adapter in `PluginApi_Impl`.
3. Add a capability branch + guard in `ScopedPluginApi::QueryInterface`.
4. Bump `kHostApiVersion` only for breaking ABI changes; bump
   `kPluginUiWindowAbiRevision` for UIWindow changes.
5. Document the capability requirement.

### 8.4 Add theme tokens

Extend `ApplyThemeDocument`; unknown keys stay ignored. `typography` is still not
applied to fonts (ThemeManager owns fonts).

### 8.5 Layout/injection

Layout lives in `PluginLayout.cpp` (parse) + `PluginHostGui::ApplyShellLayout`
(settings) + `PluginRegistry::ApplyLayout` (windows/theme). Injection lives in
`PluginPayloads.cpp` (validate/registry) + `Gui/Injector.cpp` (render/apply).

---

## 9. Known structural limits

| Limit | Where |
| --- | --- |
| `IPluginShell` full-shell takeover is not implemented; declarative layout only | `PluginApi.h` |
| Hook callbacks cannot be unregistered, so unload retains the dylib | `PluginResources`, `PluginApi.h` `Hooks` |
| Native permission enforcement covers host API calls, not direct OS calls | security model |
| No sandbox/signing/notarization | security model |
| Layout persists through `SettingsStore`; the shell must consume those fields | `PluginHostGui::ApplyShellLayout` |
| Drag-and-drop install is not wired; use **Install Plugin...** | `Gui/Ui.cpp` (SDL events) |
| No file watcher/hot reload for native code | `PluginRegistry` |
