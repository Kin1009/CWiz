# CasioEmuMsvc plugin author guide

Status: preview host, macOS
Host version: `2.2.0` (`casioemu::plugin::kHostVersion`)
Host API version: `3` (`PLUGIN_API_VERSION`, `PluginApi_Impl::GetVersion()`)
Manifest schema: `1`
Verified against: `CasioEmuMsvc/src/Plugin/`, `CasioEmuMsvc/src/Gui/PluginLogWindow.*`,
`examples/plugins/`, and the CTest targets `plugin_loader_smoke` /
`plugin_registry_headless` in this repository.

This guide is the practical companion to [`PLUGIN_SPEC.md`](PLUGIN_SPEC.md). The spec is
the frozen contract; this guide describes what the preview host in this tree actually
does. Where the two differ, the difference is called out explicitly. Internal structure
is documented in [`plugins/COMPONENTS.md`](plugins/COMPONENTS.md), failure modes in
[`plugins/TROUBLESHOOTING.md`](plugins/TROUBLESHOOTING.md), the shipped examples in
[`plugins/EXAMPLES.md`](plugins/EXAMPLES.md), and the promised-vs-implemented matrix in
[`plugins/STATUS.md`](plugins/STATUS.md).

---

## 1. What a `.plugin` is

A `.plugin` is a zip archive with a `plugin.json` manifest at the archive root. The
extension is matched case-insensitively (`.PLUGIN` works); only top-level files inside
the scan folders are considered.

The host supports five manifest types. Two of them (plus an optional mode of a third)
load native code.

| Type | Kind | Native code | What the preview host actually does |
| --- | --- | --- | --- |
| `theme` | declarative | forbidden | Parses the theme document and assigns an ImGui style immediately. Requires `ui.shell`. Re-applied automatically after the startup font rebuild. |
| `inject` | declarative | forbidden | Parses and validates every payload (`id`, `name`, `address`, `bytes`, `description`, `target`, `safe`) and registers them with the Payload/Injection view. Requires `emulator.inject`. Invalid payload = load error. |
| `ui_revamp` | declarative or native | optional | Declarative documents apply the documented layout actions (sidebar/bottom/tool visibility and size, tool order, hidden tools, session tab placement, built-in theme selection). Actions the host cannot honour fail with `layout.unsupported`; it never reports `Loaded` while doing nothing. A native variant gets the normal `PluginApi` and can add windows; `IPluginShell` from the spec is still not present. |
| `tab` | native | required | `dlopen`s the dylib, calls `fPluginLoad`, and the plugin adds `UIWindow`s. There is no separate tab framework: `tab` and `tool` load identically. |
| `tool` | native | required | Same as `tab`; the type is metadata for the manager UI. |

Practical consequences:

- A "tab" plugin is a normal ImGui window that participates in the window/docking
  system. It is not inserted into a session tab bar.
- Only native plugins receive a `PluginApi*`. Declarative plugins never get a C++ API.
- Native plugins run in-process. The host now checks declared permissions at every
  host API entry point, but native code can still call the OS directly (section 11).
- Native plugins can be unloaded while the app runs (**Unload** in the manager,
  `PluginRegistry::Unload`), and disabling/removing a loaded plugin now actually stops
  it. See section 5.4.

## 2. Package layout and zip rules

### 2.1 Layout

```
Example.plugin                (zip archive)
  plugin.json                 required, at the archive root
  assets/                     optional; where theme/layout/payload/info documents live
  lib/                        optional; native dylibs
  docs/                       optional; ignored by the loader
  src/                        optional; ignored by the loader
  build.sh                    optional; ignored by the loader
```

The loader only requires `plugin.json`. It ignores every file it does not need, so
source, build scripts, and README files are harmless inside the package.

`plugin.json` itself is read as bytes and parsed as JSON. It must be UTF-8; a BOM is
not stripped, so a BOM makes the parse fail with `manifest.invalid_json`.

### 2.2 Extraction rules (`PluginPackage.cpp`, `ExtractPackage`)

1. The whole `.plugin` file is hashed with SHA-256 (`ComputeFileSha256`,
   CommonCrypto). The lowercase hex digest is the cache key.
2. The cache directory is `<settings>/plugin-cache/<sha256>`.
3. If `<cache>/plugin.json` already exists, extraction is skipped and the cached
   manifest is loaded. The archive is **not** re-read on later launches.
4. Otherwise the archive is extracted to `<cache>.tmp-<pid>` and committed with a
   rename. The temporary directory is removed on any failure.
5. Every entry name must pass `IsSafeRelativePath`:
   - non-empty, no leading `/` or `\`;
   - no `\` anywhere (Windows separators are refused outright, not normalized);
   - no `.` or `..` component, no empty component;
   - a single trailing `/` is tolerated for directory entries.
6. Duplicate entry names and **case-colliding** names (`assets/A.txt` vs `assets/a.txt`)
   are rejected with `package.path`. This matters on default case-insensitive APFS
   volumes.
7. Unix symlink entries (`external_attr >> 16` with `S_ISLNK`) are rejected with
   `package.path`. Zip has no portable hard-link marker; entries whose Unix mode is a
   special file (device/FIFO/socket) are rejected as hard-link/special entries, and
   each extracted inode is checked with `lstat` for `S_ISREG` and `st_nlink == 1`.
8. Limits (defaults in `ExtractionLimits`): at most 4096 entries, 64 MiB per entry,
   256 MiB total, and a 200:1 compression ratio for entries above 1 MiB. Violations
   fail with `package.invalid_zip` before the cache is committed.
9. Encrypted or unsupported compression entries fail with `package.invalid_zip`.
10. A package that fails zip parsing, path validation, limits, or manifest parsing is
    never committed to the cache.

Treat `.plugin` files as untrusted input. The limits above are a defence against
zip bombs and accidental disk exhaustion, not a sandbox.

### 2.3 Packaging commands

From inside the plugin folder, so `plugin.json` lands at the archive root:

```console
$ cd MyTheme
$ zip -r -X ../MyTheme.plugin .
```

Notes:

- Do **not** zip the parent folder (`zip -r MyTheme.plugin MyTheme`); that puts
  `plugin.json` one level down and the package fails with `manifest.missing`.
- `-X` drops extra file attributes; it is optional but keeps the archive tidy.
- macOS may add `.DS_Store` entries. They are safe and ignored, but you can delete
  them before zipping.
- `__MACOSX/` entries only appear when zipping a folder from its parent; they are
  unaffected by the loader but unnecessary.

---

## 3. `plugin.json` reference

Parsing lives in `ParseManifest` / `LoadManifestFile` (`PluginManifest.cpp`). Every
rejection has a stable code and a readable message (section 3.5).

### 3.1 Required fields

| Field | JSON type | Rules in the preview host |
| --- | --- | --- |
| `schema` | integer | Must be exactly `1`. A float (`1.0`) is rejected with `manifest.schema`. |
| `id` | string | 3–128 **bytes**; lowercase `a-z`, digits, `_`, `-`, and `.`; at least one dot; no leading, trailing, or consecutive dot. Uppercase is rejected. |
| `name` | string | 1–80 **bytes** (UTF-8 bytes, not code points). |
| `version` | string | Must start with three dot-separated numeric components; an optional suffix may follow `-` or `+` and may contain alphanumerics, `.`, `-`, `+`. |
| `type` | string | `theme`, `inject`, `ui_revamp`, `tab`, or `tool`. Case-sensitive. |
| `entry_point` | string | Safe relative path (same rules as zip entries). Required even for native types. |

`entry_point` semantics:

| Type | `entry_point` should point to | Loader behavior |
| --- | --- | --- |
| `theme` | theme JSON | Read, validated, applied. Missing file → `Error`. |
| `inject` | payload JSON | Read, every payload validated, registered with the Payload view. Missing/invalid → `Error`. |
| `ui_revamp` (declarative) | layout JSON | Read, parsed, actions applied, or `Error` with `layout.unsupported`. |
| `ui_revamp` (native) | documentation/icon JSON | Not read by the native path. Native failure falls back to the declarative path. |
| `tab` | documentation/icon JSON | Not read. Only `native_entry` is used. |
| `tool` | documentation/icon JSON | Not read. Only `native_entry` is used. |

The host does not check that `entry_point` exists at install time. For declarative
plugins the error appears at load time as `Error` with `cannot open <path>`.

### 3.2 Optional fields

| Field | JSON type | Default | Notes |
| --- | --- | --- | --- |
| `author` | string | empty | Display only. Wrong type is a `manifest.field` error. |
| `description` | string | empty | At most 500 bytes; shown in the trust modal. |
| `homepage` | string | empty | Display only. The host does not validate it as a URL. |
| `min_host` | string | empty | Semver. If present and invalid → `manifest.min_host`. Host refuses if `CompareSemver(host, min_host) < 0`. |
| `platforms` | string[] | `["macos"]` | Every element must be `"macos"`; an empty array becomes `["macos"]`. |
| `permissions` | string[] | `[]` | Known names only, duplicates rejected. Enforced at runtime; see section 3.3. |
| `assets` | string[] | `[]` | Each must be a safe relative path. The host does not use this list to gate access. |
| `native_entry` | string | empty | Safe relative path to the dylib. Required for `tab`/`tool`; forbidden for `theme`/`inject`; optional for `ui_revamp`. |
| `api_version` | integer | `1` | Accepted range `1..3` (`kHostApiMinVersion..kHostApiVersion`). The loader compares it with `fPluginApiVersion()` when the dylib exports it. |

Optional string fields are type-checked: a wrong-typed optional string (for example
`"author": 42`) is now a `manifest.field` error rather than being silently ignored.
`homepage` is still not validated as a URL.

### 3.3 Permissions

The known set (`kPermissionNames` in `PluginManifest.cpp`):

| Permission | Grants capability | Declared meaning (trust modal text) |
| --- | --- | --- |
| `emulator.read` | `emulator.read` | Observe the model, ROM, memory, registers, and debugger state. |
| `emulator.write` | `emulator.write` | Write RAM, registers, and memory monitors, pause/resume, drive input. |
| `emulator.code` | `emulator.code` + `emulator.write` | Patch ROM or code bytes and hot-reload images. |
| `emulator.inject` | `inject` | Register and run injection payloads. |
| `filesystem.read` | `filesystem.read` | Read files outside the plugin package (file dialogs, snapshot import). |
| `filesystem.write` | `filesystem.write` | Write files outside the plugin package (save dialogs, snapshot export). |
| `network` | `network` | Make outbound network requests. |
| `process` | `process` + `native` | Spawn processes/shells and load native code without UI. |
| `ui.shell` | `ui.shell` + `ui.window` + `theme` + `native` | Add or modify tabs, tools, layout, themes, or the shell; add windows. |
| `clipboard` | `clipboard` | Read or write the clipboard. |

**Permissions are enforced.** `PluginPermissions.cpp` maps permissions to capabilities
and the host checks them at:

- loader gates: `theme` needs `theme`, `inject` needs `inject`, declarative `ui_revamp`
  needs `ui.shell`, native code needs `native`;
- every `PluginApi` entry point: `AddWindow`/`GetImGuiContext` need `ui.window`,
  `QueryInterface` returns `nullptr` for interfaces whose capability is missing,
  `IPlatformHost` file dialogs need `filesystem.*`, `ShowMessageBox` needs
  `ui.window`, the debugger/hooks/emulator interfaces need `emulator.read`, mutating
  methods check `emulator.write`/`emulator.code` individually, `IKeyboard` needs
  `emulator.write`;
- `ui_revamp` layout and theme selection need `ui.shell`.

A denied loader gate fails the load with code `plugin.permission`. A denied API call
returns a null/empty/default value, logs `[WARN] <id>: 'API' requires the '<cap>'
capability`, marks the entry **limited** and sets its manager state to
`Loaded (limited)`; hover the state cell for the denied capability list. Nothing is
silently executed.

Unknown permissions and duplicates are hard validation errors (`manifest.permission`).

### 3.4 Cross-field validation

| Condition | Result |
| --- | --- |
| `type` is `tab` or `tool` and `native_entry` is empty | `manifest.field`: `type '<x>' requires native_entry` |
| `type` is `theme` and `native_entry` is set | `manifest.field`: `theme plugins cannot include native code` |
| `type` is `inject` and `native_entry` is set | `manifest.field`: `inject plugins cannot include native code` |
| `api_version` outside `1..2` | `manifest.field`: `unsupported api_version N` |
| `permissions` contains an unknown name | `manifest.permission` |
| `permissions` contains a duplicate | `manifest.permission` |
| `platforms` contains anything other than `macos` | `manifest.platform` |

### 3.5 Error codes emitted by the preview host

| Code | Produced by | Meaning |
| --- | --- | --- |
| `manifest.missing` | `LoadManifestFile` | `plugin.json` cannot be opened (normally: not at the archive root). |
| `manifest.invalid_json` | `ParseManifest` | JSON parse error, or the root is not an object. |
| `manifest.schema` | `ParseManifest` | `schema` missing, not an integer, or not `1`. |
| `manifest.field` | `ParseManifest` | Missing/invalid required or optional field, bad length, unsafe path, bad `api_version`, cross-field rule. |
| `manifest.id` | `ParseManifest` | `IsValidPluginId` failed. |
| `manifest.version` | `ParseManifest` | `IsValidSemver` failed. |
| `manifest.type` | `ParseManifest` | Unknown `type`. |
| `manifest.permission` | `ParseManifest` | Unknown or duplicate permission. |
| `manifest.platform` | `ParseManifest` | Non-array, non-string, or non-`macos` platform. |
| `manifest.min_host` | `ParseManifest` | Invalid semver or host too old. |
| `package.sha256` | `ExtractPackage` | The package file could not be read for hashing. |
| `package.invalid_zip` | `ExtractPackage` | Not a readable zip, encrypted/unsupported entry, extraction failure, or a size/ratio/entry-count limit. |
| `package.path` | `ExtractPackage` | Unsafe entry, symlink/hard-link/special file, duplicate or case-colliding entry, or cache directory failure. |
| `native.entry` | `LoadNative` | `native_entry` is missing from the cache. |
| `native.load` | `LoadNative` | `dlopen` failed, or `fPluginLoad` threw. |
| `native.symbol` | `LoadNative` | `fPluginLoad` is not exported. |
| `native.api` | `LoadNative` | `api_version` or `fPluginAbiSignature()` mismatch. |
| `plugin.permission` | loaders / API gates | Missing capability for the requested operation. |
| `plugin.blocked` | `ApplyTrustAndLoad` / `Reject` | A trust record blocks this id. |
| `inject.invalid` | `ParseInjectDocument` | Payload document or one payload field is invalid. |
| `layout.invalid` | `ParseLayoutDocument` | Layout document malformed or an out-of-range value. |
| `layout.unsupported` | `ParseLayoutDocument` / `ApplyLayout` | A documented action this host cannot honour (e.g. bottom session tabs, unknown shell action, unknown theme). |

`entry.error` is `"<code>: <message>"`, so the state tooltip and log always start with a
stable code.

## 4. Install and discovery

### 4.1 Paths

`PluginRegistry::SettingsDir()` (`PluginRegistry.cpp`):

```
if <executable dir>/portable.flag exists:   <executable dir>/settings
else:                                       ~/Library/Application Support/CasioEmuMsvc
```

Derived paths:

| Purpose | Path |
| --- | --- |
| User plugins | `<SettingsDir>/Plugins` |
| Package cache | `<SettingsDir>/plugin-cache/<sha256>/` |
| Trust store | `<SettingsDir>/plugin-trust.json` |
| App-local plugins | `<executable dir>/plugins` |
| Resource plugins | `<executable dir>/../Resources/plugins` |

For a normal `.app` bundle, `<executable dir>` is `Contents/MacOS`, so the resource
scan is `Contents/Resources/plugins`. For the CMake build in this repository the
executable lives in `build/`, so the scans are `build/plugins` and `../Resources/plugins`
relative to `build/`.

### 4.2 Scan order and precedence

`PluginRegistry::Initialize` scans, in order:

1. `<SettingsDir>/Plugins`
2. `<executable dir>/plugins`
3. `<executable dir>/../Resources/plugins`

Each scan iterates only the top level of the folder. Files whose extension is
`.plugin` (case-insensitive) are added. Subdirectories are ignored. A `.plugin` that is
actually a directory is skipped silently.

If two packages have the same manifest `id`, the **first one scanned wins** (so the
user folder wins). The later package is not added to the table at all; the log gets:

```
[WARN] Duplicate plugin id '<id>'; ignoring <path>
```

A reserved workspace `.plugins` folder is **not** read by this host.

### 4.3 Installing

The supported workflows are:

1. **Install Plugin...** in the Plugin Manager (`PluginLogWindow`). It calls
   `PluginRegistry::InstallFromDialog()` → the native open panel →
   `InstallFromFile(path, &error)`. The file is copied into the user plugin folder and
   loaded immediately (new packages prompt for trust). A refused install leaves a
   readable error in the log and in the dialog result.
2. **API**: `bool PluginRegistry::InstallFromFile(path, std::string* error)`.
   Wrong extension, missing file, duplicate filename and invalid package are reported
   as `false` with a message; the copied file is kept for inspection when the package
   itself is invalid.
3. **Manual copy + restart** (always works):

   ```console
   $ cp MyTheme.plugin ~/Library/Application\ Support/CasioEmuMsvc/Plugins/
   ```

   then restart the app and approve the package.

SDL drag-and-drop is still not wired up; the manager no longer claims it is.

To remove a plugin, use **Remove** in the Plugin Manager (this unloads native code,
deletes the cache and the trust record, and never touches the original `.plugin`) or
delete the `.plugin` from the folder and restart. To stop a loaded native plugin
without removing it, use **Disable** or **Unload** (section 5.4).

## 5. Trust model

### 5.1 Trust store

Path: `<SettingsDir>/plugin-trust.json`. Written by `TrustStore::Save`, read by
`TrustStore::Load`. Format (schema 1, extra keys are optional and additive):

```json
{
  "schema": 1,
  "records": [
    {
      "id": "com.example.theme",
      "version": "1.0.0",
      "sha256": "<current 64 hex>",
      "trusted_at": "2026-11-02T12:34:56Z",
      "permissions": ["ui.shell"],
      "enabled": true,
      "blocked": false,
      "history": [
        {
          "sha256": "<previous 64 hex>",
          "version": "0.9.0",
          "trusted_at": "2026-10-01T08:00:00Z",
          "permissions": ["ui.shell"]
        }
      ]
    }
  ]
}
```

Facts that matter:

- Records are keyed by `id`. One record per id; `history` keeps up to 16 approved
  hashes with their version, permissions and approval timestamp.
- `trusted_at` is an ISO-8601 UTC timestamp written on approval (Always Allow or
  Enable). Older records without it are migrated with an empty timestamp and a
  one-entry history derived from the top-level hash.
- Permission lists are compared **as sets**: reordering permissions or repeating one
  no longer invalidates trust. Adding or removing a permission does.
- Version is stored (top-level and per hash) but is not used for decisions; the hash is.
- The file is plain JSON in the user's home directory and is trusted on read. A user
  (or anything running as that user) can pre-approve a hash by writing a record.
- `Save` writes `<file>.tmp` and renames it over the real file.

### 5.2 Load policy (`ApplyTrustAndLoad`)

For each valid package, `EvaluateTrust` (`PluginTrust.cpp`) returns:

| Situation | Decision |
| --- | --- |
| No record | `NoRecord` → prompt |
| Record `blocked: true` | `Blocked` (hash ignored) |
| Record `enabled: false` | `Disabled` |
| Permission set changed | `Prompt` |
| Native package whose hash is not in the record/history | `Prompt` |
| Declarative package with the same permissions, new hash | `Trusted` (record + history updated without prompting) |
| Hash already approved | `Trusted` |

Consequences:

- First install always prompts, even with no permissions.
- Updating a theme/inject/layout package with the same permissions reloads without a
  prompt and appends the new hash to `history`.
- Updating a native dylib always prompts (native code is never silently replaced).
- Adding or removing a permission prompts again in both directions.
- A record with an empty or foreign `sha256` counts as an unknown hash.

### 5.3 The trust modal

`PluginRegistry::RenderTrustModal` renders a real ImGui modal (`Plugin Trust`) centered
on the viewport. It shows: name, version, id, type, API version, author, description,
the full SHA-256, the previous approval timestamp / hash count, and every requested
permission with the one-line description from `PermissionDescription`. If the package
has a `native_entry` it adds a warning that native code runs with the app's access and
that permission enforcement only covers calls made through `PluginApi`, not direct OS
calls.

Buttons:

| Button | Effect |
| --- | --- |
| **Always Allow** | Writes/updates the trust record (hash, version, permissions, `trusted_at`, history), loads now. |
| **Allow Once** | Loads for this session only; no record written. |
| **Block** | Records `blocked: true`, `enabled: false`, state `Blocked`; no load. |
| **Later** | Clears `pendingTrust`, state `Deferred`; no record. |

Closing the popup with Escape clears the modal flag but leaves the plugin pending, so it
re-opens on the next frame.

### 5.4 Disable, unload, remove, review

- **Unload** (`PluginRegistry::Unload(id)`, manager button for loaded native plugins):
  1. calls `fPluginUnload(api)` if exported;
  2. hides and removes every window the plugin added through `AddWindow` (host windows
     are never touched);
  3. removes its injection payloads and runtime state;
  4. `dlclose`s the library **only when safe**. If the plugin registered `Hooks`, the
     callback cannot be unregistered by the current API, so the library stays mapped
     and the state becomes `Unloaded (library retained: hooks)` instead of leaving
     dangling function pointers.
  The entry stays installed and **Enable** reloads it.
- **Disable** on a loaded native plugin now calls `Unload` and reports `Disabled`
  (no restart needed). Declarative plugins drop their state; a disabled theme resets
  to the built-in style.
- **Remove**: unloads native code first, then deletes the cache and trust record. The
  original `.plugin` file is untouched. A plugin whose library had to be retained is
  marked `Removed (library retained: hooks)` until exit.
- `PluginRegistry::Shutdown()` unloads everything and clears runtime state. It is
  registered with `std::atexit` during `Initialize()`, so libraries are unloaded and
  `fPluginUnload` runs at process exit even without a GUI call site. If you want
  deterministic ordering before ImGui is destroyed, add this line to `gui_cleanup()`
  in `Gui/Ui.cpp` (it already includes `Plugin/PluginRegistry.h`; safe to call twice):

  ```cpp
  casioemu::plugin::PluginRegistry::Instance().Shutdown();
  ```
- **Review** re-queues a blocked/pending entry for the modal.

### 5.5 Reload

- There is no hot reload and no file watching.
- **Reload Declarative** re-reads the cached copy, not the `.plugin` file. It reloads
  entries that are declarative, not removed, and currently `loaded`; it also re-runs
  layout actions and payload registration.
- Native plugins are unloaded/reloaded through **Unload** + **Enable** (or
  `PluginRegistry::Unload` + `SetEnabled`).
- Restarting the app is always safe. Deleting the cache folder is safe; it is rebuilt
  from the `.plugin` on the next scan.

Developer iteration tip: because declarative reload reads the cache, you can edit
`<SettingsDir>/plugin-cache/<sha256>/assets/theme.json` and press **Reload Declarative**
without repackaging. The package hash no longer matches the edited cache, but the host
does not re-verify cached contents.

## 6. Step-by-step tutorials

### 6.1 Create a theme plugin from scratch

```
MyTheme/
  plugin.json
  assets/theme.json
```

`plugin.json`:

```json
{
  "schema": 1,
  "id": "com.example.mytheme",
  "name": "My Theme",
  "version": "1.0.0",
  "author": "You",
  "description": "A short description for the plugin manager.",
  "min_host": "2.1.0",
  "platforms": ["macos"],
  "type": "theme",
  "permissions": ["ui.shell"],
  "entry_point": "assets/theme.json",
  "assets": ["assets/theme.json"]
}
```

`assets/theme.json`:

```json
{
  "schema": 1,
  "name": "My Theme",
  "variant": "dark",
  "tokens": {
    "colors": {
      "background": "#101418",
      "surface": "#171c21",
      "surfaceAlt": "#1f252b",
      "foreground": "#e6e6e6",
      "muted": "#9aa4ad",
      "border": "#2b333b",
      "accent": "#4f9cf9",
      "accentHover": "#6cb0ff",
      "selection": "#254a73",
      "hover": "#242c34"
    },
    "metrics": {
      "windowRounding": 6,
      "frameRounding": 4,
      "borderSize": 1,
      "spacing": 8,
      "padding": 8,
      "scrollbarSize": 10
    }
  }
}
```

Package and install:

```console
$ cd MyTheme
$ zip -r -X ../MyTheme.plugin .
$ cp ../MyTheme.plugin ~/Library/Application\ Support/CasioEmuMsvc/Plugins/
```

Restart the app. The trust modal appears; choose **Always Allow**. The theme is applied
during the startup scan, before the first frame.

Checklist:

- `entry_point` points at the JSON inside the package, with forward slashes.
- `schema` is the integer `1` in both files.
- Colors are `#RRGGBB` or `#RRGGBBAA`.
- No `native_entry` on a theme.
- If nothing changes visually, check section 8 (token mapping) and section 10.4
  (the startup font rebuild is handled by the registry).

### 6.2 Create a declarative layout plugin

```
Compact/
  plugin.json
  assets/layout.json
```

```json
{
  "schema": 1,
  "id": "com.example.compact",
  "name": "Compact Layout",
  "version": "1.0.0",
  "min_host": "2.1.0",
  "type": "ui_revamp",
  "permissions": ["ui.shell"],
  "entry_point": "assets/layout.json"
}
```

`assets/layout.json`:

```json
{
  "schema": 1,
  "shell": {
    "type": "default",
    "sidebar": { "visible": true, "width": 220 },
    "bottom": { "visible": true, "height": 140, "active": "output" },
    "tools": {
      "visible": false,
      "width": 320,
      "tabs": ["Variables", "Hardware", "Labels"],
      "hidden": ["Payload"]
    },
    "sessions": { "tabs": "top", "showClose": true },
    "theme": "vscode-dark"
  }
}
```

Applied actions (all persisted through the host settings store and the live window
list; requires `ui.shell`):

| Action | Behavior |
| --- | --- |
| `sidebar.visible` / `sidebar.width` | Stored in the shell settings (width clamped 160–600). |
| `bottom.visible` / `height` / `active` | Stored in shell settings (`height` 80–800; `active` is `output`, `problems`, `terminal`). |
| `tools.visible` / `tools.width` | Stored in shell settings (`width` 200–800). |
| `tools.hidden` | Named windows are closed. Unknown names log a warning but are harmless. |
| `tools.tabs` | Matching windows are reordered in the host window list. |
| `sessions.tabs` | Only `"top"` is supported. |
| `sessions.showClose` | Only `true` is supported. |
| `theme` (root or `shell.theme`) | Selects a built-in preset by id. A declarative theme plugin that loaded first keeps precedence and is re-applied after font rebuilds. |

Anything else fails the load with `layout.unsupported` (for example `sessions.tabs:
"bottom"`, an unknown `shell.*` key, or an unknown theme). Out-of-range numbers and
wrong JSON types fail with `layout.invalid`. A layout never reports `Loaded` while
doing nothing.

`IPluginShell` (full shell takeover, spec section 6.3) is still not present in this
tree; a native `ui_revamp` plugin can add windows through the normal `PluginApi`.

Packaging and installation are the same as for a theme:

```console
$ cd Compact
$ zip -r -X ../Compact.plugin .
```

Then use **Install Plugin...** or copy the package into the plugins folder and restart.

### 6.3 Build and load a native tab or tool dylib

Both examples use the same recipe. The real `examples/plugins/tab-notes/build.sh`:

```bash
#!/bin/bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
mkdir -p "$HERE/lib"

# Universal binary: the app ships arm64 + x86_64 slices, so the plugin must too.
ARCHS="${ARCHS:--arch arm64 -arch x86_64}"

# shellcheck disable=SC2086
clang++ -std=c++20 -dynamiclib -undefined dynamic_lookup -fPIC $ARCHS \
    -I"$ROOT/CasioEmuMsvc/src" \
    -I"$ROOT/CasioEmuMsvc/src/Plugin" \
    -I"$ROOT/CasioEmuMsvc/src/Gui/imgui" \
    -o "$HERE/lib/tab_notes.dylib" \
    "$HERE/src/tab_notes.cpp"

echo "Built $HERE/lib/tab_notes.dylib"
lipo -info "$HERE/lib/tab_notes.dylib"
```

`examples/plugins/tool-clock/build.sh` is identical except for the names.

Flag notes:

- `-std=c++20` — the host and examples are C++20; the STL layout check
  (`AssertFundamentalSTL`) fails on ABI mismatch.
- `-dynamiclib -undefined dynamic_lookup` — do not link against the app. Undefined
  symbols (ImGui, plugin API callbacks) are resolved from the running app at `dlopen`
  time. If the host does not export a symbol, loading fails with a `native.load` error.
- `-fPIC` — required for a loaded image.
- `-arch arm64 -arch x86_64` — build a universal dylib. The checked-in examples are
  universal and verified with `lipo -info`.
- Include paths match the tree layout.

Minimal source (`examples/plugins/tool-clock/src/tool_clock.cpp`):

```cpp
#include <PluginApi.h>

#include <ctime>
#include <cstdio>

class ClockWindow : public UIWindow {
public:
    ClockWindow() : UIWindow("Clock") {
        inital_size = ImVec2(320, 180);
    }

    void RenderCore() override {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        char text[32]{};
        std::strftime(text, sizeof(text), "%H:%M:%S", &local);
        ImGui::TextUnformatted("Local time");
        ImGui::Separator();
        ImGui::Text("%s", text);
        ImGui::TextDisabled("Example native plugin.");
    }
};

extern "C" void fPluginLoad(PluginApi* api) {
    api->RegisterPlugin("com.casioemu.tool.clock", "Clock Tool", "1.0.0",
                        "CasioEmuMsvc", "Example native tool");
    api->AddWindow(new ClockWindow());
}

PLUGIN_EXPORT_ABI();
```

Manifest for a native tool:

```json
{
  "schema": 1,
  "id": "com.casioemu.tool.clock",
  "name": "Clock Tool",
  "version": "1.0.0",
  "author": "CasioEmuMsvc",
  "description": "Example native tool plugin that adds a clock window.",
  "min_host": "2.1.0",
  "platforms": ["macos"],
  "type": "tool",
  "permissions": ["ui.shell"],
  "api_version": 3,
  "entry_point": "assets/info.json",
  "native_entry": "lib/tool_clock.dylib",
  "assets": ["assets/info.json"]
}
```

Build, package, install:

```console
$ cd examples/plugins/tool-clock
$ ./build.sh
$ zip -r -X ../ToolClock.plugin .
$ # then use Install Plugin... in the Plugin Manager, or:
$ cp ../ToolClock.plugin ~/Library/Application\ Support/CasioEmuMsvc/Plugins/
```

Runtime rules the host enforces for native packages:

- `native_entry` must point to an existing file in the extracted cache. Missing:
  `native.entry`.
- The dylib must export `extern "C" void fPluginLoad(PluginApi*)`, otherwise
  `native.symbol`.
- `PLUGIN_EXPORT_ABI()` exports `fPluginApiVersion()` and `fPluginAbiSignature()`; the
  loader compares both and fails with `native.api` on mismatch. Plugins built before
  API v3 may omit them (a warning is logged and the legacy UIWindow layout is assumed).
- `api_version` defaults to `1`. Values outside `1..3` are rejected at parse time.
- `fPluginUnload(PluginApi*)` is optional; when exported it runs on **Unload**,
  **Disable**, **Remove** and at process exit, before the library is closed.
- Exceptions escaping `fPluginLoad`/`fPluginUnload` are caught and reported. Crashes are
  not caught.
- `RegisterPlugin` should be called before adding windows. The host records the call and
  uses the manifest id, not the id passed to `RegisterPlugin`, for trust and state.
- The window appears in the app's window list under its `UIWindow` name and can be
  docked, hidden, and reopened like built-in tools.

## 7. The five example plugins

Full manifests are shown so you can copy them. Each folder also has an `assets/`
document; there are no `README.md` files in the example folders in this tree.

### 7.1 `theme-vscode-dark` (`theme`)

```json
{
  "schema": 1,
  "id": "com.casioemu.theme.vscode-dark",
  "name": "VS Code Dark",
  "version": "1.0.0",
  "author": "CasioEmuMsvc",
  "description": "A dark palette inspired by the MIT-licensed VS Code default theme.",
  "min_host": "2.1.0",
  "platforms": ["macos"],
  "type": "theme",
  "permissions": ["ui.shell"],
  "entry_point": "assets/theme.json",
  "assets": ["assets/theme.json"]
}
```

`assets/theme.json` sets the dark variant, the full 13-color palette from the spec
(including `error`, `warning`, `success`, which the host ignores today), seven rounding
metrics, `borderSize`, `spacing`, `padding`, `scrollbarSize`, and an unused
`typography.uiScale`. See section 8 for which keys actually take effect.

### 7.2 `inject-hello` (`inject`)

```json
{
  "schema": 1,
  "id": "com.casioemu.inject.hello",
  "name": "Hello Injection",
  "version": "1.0.0",
  "author": "CasioEmuMsvc",
  "description": "An example injection payload list.",
  "min_host": "2.1.0",
  "platforms": ["macos"],
  "type": "inject",
  "permissions": ["emulator.inject"],
  "entry_point": "assets/payloads.json",
  "assets": ["assets/payloads.json"]
}
```

`assets/payloads.json` contains one payload, `example.hello`, target `any`, address
`0xE9E0`, bytes `3C 3C 66 66 7E 7E 66 66`, `safe: true`. The host checks that
`schema` is `1` and that `payloads` exists; it does not parse or validate the payload
fields and does not register the payload in the Injection view.

### 7.3 `layout-compact` (`ui_revamp`)

```json
{
  "schema": 1,
  "id": "com.casioemu.layout.compact",
  "name": "Compact Layout",
  "version": "1.0.0",
  "author": "CasioEmuMsvc",
  "description": "Hides the tool dock and keeps a compact workspace sidebar.",
  "min_host": "2.1.0",
  "platforms": ["macos"],
  "type": "ui_revamp",
  "permissions": ["ui.shell"],
  "entry_point": "assets/layout.json",
  "assets": ["assets/layout.json"]
}
```

`assets/layout.json` describes `sidebar.visible/width`, `bottom.visible/height/active`,
`tools.visible/width/tabs/hidden`, and `sessions.tabs/showClose`. It loads as
`Loaded`; no values are applied by the preview host.

### 7.4 `tool-clock` (`tool`)

```json
{
  "schema": 1,
  "id": "com.casioemu.tool.clock",
  "name": "Clock Tool",
  "version": "1.0.0",
  "author": "CasioEmuMsvc",
  "description": "Example native tool plugin that adds a clock window.",
  "min_host": "2.1.0",
  "platforms": ["macos"],
  "type": "tool",
  "permissions": ["ui.shell"],
  "entry_point": "assets/info.json",
  "native_entry": "lib/tool_clock.dylib",
  "assets": ["assets/info.json"]
}
```

`assets/info.json` is `{ "name", "description", "icon": null }`; it is not read by the
native loader. `src/tool_clock.cpp` defines `ClockWindow` and `fPluginLoad`.
`build.sh` compiles the dylib into `lib/`. `lib/tool_clock.dylib` is checked in as a universal
(arm64 + x86_64) binary; verify with `lipo -info`.

### 7.5 `tab-notes` (`tab`)

```json
{
  "schema": 1,
  "id": "com.casioemu.tab.notes",
  "name": "Session Notes",
  "version": "1.0.0",
  "author": "CasioEmuMsvc",
  "description": "Example native tab plugin with a scratch notes panel.",
  "min_host": "2.1.0",
  "platforms": ["macos"],
  "type": "tab",
  "permissions": ["ui.shell", "filesystem.read", "filesystem.write"],
  "entry_point": "assets/info.json",
  "native_entry": "lib/tab_notes.dylib",
  "assets": ["assets/info.json"]
}
```

`src/tab_notes.cpp` adds a `NotesWindow` with a 4 KiB `InputTextMultiline` buffer. The
notes are per window instance, not per emulator session, and are not persisted; the
`filesystem.*` permissions are declared but unused. `assets/info.json` is not read.
`lib/tab_notes.dylib` is checked in as a universal (arm64 + x86_64) binary.

---

## 8. Theme document schema and token mapping

There is no C++ `ThemeApi` class. The entire theme surface is
`ApplyThemeDocument(jsonText, error)` in `ThemeTokens.cpp` plus the ImGui style it
assigns. A theme plugin gets no API pointer.

### 8.1 Document

```json
{
  "schema": 1,
  "name": "Display name (unused by the loader)",
  "variant": "dark",
  "tokens": {
    "colors": { },
    "metrics": { },
    "typography": { }
  }
}
```

- `schema` must be the integer `1`. Missing or wrong: `theme schema must be 1`.
- `variant` is optional, defaults to `dark`, and must be `dark` or `light`. It selects
  `ImGui::StyleColorsDark` or `StyleColorsLight` as the base style before tokens are
  applied. Missing is fine; any other string is an error.
- `tokens` is required and must be an object. `colors` and `metrics` are optional but
  must be objects if present. Unknown groups and unknown keys are ignored silently —
  there is no strict mode in this host.

### 8.2 Colors

Accepted values: exactly 7 characters (`#RRGGBB`) or 9 characters (`#RRGGBBAA`),
with a leading `#`. Hex digits are case-insensitive. Anything else fails with
`colors.<key> is not a valid color`.

| Token | ImGui colors assigned |
| --- | --- |
| `background` | `WindowBg`, `PopupBg`, `MenuBarBg`, `TitleBg` |
| `surface` | `ChildBg`, `FrameBg`, `Tab`, `TabUnfocused` |
| `surfaceAlt` | `FrameBgHovered`, `TabUnfocusedActive` |
| `foreground` | `Text` |
| `muted` | `TextDisabled` |
| `border` | `Border`, `Separator` |
| `accent` | `CheckMark`, `SliderGrab`, `HeaderActive`, `TabActive`, `ButtonActive` |
| `accentHover` | `ButtonHovered`, `HeaderHovered`, `SliderGrabActive`, `TabHovered` |
| `selection` | `Header`, `TextSelectedBg`, `NavHighlight` |
| `hover` | `FrameBgHovered`, `HeaderHovered`, `TabHovered` |

`hover` is applied last, so it overrides the `surfaceAlt` / `accentHover` values for
`FrameBgHovered`, `HeaderHovered`, and `TabHovered` when both are present.

The spec example's `error`, `warning`, and `success` keys are not read. Unspecified
colors keep their value from the selected base style (`StyleColorsDark`/`Light`).

### 8.3 Metrics

All values are floats (`NumberOr` accepts any JSON number; non-numbers fall back).

| Key | ImGui style field |
| --- | --- |
| `windowRounding` | `WindowRounding` |
| `childRounding` | `ChildRounding` |
| `popupRounding` | `PopupRounding` |
| `frameRounding` | `FrameRounding` |
| `tabRounding` | `TabRounding` |
| `grabRounding` | `GrabRounding` |
| `scrollbarRounding` | `ScrollbarRounding` |
| `borderSize` | `WindowBorderSize` |
| `frameBorderSize` | `FrameBorderSize` |
| `scrollbarSize` | `ScrollbarSize` |
| `spacing` | `ItemSpacing = (v, v)` — applied only when `v > 0` |
| `padding` | `WindowPadding = (v, v)` — applied only when `v > 0` |
| `framePaddingX` | `FramePadding.x` |
| `framePaddingY` | `FramePadding.y` |

Important: the style starts from a default-constructed `ImGuiStyle` plus
`StyleColors*`, not from the app's own default layout metrics. Any metric you omit
falls back to ImGui's default, not to `ThemeManager`'s branded defaults. Specify the
rounding/spacing/padding values you want, as the example does.

### 8.4 Typography

`tokens.typography` is not read anywhere in the host. `uiScale`, `monoFamily`, and
`uiFamily` have no effect in this tree. Fonts are managed by `ThemeManager`; a theme
cannot add or select fonts.

### 8.5 Lifetime and interaction with the app theme

- Applying a theme assigns `ImGui::GetStyle()` immediately, sets
  `g_plugin_theme_active = true`, and records the document in the registry.
- Only one style exists. If several theme plugins load, the last one scanned wins.
  Disabling a theme resets to the built-in dark style via `ApplyDefaultTheme()`.
- Switching light/dark mode in Settings clears `g_plugin_theme_active` and overwrites
  the plugin theme (an explicit user action wins).
- **Startup font rebuild is handled.** `Gui/Ui.cpp` already calls
  `PluginRegistry::RenderTrustModal()` every frame; that is also the registry's
  per-frame pump. When `ThemeManager::ProcessFontRebuild` sets its request flag, the
  pump marks the active theme stale and re-applies the plugin document on the next
  frame after the rebuild completes. The same happens after any later font rebuild.
  `PluginRegistry::ReapplyActiveTheme()` is available for manual re-application.
- A declarative `ui_revamp` that selects a built-in theme (`shell.theme`) sets the
  shell default. A theme plugin that loaded first still takes precedence for the ImGui
  style and is re-applied after rebuilds.

## 9. `PluginApi` reference (host API 3)

Header: `CasioEmuMsvc/src/Plugin/PluginApi.h`.
Entry point:

```cpp
extern "C" void fPluginLoad(casioemu::PluginApi* api);
```

`casioemu::PluginApi` is actually declared at global scope; the examples use
`fPluginLoad(PluginApi* api)`. Use the same spelling as the header.

Only native plugins (`tab`, `tool`, native `ui_revamp`) receive this pointer. The host
creates a **per-plugin, permission-scoped** `PluginApi` facade and calls `fPluginLoad`
once, on the main thread, after the ImGui context exists.

Each native plugin must declare a permission that grants `native`: `ui.shell` (the
normal choice for a tool/tab) or `process`. A native package with neither fails with
`plugin.permission`.

### 9.1 `PluginApi`

```cpp
class PluginApi {
public:
    [[nodiscard]] virtual int GetVersion() = 0;   // PLUGIN_API_VERSION == 3
    virtual void AddWindow(UIWindow* wnd) = 0;
    [[nodiscard]] virtual bool RegisterPlugin(const char* id, const char* name, int version) = 0;
    virtual void AssertFundamentalSTL(size_t a, size_t b, size_t c, size_t d) = 0;
    [[nodiscard]] virtual void* QueryInterface(const char* name) = 0;
    template <class T> [[nodiscard]] T* QueryInterface();
    virtual void* GetImGuiContext() = 0;
    [[nodiscard]] virtual bool RegisterPlugin(const char* id, const char* name,
        const char* version, const char* author = nullptr, const char* desc = nullptr) = 0;
};
```

| Method | Behavior |
| --- | --- |
| `GetVersion()` | Returns `PLUGIN_API_VERSION` (`3`). |
| `AddWindow(UIWindow*)` | Requires `ui.window`. Appends to the global window vector and records ownership for unload. Denied: no window, denial logged, entry becomes `Loaded (limited)`. |
| `RegisterPlugin(...)` | Records metadata in `g_loadedPlugins` and prints to stdout. Always returns `true`. |
| `AssertFundamentalSTL(a,b,c,d)` | Panics on STL layout mismatch. `PLUGINASSERTSTL(api)`; include `<map>`/`<mutex>` if your translation unit needs them (the header now includes them). |
| `QueryInterface(const char*)` | Returns `nullptr` unless the plugin has the capability that interface requires (table in 9.12). |
| `GetImGuiContext()` | Requires `ui.window`; otherwise returns `nullptr`. |
| `PLUGINASSERTSTL` | As before. |
| `PLUGIN_EXPORT_ABI()` | Macro exporting `fPluginApiVersion()`/`fPluginAbiSignature()`; put it at file scope. |

`PLUGINASSERTSTL`:

```cpp
#define PLUGINASSERTSTL(x) \
    x->AssertFundamentalSTL(sizeof(std::string), sizeof(std::vector<int>), \
                            sizeof(std::map<int, int>), sizeof(std::mutex))
```

The macro expands `sizeof(std::map<...>)` and `sizeof(std::mutex)`; the public header now
includes `<map>` and `<mutex>` so the macro compiles in a standalone plugin.

### 9.2 `UIWindow`

```cpp
class UIWindow {
public:
    UIWindow(const char* name);
    const char* name{};
    bool open = true;
    bool bring_to_front_requested = false;
    ImVec2 inital_size{800, 800};      // spelling is "inital"
    ImGuiWindowFlags flags{};
    virtual void Render();
    void BringToFront();
    virtual void RenderCore() = 0;
    virtual void GotoMemoryAddress(uint32_t addr) {}
    virtual ~UIWindow() {}
};
```

This is the **same layout and vtable as the host's `Gui/Ui.hpp` `UIWindow`** in API v3:
same member order, `Render` → `RenderCore` → `GotoMemoryAddress` → destructor slot
order, and the same `BringToFront` helper. The host adds a `static_assert` against a
mirror of this declaration and validates `fPluginAbiSignature()` at load, so the old
"extra bool and extra virtual" hazard is closed. Plugins built with the v1/v2 SDK still
load (with an ABI warning) but should be rebuilt.

Still avoid host-special-cased window names (`Settings`, `Calculator`, `RAM`, `ROM`,
`All`, `PRAM`, `Flash`, `T4x ...`). `Settings` is forced open and `Calculator` closes
back to the menu.

### 9.3 `IPlatformHost`

```cpp
class IPlatformHost {
public:
    virtual const char* GetInternalStoragePath() = 0;
    virtual const char* GetExternalStoragePath() = 0;
    virtual bool ShowFileOpenDialog(const char* title, const char* filters,
        char* selectedFilePath, size_t pathBufferSize) = 0;
    virtual void OpenSystemFileDialog(std::function<void(std::string)> callback) = 0;
    virtual void SaveSystemFileDialog(const char* preferred_name,
        std::function<void(std::string)> callback) = 0;
    virtual void ShowMessageBox(const char* title, const char* message, int type) = 0;
};
```

| Method | Notes |
| --- | --- |
| `GetInternalStoragePath()` | macOS: `"."` (the app chdirs next to the executable at startup). |
| `GetExternalStoragePath()` | macOS: `"."`. |
| `ShowFileOpenDialog` | Synchronous native open dialog via `FileDialog`. |
| `OpenSystemFileDialog` / `SaveSystemFileDialog` | Asynchronous dialogs with a path callback. |
| `ShowMessageBox` | SDL message box; `type` 0=info, 1=warning, 2=error. |

### 9.4 `IEmulator`

```cpp
class IEmulator {
public:
    virtual float* SolarPanelVoltage() = 0;
    virtual float* BatteryVoltage() = 0;
    virtual casioemu::ModelInfo* ModelDefinition() = 0;
    virtual void RequestScreenshot() = 0;
    virtual bool IsPaused() = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual unsigned int GetCyclesPerSecond() = 0;
    virtual void SetCyclePerSecond(uint32_t cps) = 0;
    virtual std::string GetModelFilePath(std::string relative_path) = 0;
    virtual void* GetRenderer() = 0;
    virtual void* GetInterfaceTexture() = 0;
    virtual const char* GetRunningModelName() = 0;
    virtual const char* GetRunningRomPath() = 0;
};
```

Voltage pointers are live floats owned by the emulator. `GetRenderer()` returns the
`SDL_Renderer*`; `GetInterfaceTexture()` the calculator interface texture. Pause/Resume
cooperate with the debugger's run state.

### 9.5 `ICPU`

```cpp
class ICPU {
public:
    virtual uint16_t* Register(const char* name) = 0;
};
```

Returns a pointer to the raw register proxy value, or `nullptr`. Intended for the
legacy CPU path; prefer `IDebugger::ReadRegister`/`WriteRegister` for cross-model code.

### 9.6 `IMMU`

```cpp
class IMMU {
public:
    virtual uint8_t ReadData(size_t addr) = 0;
    virtual void WriteData(size_t addr, uint8_t dat) = 0;
    virtual uint16_t ReadCode(size_t addr) = 0;
    virtual void WriteCode(size_t addr, uint8_t dat) = 0;
};
```

Dispatches to the active core (T4x, EPS, or legacy MMU). Note that code addressing
differs per core; the EPS path shifts byte addresses to word addresses internally.

### 9.7 `IChipset`

```cpp
class IChipset {
public:
    virtual void RaiseInterrupt(int index) = 0;
    virtual void Tick() = 0;
    enum struct RunStatus { Stop, Halt, Run, DeepHalt, HaltH };
    virtual void SetStatus(RunStatus status) = 0;
    virtual RunStatus GetStatus() = 0;
    virtual void* GetRom() = 0;
    virtual size_t GetRomSize() = 0;
};
```

`Tick()` takes the emulator execution lock. `GetRom()` exposes the raw ROM buffer;
`GetRomSize()` its byte size.

### 9.8 `IKeyboard`

```cpp
class IKeyboard {
public:
    virtual void Key(int ki, int ko, bool pressed) = 0;
    virtual void ReleaseAll() = 0;
    virtual void PressCode(uint8_t code, bool pressed) = 0;
};
```

`ki` 0–7 (KI line), `ko` 0–9 (KO line). `PressCode` uses the raw `kiko` byte from
`ModelInfo::buttons[].kiko`.

### 9.9 `IDebugger`

```cpp
class IDebugger {
public:
    virtual uint32_t GetProgramCounter() = 0;
    virtual std::vector<DebugRegisterInfo> GetRegisters() = 0;
    virtual bool ReadRegister(const char* name, uint32_t& value, uint32_t& bitWidth) = 0;
    virtual bool WriteRegister(const char* name, uint32_t value) = 0;

    virtual std::vector<uint8_t> ReadMemory(uint32_t address, size_t size) = 0;
    virtual void WriteMemory(uint32_t address, const std::vector<uint8_t>& data) = 0;
    virtual std::vector<uint16_t> ReadCode(uint32_t byteAddress, size_t count) = 0;
    virtual void WriteCode(uint32_t byteAddress, const std::vector<uint8_t>& data) = 0;
    virtual std::vector<DebugDisassemblyLine> GetDisassembly(uint32_t address, size_t count) = 0;

    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void Reset() = 0;
    virtual bool StepInto() = 0;
    virtual bool StepOver() = 0;
    virtual bool StepOut() = 0;

    virtual std::vector<uint32_t> GetExecutionBreakpoints() = 0;
    virtual bool AddExecutionBreakpoint(uint32_t address) = 0;
    virtual bool RemoveExecutionBreakpoint(uint32_t address) = 0;
    virtual void ClearExecutionBreakpoints() = 0;

    virtual std::vector<DebugMemoryBreakpointInfo> GetMemoryBreakpoints() = 0;
    virtual std::vector<DebugMemoryBreakpointHitInfo> GetMemoryBreakpointHits(
        uint32_t address, bool write) = 0;
    virtual bool AddMemoryBreakpoint(uint32_t address, bool write, bool breakWhenHit,
        bool enabled = true, bool compareData = false, uint8_t data = 0,
        uint8_t mask = 0xff, uint64_t skipCount = 0) = 0;
    virtual bool RemoveMemoryBreakpoint(uint32_t address, bool write) = 0;
    virtual void ClearMemoryBreakpoints() = 0;

    virtual std::string GetBacktrace() = 0;
    virtual std::vector<DebugStackFrameInfo> GetStackFrames() = 0;
    virtual std::vector<DebugLabelInfo> GetLabels(const char* query, size_t limit) = 0;
    virtual std::vector<DebugVariableInfo> GetVariables() = 0;

    virtual std::vector<DebugSnapshotInfo> GetSnapshots() = 0;
    virtual uint32_t SaveSnapshot(uint32_t parentId, const char* label) = 0;
    virtual bool LoadSnapshot(uint32_t id, std::string& error) = 0;
    virtual bool DeleteSnapshot(uint32_t id) = 0;
    virtual bool ExportSnapshots(const char* path, uint32_t id, bool subtree,
        std::string& error) = 0;
    virtual bool ImportSnapshots(const char* path, std::string& error) = 0;

    virtual std::vector<DebugAddressLockInfo> GetAddressLocks() = 0;
    virtual void SetAddressLock(uint32_t address, uint8_t value, bool locked) = 0;
    virtual bool RemoveAddressLock(uint32_t address) = 0;
    virtual void ClearAddressLocks() = 0;

    virtual void StartCallRecording(bool filterCaller, uint32_t caller,
        bool filterCallee, uint32_t callee) = 0;
    virtual void StopCallRecording() = 0;
    virtual void ClearCallRecording() = 0;
    virtual std::vector<DebugFunctionCallInfo> GetFunctionCalls(uint32_t function) = 0;

    virtual void SetCyclesPerSecond(uint32_t cps) = 0;
    virtual void SetPdValue(uint8_t value) = 0;
    virtual DebugDisplaySettings GetDisplaySettings() = 0;
    virtual void SetDisplaySettings(const DebugDisplaySettings& settings) = 0;
    virtual DebugQrCodeInfo GetQrCode() = 0;
    virtual void RequestScreenshot() = 0;
    virtual void RequestRecording(bool start) = 0;
    virtual bool HotReloadRom(std::string& error) = 0;
};
```

Behavior notes for the preview host:

- Register reads are case-insensitive (names are lowercased). The legacy path exposes
  `pc`, `lr`, and each register proxy; T4x exposes `pc` and `r0..r63`; EPS exposes
  named EPS registers plus `rXX` aliases.
- `WriteRegister` on T4x requires the emulator to be paused; EPS and legacy paths do
  not.
- `WriteMemory` on T4x requires paused; other cores write unconditionally.
- `ReadCode`/`WriteCode` use byte addresses, align words down to even bytes, and
  maintain the ROM image for EPS patching.
- `GetDisassembly` returns empty when no code viewer exists.
- `StepInto`/`StepOver`/`StepOut` require paused state and an available code viewer;
  T4x has its own stepping path.
- Execution breakpoints are limited: T4x addresses must be `< 65536`, EPS addresses
  `< RomWordCount()`; otherwise `AddExecutionBreakpoint` returns false.
- `SaveSnapshot` defaults the label to `"MCP Snapshot"` when empty.
- `GetVariables` needs the RAM buffer; returns empty otherwise.
- `GetQrCode` polls the QR state before returning it.
- `SetDisplaySettings` clamps values (flashing threshold 0–63, brightness 1.0–8.0,
  buffer select 0–2).
- `HotReloadRom` pauses, reloads, and restores the previous pause state.
- Most memory/register/stack accessors take `m_emu->access_mx`; the debugger interface
  is the thread-safe way to touch emulator state from a plugin.

### 9.10 `Hooks`

```cpp
class Hooks {
public:
    virtual void SetupOnInstructionHook(std::function<void(InstructionEventArgs&)> handler) = 0;
    virtual void SetupOnCallFunctionHook(std::function<void(const FunctionEventArgs&)> handler) = 0;
    virtual void SetupOnFunctionReturnHook(std::function<void(const FunctionEventArgs&)> handler) = 0;
    virtual void SetupOnMemoryReadHook(std::function<void(MemoryEventArgs&)> handler) = 0;
    virtual void SetupOnMemoryWriteHook(std::function<void(MemoryEventArgs&)> handler) = 0;
    virtual void SetupOnBrkHook(std::function<void(InterruptEventArgs&)> handler) = 0;
    virtual void SetupOnInterruptHook(std::function<void(InterruptEventArgs&)> handler) = 0;
    virtual void SetupOnResetHook(std::function<void()> handler) = 0;
};
```

Event argument structs (from `PluginApi.h`):

```cpp
struct FunctionEventArgs { uint32_t pc{}; uint32_t lr{}; };
struct MemoryEventArgs   { uint32_t offset{}; bool handled{}; uint8_t value{}; };
struct InterruptEventArgs{ uint8_t index{}; bool handled{}; };
struct InstructionEventArgs {
    uint32_t pc_before;
    uint32_t pc_after;
    bool should_break{};
};
```

Hooks are registered per backend: the legacy CPU and EPS paths are wired for every
hook; T4x additionally participates in call, return, memory, interrupt, and reset
hooks. Handlers run on the emulation thread, so do not touch ImGui or call UI code
from them.

### 9.11 `DebuggerTypes.h` structs

| Struct | Fields |
| --- | --- |
| `DebugRegisterInfo` | `Name`, `Value`, `BitWidth` |
| `DebugStackFrameInfo` | `ProgramCounter`, `LinkRegister`, `StackPointer`, `Er0`, `Er2`, `LinkRegisterPushed`, `IsJump`, `Symbol` |
| `DebugLabelInfo` | `Address`, `Name` |
| `DebugVariableInfo` | `Name`, `RealAddress`, `ImaginaryAddress`, `RealValue`, `ImaginaryValue`, `RealHex`, `ImaginaryHex`, `HasImaginaryPart` |
| `DebugMemoryBreakpointInfo` | `Address`, `Write`, `BreakWhenHit`, `HitCount`, `Enabled`, `CompareData`, `Data`, `Mask`, `SkipCount` |
| `DebugMemoryBreakpointHitInfo` | `ProgramCounter`, `LinkRegister`, `Stack`, `Value`, `Registers` (`vector<DebugRegisterInfo>`) |
| `DebugDisassemblyLine` | `Address`, `Text`, `IsLabel`, `CrossReference` |
| `DebugSnapshotInfo` | `Id`, `ParentId`, `Label`, `Timestamp`, `PreviewSize`, `StateSize` |
| `DebugAddressLockInfo` | `Address`, `Value`, `Locked` |
| `DebugFunctionCallInfo` | `Function`, `Caller`, `Xr0`, `Stack` |
| `DebugDisplaySettings` | `FlashingThreshold`, `FlashingBrightness`, `BufferSelect`, `FadingEnabled`, `FadingCoefficient`, `ResidualEnabled`, `ResidualAlphaScale`, `AudioEnabled` |
| `DebugQrCodeHistoryEntry` | `Id`, `Version`, `Data` |
| `DebugQrCodeInfo` | `Active`, `Complete`, `Version`, `Revision`, `Data`, `History`, `RealCurrentPage`, `RealTotalPages`, `RealCurrentPageData`, `RealPageLengths` |

### 9.12 Interface availability by plugin type

| Type | Receives `PluginApi*` | Can add windows | Can use debugger/emulator/hooks | Required permission |
| --- | --- | --- | --- | --- |
| `theme` | No | No | No | `ui.shell` |
| `inject` | No | No | No | `emulator.inject` |
| `ui_revamp` declarative | No | No | No | `ui.shell` |
| `ui_revamp` native | Yes | Yes | Yes | `ui.shell` (or `process` + `emulator.*` for a no-window tool) |
| `tab` | Yes | Yes | Yes | `ui.shell` |
| `tool` | Yes | Yes | Yes | `ui.shell` |

Capability requirements for `QueryInterface<T>()`:

| Interface | Capability |
| --- | --- |
| `IPlatformHost` | `filesystem.read`/`filesystem.write`/`ui.window` (methods checked individually) |
| `IEmulator` | `emulator.read`; pause/resume/cycle/voltage need `emulator.write` |
| `ICPU` | `emulator.write` (it only returns a writable register pointer) |
| `IMMU` | reads need `emulator.read`; `WriteData` needs `emulator.write`; `WriteCode` needs `emulator.code` |
| `IChipset` | reads need `emulator.read`; `RaiseInterrupt`/`Tick`/`SetStatus` need `emulator.write` |
| `Hooks` | `emulator.read` |
| `IKeyboard` | `emulator.write` |
| `IDebugger` | reads need `emulator.read`; mutating methods need `emulator.write`; `WriteCode`/`HotReloadRom` need `emulator.code`; snapshot import/export need `filesystem.read`/`filesystem.write` |
| chipset peripheral interfaces | any of `emulator.read`/`write`/`code` |

Denied calls return a null/empty/default value, never the real object.

## 10. Debugging

### 10.1 Plugin Manager window

`PluginLogWindow` (`CasioEmuMsvc/src/Gui/PluginLogWindow.*`) is a normal `UIWindow`
named **Plugin Manager** (900×560, starts closed). Open it from the app's window menu.

**Installed** tab:

- buttons: **Install Plugin...**, **Open Plugins Folder**, **Reload Declarative**;
- table columns: Name (+ id), Version, Type, State, Permissions, Actions;
- state colors: green `Loaded`; amber `Loaded (limited)`; red `Error`/`Invalid`/blocked;
  amber `Awaiting approval`/`Deferred`; muted everything else;
- hover the state cell for the full error text and, for limited plugins, the denied
  capability list;
- per-row actions: Review (pending/blocked), Disable/Load/Enable, Unload (loaded native),
  Reveal, Remove (with a confirmation modal);
- empty state explains the install options.

**Developer** tab:

- host API version and host version (`v3 (2.2.0)`);
- settings, user plugins, app plugins, and cache paths;
- **Copy Log** / **Clear Log**;
- the in-memory load log, colored by `[ERROR]`/`[FATAL]`, `[WARN]`, `[SUCCESS]`.

### 10.2 Logs

`g_PluginLoadLog` (declared in `PluginMan.h`, defined in `PluginMan.cpp`) is a plain
`std::string` appended by `PluginRegistry::LogLine`. There is no log level setting and
no file output; copy it from the Developer tab. Typical sequence:

```
[INFO] Plugin host API v3 (2.2.0)
[INFO] User plugins: /Users/you/Library/Application Support/CasioEmuMsvc/Plugins
[INFO] Reading /Users/you/.../Plugins/MyTheme.plugin
[INFO] Trusted plugin com.example.mytheme (approved 2026-11-02T12:34:56Z)
[SUCCESS] Loaded theme plugin com.example.mytheme
[WARN] com.example.tool: 'PluginApi::AddWindow' requires the 'ui.window' capability
```

`PluginApi_Impl::RegisterPlugin` also prints `"<name> loaded."` to stdout.

### 10.3 Common failures

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| No row in the manager | File not top-level in a scan folder, no `.plugin` extension, or the path is a directory | Put the file directly in `.../Plugins/`; restart or use **Install Plugin...**. |
| State `Invalid`, `package.invalid_zip` | Not a zip, corrupted, encrypted, or over a size/ratio/entry limit | Recreate with `zip -r -X`. |
| State `Invalid`, `manifest.missing` | `plugin.json` not at the archive root | Zip from inside the plugin folder. |
| State `Invalid`, `manifest.invalid_json` | Syntax error or UTF-8 BOM | Validate JSON; rewrite without BOM. |
| State `Invalid`, `manifest.*` | Field/schema/type/permission/version problem | Read the message in the tooltip; compare with section 3. |
| State `Invalid`, `package.path` | `..`, absolute path, backslash, symlink/hard-link/special file, duplicate or case-colliding entry | Rebuild the package; one name per file, real regular files only. |
| State `Blocked` | User clicked Block, or a trust record has `blocked: true` | **Review** (or delete the record) and allow. |
| State `Disabled` | Trust record has `enabled: false`, or you disabled it | **Enable**. |
| State `Error`, `native.entry` | `native_entry` path wrong or dylib absent from zip | Check the path and rebuild the package. |
| State `Error`, `native.load` | Wrong architecture, missing dependency, unresolved host symbol | Build universal (`-arch arm64 -arch x86_64`), keep `-undefined dynamic_lookup`. |
| State `Error`, `native.symbol` | Symbol not exported | Add `extern "C" void fPluginLoad(PluginApi*)`. |
| State `Error`, `native.api` | `api_version`/ABI handshake mismatch | Rebuild with the headers in this tree and `PLUGIN_EXPORT_ABI()`. |
| State `Error`, `plugin.permission` | Missing capability for the type/action | Add the permission from section 3.3 and approve the new permission set. |
| State `Error`, `inject.invalid` | Bad payload id/name/address/bytes/target | Fix the JSON; the message names the payload index and field. |
| State `Error`, `layout.invalid` / `layout.unsupported` | Layout value out of range, or an action this host cannot honour (bottom session tabs, unknown key/theme) | Fix the value or use a supported action. |
| State `Loaded (limited)` | Plugin called a host API without the capability | Add the permission (and re-approve) or accept the degraded behavior. |
| Not listed twice | Duplicate `id`; the second package was ignored | Read the `[WARN] Duplicate plugin id` line; change the id or remove one package. |
| Theme has no visible effect | Typography-only theme, or a layout selected another preset afterwards | Use colors/metrics; see section 8. The startup font rebuild no longer overwrites a plugin theme. |
| Layout has no effect | Action was not in the document, or names a tool that does not exist | Check the log; unknown hidden names only warn. |
| Injection not in the Payload view | Plugin not trusted/loaded, or the payload document failed validation | Check the state; `inject.invalid` names the field. |
| Window missing after load | Plugin's `open` is false, or `AddWindow` was denied | Check state/tooltip; open the denied capability list. |
| App crashes at startup | Crash inside `fPluginLoad`, or a window name the host special-cases | Remove the plugin; avoid `RAM`/`ROM`/`T4x...` names. |

### 10.4 Theme appears briefly and then reverts

This was caused by the startup font rebuild re-applying the user's saved style over the
plugin style. It is fixed: `PluginRegistry::PumpFrame()` (called from the existing
`RenderTrustModal()` frame hook) notices `ThemeManager::IsFontRebuildRequested()`,
waits for the rebuild to finish, and re-applies the active theme document on the next
frame. The log shows:

```
[INFO] Reapplied theme plugin com.example.mytheme after font rebuild
```

If a user then switches light/dark mode in Settings, that is an explicit choice and
wins (`g_plugin_theme_active` is cleared). Disable/enable the theme plugin to take it
back.

### 10.5 Iterating without repackaging

- Declarative: edit the file under
  `<SettingsDir>/plugin-cache/<sha256>/<entry_point>` and press **Reload Declarative**.
  Only entries currently `loaded` are re-read.
- Native: rebuild the dylib, then use **Unload** followed by **Enable** in the manager
  (or `PluginRegistry::Unload` + `SetEnabled`) to load the new build without restarting.
- Trust: while developing, `Always Allow` once; a same-permission declarative rebuild
  is accepted silently, a native rebuild prompts each time by design.
- If a scan is confusing, quit the app, delete `<SettingsDir>/plugin-cache/` and (if
  you want to re-approve) `<SettingsDir>/plugin-trust.json`, then restart.

---

## 11. Security model and limits

What the host does:

- SHA-256 hashes the package file and keys the extraction cache by hash.
- Rejects absolute paths, `..`, backslash separators, empty components, symlinks,
  hard-link/special-file entries, duplicate and case-colliding names.
- Enforces entry-count, per-entry size, total size and compression-ratio limits.
- Extracts into the app's cache directory only, using a temp dir plus rename.
- Requires explicit user approval for a new id, a new native hash, or any permission
  change; blocks are remembered by id.
- Shows every declared permission and a native-code warning before load.
- Loads dylibs with `dlopen(..., RTLD_NOW | RTLD_LOCAL)`.
- Catches C++ exceptions escaping `fPluginLoad`/`fPluginUnload` and reports them.
- **Enforces declared permissions** at loader gates and every `PluginApi` entry point;
  denied calls return null/default, log the denial and mark the plugin limited.
- Unloads native libraries on Unload/Disable/Remove and at process exit, hiding plugin
  windows first. Libraries with registered emulation hooks are retained because the
  hook API has no unregister call.
- Validates the SDK `UIWindow` layout at compile time (host mirror `static_assert`) and
  the plugin ABI at load time (`fPluginAbiSignature`/`fPluginApiVersion`).

What the host does **not** do:

- No sandbox, entitlement restriction, seccomp, or App Sandbox container. Native code
  runs in the app process with the user's full access.
- No code signing, notarization, or publisher verification. The trust prompt is the
  security boundary.
- Permission enforcement covers **host API calls**, not direct OS calls. A native
  plugin can still `open(2)`, `socket(2)`, `fork(2)`, or `dlopen` its own libraries
  behind the host's back. Treat native plugins as native programs.
- No protection against a plugin crashing the app (`abort`, segfault, `std::terminate`),
  hanging the UI thread, or exhausting memory.
- No protection against trust-store tampering: `plugin-trust.json` is user-writable.
- No isolation of declarative plugins either, but the host only implements styling,
  layout, payload validation and injection registration for them.
- No code-signature-based hash history: `history` is an audit trail, not a signature.

Additional ABI risks:

- Plugin and host must use the same libc++/C++ standard library and a compatible
  deployment target. `PLUGINASSERTSTL` catches STL size drift, not every ABI detail.
- `PluginApi.h`'s `UIWindow` now matches `Gui/Ui.hpp`; keep them in sync and bump
  `kPluginUiWindowAbiRevision` on changes.
- Universal example dylibs are arm64 + x86_64. Build third-party plugins universal too.

Design guidance: treat every `.plugin` you install as equivalent to running a native
program as yourself, because that is what it is. For third-party distribution, publish
source and build instructions rather than opaque dylibs, and prefer declarative plugins
where the host supports them.
