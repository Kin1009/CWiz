# Plugin examples

The example packages live in the emulator repository under `examples/plugins/`. Each
folder contains `plugin.json`, its `assets/` document, and — for the native examples —
source, a `build.sh`, a checked-in `lib/*.dylib`, and an `assets/info.json` that the
native loader does not read.

The checked-in dylibs are **universal (arm64 + x86_64)** and export the API v3 ABI
handshake (`PLUGIN_EXPORT_ABI()`). There are no `README.md` files inside the example
folders.

| Example | Type | What it demonstrates | Actually applied by the host? |
| --- | --- | --- | --- |
| `theme-vscode-dark` | `theme` | JSON design tokens, dark palette | Yes (style assignment) |
| `inject-hello` | `inject` | Named payload with address and bytes | Yes; validated and registered with the Payload view |
| `layout-compact` | `ui_revamp` | Declarative tool/sidebar layout | Yes; supported actions applied, unsupported ones fail the load |
| `tool-clock` | `tool` | Native `UIWindow` via `PluginApi` | Yes |
| `tab-notes` | `tab` | Native notes window | Yes; notes are per window, not per session |

Full manifests and per-asset explanations are in
[`../PLUGIN-GUIDE.md`](../PLUGIN-GUIDE.md#7-the-five-example-plugins). The host contract is
[`../PLUGIN_SPEC.md`](../PLUGIN_SPEC.md); internals are in
[`COMPONENTS.md`](COMPONENTS.md).

## Build a native example

The native examples are dylibs loaded by the host. `build.sh` compiles the source with
the same flags the other examples use:

```console
$ cd examples/plugins/tool-clock
$ ./build.sh
```

which runs, in effect:

```console
$ clang++ -std=c++20 -dynamiclib -undefined dynamic_lookup -fPIC \
    -I"$ROOT/CasioEmuMsvc/src" \
    -I"$ROOT/CasioEmuMsvc/src/Plugin" \
    -I"$ROOT/CasioEmuMsvc/src/Gui/imgui" \
    -o "examples/plugins/tool-clock/lib/tool_clock.dylib" \
    "examples/plugins/tool-clock/src/tool_clock.cpp"
```

`tab-notes/build.sh` is identical except for `tab_notes.dylib` and `tab_notes.cpp`.

Package from **inside** the example folder so `plugin.json` is at the archive root:

```console
$ cd examples/plugins/tool-clock
$ ./build.sh
$ zip -r -X ../ToolClock.plugin .
```

Do not run `zip -r ToolClock.plugin tool-clock` from `examples/plugins/`; that puts
`plugin.json` inside a `tool-clock/` directory and the host rejects the package with
`manifest.missing`.

`ToolClock.plugin` then contains `plugin.json`, `assets/`, `src/`, `lib/`, and
`build.sh`. The loader ignores the source and the build script.

## Install

Copy the `.plugin` into:

```
~/Library/Application Support/CasioEmuMsvc/Plugins/
```

or into `<executable dir>/plugins/`. The macOS bundle also scans
`<executable dir>/../Resources/plugins`. A portable build (`portable.flag` next to the
executable) uses `<executable dir>/settings/Plugins` instead of Application Support.

There are three ways in:

1. **Install Plugin...** in the Plugin Manager (`PluginLogWindow.cpp:32`) opens a
   file picker, copies the package into the user folder and loads it — no restart.
2. Copy the `.plugin` into the plugins folder yourself (the **Open Plugins
   Folder** button reveals it) and **restart the app**: discovery runs at
   startup. **Reload Declarative** only re-applies declarative packages that were
   already discovered, so it will not pick up a newly copied file.
3. Drop it into the folder before starting the app.

Drag-and-drop onto the window is still not wired (`plugins/COMPONENTS.md` §9). The
first load shows the trust modal with identity, SHA-256, and every requested
permission; native packages also show the native-code warning.

- **Always Allow** writes a trust record keyed by the manifest `id`. The record stores
  the version, SHA-256, permission list, and enabled/blocked flags.
- Trust decisions compare the SHA-256 and the permission list (exact order). The
  version is stored but not compared.
- **Allow Once** loads for the session without writing a record.
- **Block** is keyed by id, so a later rebuild of the same id stays blocked until
  reviewed.

## Declarative examples

The theme, inject, and layout examples need no build step:

```console
$ cd examples/plugins/theme-vscode-dark
$ zip -r -X ../../../VSCodeDark.plugin .
```

Adjust the output path so it lands outside the repository or wherever you collect
packages. The same pattern works for `inject-hello` and `layout-compact`.

Expected results:

- `theme-vscode-dark`: the palette/metrics are assigned to the ImGui style. The
  `typography.uiScale` key and the `error`/`warning`/`success` colors are ignored. See
  the theme-token mapping and the startup font-rebuild caveat in the guide.
- `inject-hello`: state `Loaded`, but the payload is not listed in the Injection view.
- `layout-compact`: state `Loaded`, but no layout change is applied.

## Package rules

- `plugin.json` must be at the archive root, UTF-8 without BOM.
- `id` must be unique, lower-case reverse-DNS with at least one dot, 3–128 bytes.
- Paths inside the zip must be relative; `..`, absolute paths, backslashes, empty
  components, and Unix symlink entries are rejected.
- The package is hashed (SHA-256) and extracted to
  `<settings>/plugin-cache/<sha256>/`; the original `.plugin` file is never modified.
- Only top-level `.plugin` files in the scanned folders are considered; the extension
  match is case-insensitive.
- A second package with the same id is ignored with a warning; the first scanned
  folder wins (user folder first).

See `docs/PLUGIN_SPEC.md` for the manifest schema, permissions, and native API, and
[`TROUBLESHOOTING.md`](TROUBLESHOOTING.md) for failure modes.
