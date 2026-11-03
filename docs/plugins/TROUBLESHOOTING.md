# Plugin troubleshooting

Failure modes that exist in the CasioEmuMsvc preview plugin host, derived from
`CasioEmuMsvc/src/Plugin/PluginRegistry.cpp`, `PluginPackage.cpp`,
`PluginManifest.cpp`, `ThemeTokens.cpp`, and `CasioEmuMsvc/src/Gui/PluginLogWindow.cpp`.

General workflow:

1. Open **Plugin Manager** from the window menu.
2. Read the **State** column; hover it for the full error string.
3. Open the **Developer** tab for paths, host versions, and the load log
   (**Copy Log**).
4. Compare with the table below.

States used by the host: `Discovered`, `Awaiting approval`, `Deferred`, `Loaded`,
`Loaded (limited)`, `Unloaded`, `Unloaded (library retained: hooks)`, `Disabled`,
`Blocked`, `Error`, `Invalid`, `Removed`, `Restart required (hooks retained)`.

Log prefixes: `[INFO]`, `[WARN]`, `[ERROR]`, `[SUCCESS]`. The log lives in
`g_PluginLoadLog` and is only in memory.

---

## Quick index

| # | Failure mode | Typical code |
| --- | --- | --- |
| 1 | [Invalid zip](#1-invalid-zip) | `package.invalid_zip` |
| 2 | [Missing or invalid manifest](#2-missing-or-invalid-manifest) | `manifest.missing`, `manifest.invalid_json` |
| 3 | [Unsupported type](#3-unsupported-type) | `manifest.type` |
| 4 | [Permission denied](#4-permission-denied) | `plugin.permission`, `Loaded (limited)`, `manifest.permission` |
| 5 | [Hash change](#5-hash-change) | re-prompt, `package.sha256` |
| 6 | [Dylib load failure](#6-dylib-load-failure) | raw `dlerror()` text |
| 7 | [Duplicate id](#7-duplicate-id) | `[WARN] Duplicate plugin id` |
| 8 | [Disabled plugin](#8-disabled-plugin) | state `Disabled` |
| 9 | [Missing entry point](#9-missing-entry-point) | `cannot open ...` |
| 10 | [Unsafe package path](#10-unsafe-package-path) | `package.path` |
| 11 | [Manifest field, id, version, platform, min_host](#11-manifest-field-id-version-platform-min_host) | `manifest.*` |
| 12 | [Theme loads but does not apply](#12-theme-loads-but-does-not-apply) | state `Loaded` |
| 13 | [Declarative layout has no effect](#13-declarative-layout-has-no-effect) | state `Loaded` |
| 14 | [Injection never appears](#14-injection-never-appears) | state `Loaded` |
| 15 | [Native plugin window missing or crash](#15-native-plugin-window-missing-or-crash) | state `Loaded` / crash |
| 16 | [Stale cache or trust store](#16-stale-cache-or-trust-store) | wrong old content |
| 17 | [Install flow](#17-install-flow) | Install Plugin... / `InstallFromFile` |
| 18 | [Unload / library retained](#18-unload--library-retained) | `fPluginUnload`, hooks |

---

## 1. Invalid zip

**Symptom.** The plugin appears in the manager with state `Invalid` and an error like:

```
package.invalid_zip: the file is not a readable zip archive
package.invalid_zip: cannot extract: assets/theme.json
```

Log line (from `PluginRegistry::AddPackage`):

```
[ERROR] MyTheme.plugin: package.invalid_zip: the file is not a readable zip archive
```

**Cause.** `PluginPackage.cpp::ExtractPackage` calls
`mz_zip_reader_init_file`. It fails when the file is not a zip, is truncated, uses an
unsupported compression method, or has an encrypted entry. If the central directory
parses but a member cannot be extracted (for example an encrypted member), the code
reports `cannot extract: <name>`.

**Fix.** Rebuild the package:

```console
$ cd MyTheme
$ zip -r -X ../MyTheme.plugin .
```

If the source contains an encrypted member, remove the encryption; the host does not
prompt for passwords.

**Reported by.** `casioemu::plugin::ExtractPackage` (`PluginPackage.cpp`), surfaced by
`PluginRegistry::AddPackage` (`PluginRegistry.cpp`). Entry state: `Invalid`.
`entry.error = "<code>: <message>"`.

---

## 2. Missing or invalid manifest

**Symptom.** State `Invalid` with one of:

```
manifest.missing: cannot open plugin.json
manifest.invalid_json: [json.exception.parse_error.101] parse error at line ...
manifest.invalid_json: plugin.json must be a JSON object
manifest.schema: field 'schema' must be an integer
manifest.schema: unsupported schema version 2
```

**Cause.**

- `manifest.missing`: there is no `plugin.json` at the archive root. This is almost
  always a packaging mistake (`zip -r X.plugin MyTheme` instead of zipping from inside
  the folder).
- `manifest.invalid_json`: syntax error, a UTF-8 BOM before `{`, or the root value is
  an array/string instead of an object.
- `manifest.schema`: `schema` absent, not a JSON integer (`1.0` is a float and is
  rejected), or not `1`.

**Fix.**

- Repackage from inside the plugin folder.
- Validate the JSON and rewrite `plugin.json` as UTF-8 without BOM.
- Use `"schema": 1` (integer, no quotes, no decimal point).

**Reported by.** `casioemu::plugin::LoadManifestFile`, `ParseManifest`
(`PluginManifest.cpp`); `ExtractPackage` for the missing-file path.

---

## 3. Unsupported type

**Symptom.** State `Invalid`:

```
manifest.type: unknown type 'Theme'
```

**Cause.** `ParsePluginType` compares exact lowercase strings: `theme`, `inject`,
`ui_revamp`, `tab`, `tool`. `"Theme"`, `"ui-revamp"`, `"widget"`, and `"shell"` are all
unknown.

**Fix.** Use one of the five exact names. Remember that `type` also carries
requirements:

- `tab` and `tool` require `native_entry`;
- `theme` and `inject` must not have `native_entry`;
- `ui_revamp` may have either.

A missing `native_entry` on a native type reports:

```
manifest.field: type 'tool' requires native_entry
```

**Reported by.** `ParsePluginType`, `ParseManifest` (`PluginManifest.cpp`).

---

## 4. Permission denied

**Symptom.** One of:

- state `Error` with `plugin.permission: permission denied: '<phase>' requires the
  '<capability>' capability ...`;
- state `Loaded (limited)`, with a denial tooltip listing e.g. `ui.window`;
- the log has `[WARN] <id>: '<API>' requires the '<capability>' capability`;
- `manifest.permission` at parse time for an unknown/duplicate permission.

**Cause.** `PluginPermissions.cpp` maps declared permissions to capabilities, and the
host checks them at loader gates and at every `PluginApi` entry point (see the guide,
section 3.3). A plugin that declares `filesystem.read` but calls `AddWindow` gets the
window denied, not a crash; a theme/inject/layout/native package without its type's
permission fails to load.

**Fix.**

- Add the permission named in the message (`ui.shell`, `emulator.inject`,
  `emulator.read`, `emulator.write`, `emulator.code`, `filesystem.read`,
  `filesystem.write`, `network`, `process`, `clipboard`).
- The permission set changed, so approve the package again (the trust modal re-prompts
  when permissions are added or removed).
- `Loaded (limited)` means the plugin chose to continue without a capability; check the
  tooltip to see which API was denied.
- For `manifest.permission`, fix the spelling; duplicates are also rejected.

**Reported by.** `PluginPermissions.cpp`, `PluginRegistry::RequireCapability` /
`ApplyDenials`, `ScopedPluginApi` in `PluginApi_Impl.cpp`, `PluginManifest.cpp`.

## 5. Hash change

**Symptom.** After replacing a `.plugin` file, the app prompts for trust again (native
plugins and any permission change), or silently keeps working (declarative plugins with
the same permissions).

**Cause.** `PluginTrust.cpp::EvaluateTrust` compares the package hash against the
record's current hash **and** its per-hash `history`, and compares permission lists as
sets:

- native + unknown hash → prompt;
- declarative + unknown hash + same permissions → trusted, history appended;
- any permission added/removed → prompt (reordering does not);
- `blocked: true` → `Blocked` regardless of hash;
- `enabled: false` → `Disabled`.

The `version` string is stored per hash but is not a decision input. `trusted_at` records
when the current hash was approved.

**Fix.** Approve the new hash in the modal. To reset trust, delete the record in
`<SettingsDir>/plugin-trust.json` (or remove/reinstall via the manager). The manager
tooltip/log shows the approval timestamp.

**Reported by.** `PluginTrust.cpp`, `PluginRegistry::ApplyTrustAndLoad`.

## 6. Dylib load failure

**Symptom.** State `Error`, with one of:

```
native.entry: native entry not found: /Users/.../plugin-cache/<sha>/lib/foo.dylib
native.load: dlopen(...): no suitable image found. Did find: ... mach-o, but wrong architecture
native.load: dlopen(...): symbol not found: ...
native.symbol: missing fPluginLoad
native.load: fPluginLoad threw: <what>
native.api: UIWindow ABI mismatch (plugin N, host M); rebuild ...
native.api: plugin targets API v4; host supports v1..3
```

Log lines: `[ERROR] <id>: <error>`; for `ui_revamp` the host then tries the
declarative fallback: `[WARN] Native shell for <id> failed; trying the declarative fallback`.

**Cause.**

- `native entry not found`: `native_entry` does not match the file inside the package,
  or the dylib was not included when zipping.
- `native.load` text: architecture mismatch (for example an `arm64` app loading an
  `x86_64`-only plugin), an unresolved symbol because the plugin was linked against
  something the app does not export, a missing dependent dylib, or a corrupt Mach-O.
- `native.symbol`: `fPluginLoad` is not exported. C++ name mangling without
  `extern "C"` is the usual cause.
- `native.api`: the plugin's `api_version`/`fPluginApiVersion()` is newer than the host,
  or `fPluginAbiSignature()` does not match. Rebuild with the headers in this tree and
  include `PLUGIN_EXPORT_ABI();`.
- `fPluginLoad threw`: an exception escaped the entry point. The host catches
  `std::exception` and `...`, closes the library, and reports it. A segfault is not
  caught and crashes the app.

**Fix.**

- Confirm `native_entry` and rebuild the zip.
- Match architectures. The example build scripts build universal (`arm64` + `x86_64`)
  by default; for a custom plugin:

  ```console
  $ clang++ -std=c++20 -dynamiclib -undefined dynamic_lookup -fPIC \
      -arch arm64 -arch x86_64 \
      -I"$ROOT/CasioEmuMsvc/src" \
      -I"$ROOT/CasioEmuMsvc/src/Plugin" \
      -I"$ROOT/CasioEmuMsvc/src/Gui/imgui" \
      -o "$HERE/lib/myplugin.dylib" "$HERE/src/myplugin.cpp"
  ```

- Export the entry point exactly:

  ```cpp
  extern "C" void fPluginLoad(PluginApi* api) { ... }
  ```

- Check undefined symbols with `nm -u` and `otool -L`.
- Do not throw across the boundary; catch inside `fPluginLoad`.

**Reported by.** `PluginRegistry.cpp::LoadNative` (exists check, `dlopen`, `dlsym`,
try/catch). Entry state: `Error`. The library is `dlclose`d on every failure path.

---

## 7. Duplicate id

**Symptom.** Only one of two packages appears. The log has:

```
[WARN] Duplicate plugin id 'com.example.theme'; ignoring /Users/.../plugins/Other.plugin
```

The duplicate is not added to the manager at all (there is no row to remove).

**Cause.** `PluginRegistry::AddPackage` rejects a package whose manifest `id` matches
an already-added, valid, non-removed entry. Scan order is: user plugins, then
`<exe>/plugins`, then `<exe>/../Resources/plugins`. The first one wins, so a user
package shadows a built-in with the same id.

**Fix.** Give each package a unique reverse-DNS id (or remove the shadowing package).
Remember the id is also the key for the trust record: two packages with the same id
share one trust entry.

**Reported by.** `PluginRegistry.cpp::AddPackage`.

---

## 8. Disabled plugin

**Symptom.** State `Disabled`; a native plugin's window disappears immediately (no
restart).

**Cause.** The trust record has `enabled: false`, or the user clicked **Disable**.
Disabling a loaded native plugin now calls `Unload` (`fPluginUnload`, windows removed,
`dlclose` when safe). Disabling a declarative theme resets the built-in style. Disabling
an untrusted plugin (no trust record) is session-only; it prompts again next launch.

**Fix.** Click **Enable**; a trusted plugin reloads immediately, an untrusted one opens
the trust modal.

**Reported by.** `PluginRegistry::SetEnabled`, `PluginRegistry::Unload`.

## 9. Missing entry point

**Symptom.** State `Error`:

```
cannot open /Users/.../plugin-cache/<sha>/assets/theme.json
```

For native packages the equivalent is section 6 (`native entry not found`). For a
native `ui_revamp` whose native load fails and whose declarative file is also missing,
the second error replaces the first.

**Cause.** `PluginRegistry::LoadDeclarative` resolves
`entry.package.root / manifest.entryPoint` and calls `ReadTextFile`. The manifest
validates that `entry_point` is a safe relative path, but not that the file exists.
Native `tab`/`tool` never read `entry_point`; only `native_entry` matters.

**Fix.** Include the file in the package and match the path exactly (case-sensitive on
the filesystem, forward slashes only). Repackage and restart. **Reload Declarative**
will not retry an entry that is already in `Error`, because it skips `loaded == false`.

**Reported by.** `PluginRegistry.cpp::LoadDeclarative` (`ReadTextFile` helper),
`PluginRegistry::LoadNative` for dylibs.

---

## 10. Unsafe package path

**Symptom.** State `Invalid`:

```
package.path: unsafe zip entry: ../evil
package.path: symlink entries are not allowed: link
package.path: cannot create plugin cache: ...
package.path: cannot commit plugin cache: ...
```

**Cause.** `ExtractPackage` validates every member with `IsSafeRelativePath`
(`PluginManifest.cpp`): non-empty, no leading `/` or `\`, no backslashes, no empty,
`.` or `..` components. Unix symlink members are rejected via `S_ISLNK` on the external
attributes. There is no hard-link check. Directory creation or the final rename can
also fail (permissions, disk full).

**Fix.** Rebuild the zip without absolute paths, parent traversals, backslashes, or
symlinks. On macOS, `zip -r` normally stores symlinks as symlinks; dereference them
first or exclude them. Ensure the cache directory
(`<SettingsDir>/plugin-cache`) is writable.

**Reported by.** `PluginPackage.cpp::ExtractPackage`.

---

## 11. Manifest field, id, version, platform, min_host

**Symptom.** State `Invalid` with a specific code:

| Message (abridged) | Code |
| --- | --- |
| `missing required field 'id'` / `'name'` / `'version'` / `'type'` / `'entry_point'` | `manifest.field` |
| `field 'schema' must be an integer` | `manifest.schema` |
| `id must be reverse-DNS-ish: lower-case letters, digits, '.', '_' or '-', with at least one dot` | `manifest.id` |
| `name must be 1-80 characters` | `manifest.field` |
| `description must be at most 500 characters` | `manifest.field` |
| `version must be semver (MAJOR.MINOR.PATCH)` | `manifest.version` |
| `min_host must be semver` / `plugin requires host X or newer` | `manifest.min_host` |
| `only the 'macos' platform is supported` | `manifest.platform` |
| `unsupported api_version N` | `manifest.field` |
| `entry_point must be a safe relative path` | `manifest.field` |
| `asset path must be a safe relative path` | `manifest.field` |

**Cause / fix.**

- id: lowercase only, at least one dot, no leading/trailing/consecutive dot, 3–128
  bytes.
- name: 1–80 bytes; description: at most 500 bytes.
- version: three numeric components separated by dots, then an optional suffix after
  `-` or `+`. `1.0`, `v1.0.0`, and `1.0.0 beta` are rejected.
- `min_host` is compared against host `2.1.0`. Remove it or lower it.
- `platforms` may only contain `"macos"`; omit the field for the default.
- `api_version` must be `1` or `2`.
- Optional string fields with the wrong JSON type are silently ignored, **except**
  `min_host`, which then fails validation.
- `homepage` is not validated, so a bad URL passes.

**Reported by.** `PluginManifest.cpp::ParseManifest`, `IsValidPluginId`,
`IsValidSemver`, `CompareSemver`.

---

## 12. Theme loads but does not apply

**Symptom.** State `Loaded`, but colors/metrics are unchanged or revert after a moment.
No error.

**Cause.**

- The theme document uses only `tokens.typography` (`uiScale`, `monoFamily`,
  `uiFamily`) or the `error`/`warning`/`success` colors. The host reads none of those.
- Only the ten documented color keys and the metrics keys in the guide are mapped.
- A later theme plugin overwrote it (last loaded wins).
- The user toggled light/dark mode in Settings; `SetLightMode`/`SetDarkMode` clear
  `g_plugin_theme_active` and overwrite the style.
- **Startup font rebuild:** when settings load before the ImGui context exists (the
  normal startup path), `ThemeManager::ProcessFontRebuild` runs after the plugin scan
  and assigns the user's saved style over the plugin style. The plugin stays loaded,
  but the visible theme is the user's.

**Fix.**

- Add mapped colors and metrics (guide section 8).
- Ensure only one theme plugin is enabled.
- Re-apply after startup: **Reload Declarative** re-runs `ApplyThemeDocument` for
  loaded declarative plugins; or toggle the theme plugin off/on.
- If the document itself is bad, the state is `Error`, not `Loaded` (for example
  `theme schema must be 1`, `theme is missing the 'tokens' object`,
  `colors.background is not a valid color`). In that case fix the JSON, then restart:
  Reload Declarative skips errored entries.

**Reported by.** `ThemeTokens.cpp::ApplyThemeDocument`, `PluginRegistry::LoadDeclarative`,
`ThemeManager::ProcessFontRebuild`, `ThemeManager::SetLightMode`/`SetDarkMode`.

---

## 13. Declarative layout has no effect

**Symptom.** `ui_revamp` plugin is `Loaded` but a specific value did not change, or the
state is `Error` with `layout.invalid` / `layout.unsupported`.

**Cause / fix.**

- `layout.unsupported` means the document asks for an action this host cannot honour:
  `sessions.tabs` other than `top`, `sessions.showClose: false`, an unknown `shell.*`
  key, a non-default `shell.type`, or an unknown theme id. Change the document.
- `layout.invalid` means a wrong type or out-of-range number (sidebar 160–600, bottom
  80–800, tools 200–800).
- Unknown names in `tools.hidden` only log a warning; check the window name matches.
- `tools.tabs` reorders existing windows; it does not create tools.
- `sidebar`/`bottom`/`tools` values are persisted through the settings store; the shell
  applies them on its next frame/startup.
- `theme` selects a built-in preset; a declarative theme plugin that loaded earlier keeps
  precedence for the ImGui style and is re-applied after font rebuilds.

**Reported by.** `PluginLayout.cpp`, `PluginRegistry::ApplyLayout`,
`PluginHostGui::ApplyShellLayout`.

## 14. Injection never appears

**Symptom.** `inject` plugin is `Loaded`, but the payload is missing from the Payload
view; or the plugin is `Error` with `inject.invalid` / `plugin.permission`.

**Cause / fix.**

- The loader validates every payload: `id` (lowercase, 1–96 chars), `name` (1–80),
  `address` (hex, optional `0x`, up to 32 bits), `bytes` (even non-zero hex, ≤ 4096
  bytes), `description` (≤ 500), `target` (`any`/`t4x`/`eps`/`legacy`), `safe` (bool).
  A single invalid payload rejects the document; the message names
  `payloads[N].field`.
- `plugin.permission` means the manifest lacks `emulator.inject`.
- Payloads are registered only after a successful trust decision and load.
- Open the **Payload** window and expand **Plugin payloads**; click **Apply** to write
  the bytes (same MMU path as manual injection).

**Reported by.** `PluginPayloads.cpp`, `PluginRegistry::LoadDeclarative`,
`Gui/Injector.cpp`.

## 15. Native plugin window missing or crash

**Symptom.** State `Loaded` but no window; or the app crashes right after approval.

**Cause.**

- `AddWindow` was denied (`Loaded (limited)`, capability tooltip `ui.window`): add
  `ui.shell` to the manifest.
- `AddWindow` was never called, or the plugin set `UIWindow::open = false`. The window
  is reachable from the window menu by name.
- `RegisterPlugin` was never called: the plugin still loads; the manager uses the
  manifest id and `g_loadedPlugins` may stay empty.
- A crash is usually inside `fPluginLoad`, `RenderCore`, or an ABI mismatch. API v3
  makes the SDK `UIWindow` layout/vtable match the host and the loader checks
  `fPluginAbiSignature()`; plugins without the export are assumed legacy and log a
  warning. Rebuild with the headers in this tree.
- Window names `Settings`/`Calculator` are special-cased by the host; avoid names that
  collide with built-in memory windows (`RAM`, `ROM`, `All`, `PRAM`, `Flash`, `T4x ...`).

**Fix.** Declare `ui.shell`, call `RegisterPlugin` then `AddWindow`, add
`PLUGIN_EXPORT_ABI()`, and rebuild universal. Open the window from the menu.

**Reported by.** `ScopedPluginApi::AddWindow`, `LoadNative`, `Gui/Ui.cpp`.

## 16. Stale cache or trust store

**Symptom.** The manager shows old metadata or old theme content after you edited the
package; or a plugin stays blocked/disabled after you changed the file.

**Cause.**

- Extraction is keyed by the package SHA-256. If `<SettingsDir>/plugin-cache/<sha>/plugin.json`
  already exists, `ExtractPackage` reuses it and never re-reads the archive.
- **Reload Declarative** re-reads the cached copy, not the `.plugin` file.
- The trust record is keyed by id; a new file with the same id inherits
  `enabled`/`blocked`.
- A corrupt `plugin-trust.json` is ignored with
  `[WARN] Cannot read plugin trust store: ...`; all plugins then look untrusted and
  prompt again.

**Fix.**

- Quit the app, delete `<SettingsDir>/plugin-cache/`, restart.
- To reset trust, delete or edit `<SettingsDir>/plugin-trust.json` (schema 1,
  `records` array).
- To iterate without repackaging, edit the cached file directly and press
  **Reload Declarative** (only for currently loaded declarative entries).

**Reported by.** `PluginPackage.cpp::ExtractPackage` (cache hit),
`PluginRegistry.cpp::ReloadDeclarative`, `PluginRegistry::LoadTrust`.

---

## 17. Install flow

**Symptom.** The old "Drop a .plugin file onto the window" hint did nothing.

**Cause / fix.** Drag-and-drop is still not wired, but the manager now has an
**Install Plugin...** button. It opens the native file picker and calls
`PluginRegistry::InstallFromDialog()` → `InstallFromFile(path, &error)`:

- copies the package into the user plugin folder (portable: `<exe>/settings/Plugins`);
- refuses a duplicate filename, wrong extension, missing file, or invalid package with
  a readable error (logged in the Developer tab; invalid copied packages are kept for
  inspection);
- loads the package immediately; a new id prompts for trust.

Manual copy + restart remains supported:

```console
$ cp MyTheme.plugin ~/Library/Application\ Support/CasioEmuMsvc/Plugins/
```

**Reported by.** `PluginLogWindow::RenderInstalled`, `PluginRegistry::InstallFromFile`.

---

## 18. Unload / library retained

**Symptom.** After **Unload**/**Disable**/**Remove**, the state is
`Unloaded (library retained: hooks)` or `Removed (library retained: hooks)` and the
`.dylib` stays mapped until exit.

**Cause.** The plugin registered one or more `Hooks`. The current API has no unregister
call, so `dlclose` would leave the emulation thread calling into freed code. The host
runs `fPluginUnload`, hides the windows and tears down the plugin state, but keeps the
library (and the permission facade its callbacks reference) mapped. **Enable** will not
reload it; restart the app.

**Fix.** This is a safety feature. Remove hook registration from the plugin if it must be
unloadable, or restart after testing. Normal plugins unload cleanly and **Enable**
reloads them.

**Reported by.** `PluginRegistry::UnloadEntry`, `PluginResources.cpp`.

## Log-line reference

| Log line | Emitted by |
| --- | --- |
| `[INFO] Plugin host API v3 (2.2.0)` | `PluginRegistry::Initialize` |
| `[INFO] User plugins: <path>` | `PluginRegistry::Initialize` |
| `[INFO] No plugin folder at <path>` | `PluginRegistry::ScanDirectory` |
| `[INFO] Reading <path>` | `PluginRegistry::AddPackage` |
| `[ERROR] <file>: <code>: <message>` | `PluginRegistry::AddPackage` |
| `[WARN] Duplicate plugin id '<id>'; ignoring <path>` | `PluginRegistry::AddPackage` |
| `[WARN] Blocked plugin <id> (hash <12 hex>)` | `ApplyTrustAndLoad` |
| `[INFO] Plugin <id> is waiting for approval` | `ApplyTrustAndLoad` |
| `[INFO] Updated declarative plugin <id> without a new prompt` | `ApplyTrustAndLoad` |
| `[ERROR] <id>: <error>` | `LoadNative` / `LoadDeclarative` |
| `[WARN] Native shell for <id> failed; trying the declarative fallback` | `LoadEntry` |
| `[SUCCESS] Loaded <type> plugin <id>` | `LoadDeclarative` |
| `[SUCCESS] Loaded native plugin <id>` | `LoadNative` |
| `[WARN] <id>: '<API>' requires the '<capability>' capability` | `ScopedPluginApi` |
| `[ERROR] <id>: plugin.permission: ...` | `RequireCapability` |
| `[INFO] Called fPluginUnload for <id>` / `[INFO] Unloaded native library <id>` | `UnloadEntry` |
| `[WARN] <id>: ... library stays mapped ... hooks` | `UnloadEntry` |
| `[INFO] Reapplied theme plugin <id> after font rebuild` | `PumpFrame` |
| `[INFO] Applied declarative layout from <id>` | `ApplyLayout` |
| `[INFO] Accepted new hash for <id> without a new prompt` | `ApplyTrustAndLoad` |
| `[SUCCESS] Installed <file>` | `InstallFromFile` |
| `[WARN] Cannot read plugin trust store: <what>` | `LoadTrust` |
| `[WARN] User blocked plugin <id>` | `Reject` |
| `[INFO] Removed plugin <id>` | `Remove` |
