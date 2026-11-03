# Wiz Editor Reference — Directives, Targets & Autocomplete

Status: **design reference — autocomplete is not implemented** (verified
2026-11-02). Items 1.1/1.2/1.4 have no code; 1.5 is limited to the target and
language choosers plus the `file - target` header in
`CasioEmuMsvc/src/Gui/WizToolchain.cpp`. The editor currently uses the C++
language definition (`WizToolchain.cpp:241`), so item 10.6 is also open.

## Directive → Target / Language mapping

Directives are parsed from source lines starting with `#`. The mapping below is
the intended source for the editor status bar (1.5) and tooltip parser (1.4).

Canonical grammar and examples live in [DIRECTIVES.md](DIRECTIVES.md).  The
editor highlights only these spellings; compatibility aliases are still
accepted by the compiler and reported by `wizlint`.

| Directive | Target (hardware) | Language | Description |
|---|---|---|---|
| `#for 580`, `#for 880` | 580 / 880 | CWiz | Target-section directive (canonical names) |
| `#cwapp` | 580 (cw) | CWiz | Firmware app / patch section only |
| `#slot N` | 580 (cw) | CWiz | Menu slot (1..12) for `#cwapp` |
| `#binary "file"` | fwnx | CWiz | Full firmware image |
| `#org 0xADDR` | 580 (cwapp) / ROP | CWiz | Firmware metadata or ROP payload origin |
| `#rop` | 580 / 880 | CWiz | ROP payload / gadget-chain output |
| `#launcher common` | 580 / 880 | ROP | Launcher stub |
| `#pad N`, `#nopad` | 580 / 880 | ROP | Trailing null buffer |
| `#include "file"`, `#include <stdint.h>` | any | CWiz | Project include / freestanding header |
| `#define`, `#undef`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#elifdef`, `#elifndef`, `#else`, `#endif` | any | CWiz | Preprocessor |
| `#pragma once`, `#error`, `#line`, `#embed` | any | CWiz | File/preprocessor directives |
| `for 580` / `for 880` | 580 / 880 | Wiz | Target sections (Wiz syntax) |

Target ids used by the compiler and the CLI (`wiz::canonical` in
`compiler/src/targets.cpp`):
- **580** — `580` (internal/alias name `cw`); the `cwapp` flavour adds the
  fx-580VN X firmware-patch emission mode.
- **880** — `880` (internal/alias name `cwii`).
- **fwnx** — full firmware image for `#binary` payloads (`--target fwnx`); not a
  bootable target.

There are **no** `cwrop`/`cwiirop` targets: the ROP backend is selected by the
`#rop` directive inside a normal `.cwiz`/`.wz` source, and `wizc` reports it as
`[ROP 580]` or `[ROP 880]`. An editor that offers a "ROP target" must map it to
the source's declared calculator, not to a separate target id.

Language names:
- **Wiz** — `.wz` source (original)
- **CWiz** — `.cwiz` source (C-style syntax)

## Autocomplete rules (1.1–1.2)

Trigger conditions:
- **Ctrl+Space** always opens dropdown.
- **Word-boundary trigger**: when typing an alphabetic key (`[A-Za-z]`) or after a period (`.`), the dropdown is refreshed with suggestions that match the current token.
- **Directive trigger**: when typing `#`, the list prioritises directives (`#cwapp`, `#slot`, `#for`, `#rop`).

Dropdown contents (keyword list from API.md + g_labels + file names):
- Built-in functions: `clear`, `clearRender`, `pixel`, `key`, `key_down`, `key_hit`, `delay`, `peek`, `poke`, `render`, `flip`, `exit`, `print`, `text`, `num`, `str`, `num_cw`, `char`, `glyph`, `sprite`, `hline`, `vline`, `rect`, `fillrect`, `line`, `circle`, `disc`, `abs`, `min`, `max`, `clamp`, `sign`, `wait_key`, `srand`, `rand`, `char_cw`, `glyph_cw`, `text_cw`, `num_cw`, `print_cw`
- Directives: `#for`, `#cwapp`, `#slot`, `#org`, `#rop`, `#launcher`, `#pad`, `#nopad`, `#include`, `#define`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#elifdef`, `#elifndef`, `#else`, `#endif`, `#pragma`, `#error`, `#line`, `#embed`
- File names from project / examples (`program.cwiz`, `01_hello.wz`, `01_target.cwiz`)

Planned for 1.2 (not implemented yet): next to each item a docs snippet is shown
beside the cursor:
- Example: `#cwapp` → snippet: `cwapp | cwiz | 580 — firmware app directive`
- Example: `print()` → snippet: `Built-in: layout text/numbers at (x,y)`

Planned for 1.5 (not implemented yet): the editor status bar displays the active
directive/target/lang badge:
`cwapp | cwiz | 580` or `Wiz · 580` depending on parsed source line.
