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
   `wizlint --json`, with `--max-line-length` taken from `wiz.lint.lineLength`.
   If `wizlint` is not installed the extension silently keeps the in-memory
   result. The JSON carries the compiler's error **code**
   (`sema.undefined-identifier`, `syntax.expected-token`, …), the exact end
   column and an optional fix **hint**. The code is what the Problems panel and
   the diagnostic tooltip show as the diagnostic id (the lint rule id is used
   when a diagnostic has no compiler code), and the rule stays in the message
   as `[… rule]`.

## Linter

Diagnostics appear as you type (350 ms debounce). Saved files are also checked
with `wizlint --json` (see `wiz.lint.useCompiler`). Run **Wiz: Lint the current
file** from the command palette to run both stages on demand.

| Rule | What it reports |
|------|-----------------|
| `readability/unused-variable` | A declared variable is never read |
| `readability/unused-parameter` | A function parameter is never used |
| `readability/empty-block` | An `if`/`while`/`for`/`else` body is empty |
| `readability/redundant-boolean` | `x == true`, `x != false`, … |
| `correctness/shadowed-variable` | A local hides a global with the same name |
| `correctness/shadowed-builtin` | A declaration hides a built-in function |
| `correctness/unreachable-code` | A statement follows `return`/`break`/`continue` |
| `correctness/assignment-in-condition` | `if (x = 1)` instead of `==` |
| `correctness/self-assignment` | `x = x;` |
| `correctness/division-by-zero` | A literal `0` divisor |
| `correctness/constant-condition` | `if (0)` / `while (false)` |
| `correctness/out-of-bounds` | A constant index outside a known array |
| `correctness/missing-return` | A non-void CWiz function has no `return` |
| `preproc/duplicate-target` | A `#for target` is listed twice |
| `preproc/unknown-target` | `#for` names something other than `580`/`880` |
| `preproc/unknown-directive` | A misspelled/unsupported `#directive` |
| `preproc/unbalanced-conditional` | An unmatched `#if`/`#endif`/`#else`/`#elif` |
| `preproc/cwapp-order` | `#cwapp` is not preceded by `#for 580` |
| `preproc/slot-order` | `#slot` is not inside a `#cwapp` section |
| `preproc/slot-range` | `#slot` is not a number from 1 to 12 |
| `preproc/duplicate-slot` | A `#cwapp` section has more than one `#slot` |
| `preproc/noncanonical-directive` | A directive uses a compatibility alias (`#for cw`, `#org E9E0`, `#pad 0x100`, `#launcher shared`, …) |
| `preproc/noncanonical-type` | `rom`/`rom8`/`__normalized__` uses a non-canonical placement |
| `style/line-length` | Line longer than `wiz.lint.lineLength` |
| `style/trailing-whitespace` | Trailing spaces or tabs |
| `style/tabs` | Tab indentation (the examples use spaces) |
| `style/todo` | A `TODO`/`FIXME`/`XXX` marker in a comment |
| `compile` | The compiler's own diagnostic (`wizlint --json`) |

Diagnostics also carry a **`help:`** hint when there is an obvious fix (for
example “use `'=='` for comparison” or “add `#endif`”). The hint appears in the
diagnostic tooltip/related information and in `wizlint`'s terminal output.

Severities are mapped in one place for both stages: `error` → Error,
`warning` → Warning, `note`/`info` → Information, `hint` → Hint. The in-memory
linter and `wizlint` therefore give the same rule the same weight.

Settings:

| Setting | Default | Meaning |
| --- | --- | --- |
| `wiz.lint.enable` | `true` | Turn all diagnostics on/off |
| `wiz.lint.onSave` | `true` | Lint when a document is saved |
| `wiz.lint.onType` | `true` | Lint while typing (350 ms debounce) |
| `wiz.lint.showHints` | `true` | Attach the `help:` hint to a diagnostic |
| `wiz.lint.lineLength` | `100` | `style/line-length` limit, also passed to `wizlint --max-line-length` (clamped to 20–10000) |
| `wiz.lint.useCompiler` | `true` | Run `wizlint --json` on saved files |
| `wiz.lint.compilerPath` | `wizlint` | Path to the command-line linter |
| `wiz.lint.target` | `auto` | `auto`, `580`, `880`, or `cwapp` |

`auto` infers the target from the file's `#for` / `for` sections. For files
without sections it uses the `580`/`880` part of the file name, and it retries
the other calculator if the first one does not compile (for example because a
`K_*` constant only exists on one machine).

The command-line linter is the authority for compiler diagnostics:

```
wizlint file.cwiz --target 580
```

## Directives

The authoritative grammar is `docs/DIRECTIVES.md`; the extension highlights and
completes exactly those spellings.

| Directive | Meaning |
|-----------|---------|
| `#for 580` / `#for 880` | Start a section for that calculator (`cwapp`/`all` also accepted) |
| `#cwapp` | Start the fx-580VN X firmware-app section (after `#for 580`) |
| `#slot n` | Menu slot to replace, `1`..`12` (after `#cwapp`; default `2`) |
| `#binary "file.bin"` | Full firmware image (`fwnx` target) |
| `#org 0xADDR` | `#cwapp` metadata, or the ROP payload origin |
| `#rop`, `#pad N`, `#nopad`, `#launcher common\|simple` | ROP backend |
| `#include "file"`, `#define`, `#undef`, `#pragma once`, `#error`, `#line`, `#embed` | Preprocessor and file directives |
| `#if`, `#ifdef`, `#ifndef`, `#elif`, `#elifdef`, `#elifndef`, `#else`, `#endif` | Conditionals |

Compatibility aliases (`#for cw`, `#org E9E0`, `#pad 0x100`, `#launcher shared`,
…) still build but are reported as `preproc/noncanonical-directive`.

`wizc` embeds a 580 ROM, so `wizc app.cwiz` writes a complete, already-patched
`.bin`; `wizrun app.bin 580` runs it. `--cwapp` is no longer needed.

## Completion and hover

* **Directives (CWiz):** type `#` (or the first word of a line) to get the full
  directive list, including the target/context in the detail pane.
* **Built-ins:** every function from `docs/API.md` (`render`, `key_hit`,
  `print_cw`, `_rpeek`, …) with its signature and description.
* **Types:** `rom`, `rom8` and `__normalized__`, with the placement rules from
  `docs/DIRECTIVES.md`.
* **Hover:** on a `#directive` line, on a built-in call and on the custom types.

The tables live in `src/directives.js`; `npm test` compares them with
`compiler/src/directives.cpp` and `compiler/src/lint.cpp` when the toolchain is
checked out next to the extension, so they cannot drift silently.

## Fonts

The extension **does not change the editor font**. It bundles no fonts and sets no
language-specific `editor.fontFamily`, so `.wz`/`.cwiz` files keep your normal
editor font.

The CW font set is used only by compiled Wiz/CWiz programs when they draw text on
the calculator LCD. The toolchain has two glyph tables: the original 6x8 table
(`compiler/src/font6x8.hpp`, generated from `cw-mono.ttf`) and the native
ClassWiz CW Display 11x13 table (`compiler/src/font_cw.hpp`, rasterised at 1x
from `Wenti-D/ClasswizDisplayFont`). The latter is used by `print_cw`,
`text_cw`, `num_cw` and `char_cw`/`glyph_cw`.

## Build

**Wiz: Build the current file with wizc** runs `wizc` (path configurable with
`wiz.compilerPath`) and shows its output in the *Wiz build* channel.

## Snippets

`wiz`, `cwapp`, `if`, `for`, `while`, `def`/`fn`, `text`, `keyloop` — see
`snippets/`.

## Package

```
cd vscode-wiz
npm test               # unit checks for the linter, data tables and providers
npm run test:diff      # optional: compare with a built ../build/wizlint
npm run package        # writes wiz-cwiz-1.3.0.vsix
code --install-extension wiz-cwiz-1.3.0.vsix
```

`scripts/package.js` stages the VSIX under `vscode-wiz/temp/` (gitignored) and
deletes it again, so nothing is written to a system temp directory.

The language and linter are documented in the Wiz repository:
`docs/GUIDE.md`, `docs/API.md` and `docs/RUNNING.md`.
