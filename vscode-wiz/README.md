# Wiz / CWiz for VS Code

Syntax highlighting, snippets and a linter for the two languages of the
**Wiz** toolchain for Casio ClassWiz calculators:

| Language | Extension | Style |
|----------|-----------|-------|
| Wiz | `.wz` | Python-like |
| CWiz | `.cwiz` | C-like with `#for`, `#cwapp`, `#include` |

This extension is one of the two linter integrations. The other one is built
into the **CasioEmuMsvc** debugger (the diagnostics panel of the **Wiz Compiler**
tab). Both implement the same rule ids.

The editor linter runs in two stages:

1. **In-memory rules** (`src/linter.js`) run while you type. They need no
   compiler and understand comments, strings, multi-line declarations,
   function parameters and scopes.
2. **Compiler diagnostics** are added for saved files by running
   `wizlint --json`. If `wizlint` is not installed the extension silently keeps
   the in-memory result.

## Linter

Diagnostics appear as you type (350 ms debounce). Saved files are also checked
with `wizlint --json` (see `wiz.lint.useCompiler`). Run **Wiz: Lint the current
file** from the command palette to run both stages on demand.

| Rule | What it reports |
|------|-----------------|
| `readability/unused-variable` | A declared variable is never read |
| `correctness/shadowed-variable` | A local hides a global with the same name |
| `correctness/unreachable-code` | A statement follows `return`/`break`/`continue` |
| `preproc/duplicate-target` | A `#for target` is listed twice |
| `preproc/unknown-target` | `#for` names something other than `580`/`880` |
| `preproc/cwapp-order` | `#cwapp` is not preceded by `#for 580` |
| `preproc/slot-order` | `#slot` is not inside a `#cwapp` section |
| `preproc/slot-range` | `#slot` is not a number from 1 to 12 |
| `preproc/duplicate-slot` | A `#cwapp` section has more than one `#slot` |
| `style/line-length` | Line longer than 100 columns |
| `style/trailing-whitespace` | Trailing spaces or tabs |
| `style/tabs` | Tab indentation (the examples use spaces) |
| `style/todo` | A `TODO`/`FIXME`/`XXX` marker in a comment |
| `compile` | The compiler's own first error (`wizlint --json`) |

Settings:

| Setting | Default | Meaning |
| --- | --- | --- |
| `wiz.lint.onSave` | `true` | Lint when a document is saved |
| `wiz.lint.onType` | `true` | Lint while typing (350 ms debounce) |
| `wiz.lint.lineLength` | `100` | `style/line-length` limit |
| `wiz.lint.useCompiler` | `true` | Run `wizlint --json` on saved files |
| `wiz.lint.compilerPath` | `wizlint` | Path to the command-line linter |
| `wiz.lint.target` | `580` | Target for the compiler pass (`580`, `880`, `cwapp`) |

The command-line linter is the authority for compiler diagnostics:

```
wizlint file.cwiz --target 580
```

## Directives

| Directive | Meaning |
|-----------|---------|
| `#for 580` / `#for 880` | Start a section for that calculator |
| `#cwapp` | Start the fx-580VN X firmware-app section (after `#for 580`) |
| `#slot n` | Menu slot to replace, `1`..`12` (after `#cwapp`; default `2`) |
| `#include "file"` | Include another source file |
| `#define`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, `#pragma` | Normal preprocessor features |

`wizc` embeds a 580 ROM, so `wizc app.cwiz` writes a complete, already-patched
`.bin`; `wizrun app.bin 580` runs it. `--cwapp` is no longer needed.

## Fonts

The extension **does not change the editor font**. It bundles no fonts and sets no
language-specific `editor.fontFamily`, so `.wz`/`.cwiz` files keep your normal
editor font.

The CW font set is used only by compiled Wiz/CWiz programs when they draw text on
the calculator LCD. That glyph table is part of the toolchain
(`compiler/src/font6x8.hpp`), generated from `cw-mono.ttf`.

## Build

**Wiz: Build the current file with wizc** runs `wizc` (path configurable with
`wiz.compilerPath`) and shows its output in the *Wiz build* channel.

## Snippets

`wiz`, `cwapp`, `if`, `for`, `while`, `def`/`fn`, `text`, `keyloop` — see
`snippets/`.

## Package

```
cd vscode-wiz
npm run package        # writes wiz-cwiz-1.0.0.vsix
code --install-extension wiz-cwiz-1.0.0.vsix
```

The language and linter are documented in the Wiz repository:
`docs/GUIDE.md`, `docs/API.md` and `docs/RUNNING.md`.
