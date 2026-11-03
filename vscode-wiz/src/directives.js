'use strict';

// Editor metadata for Wiz (.wz) and CWiz (.cwiz).
//
//   DIRECTIVES - the canonical directive table.  Copied from docs/DIRECTIVES.md;
//                the authoritative implementation is compiler/src/directives.cpp
//                (`directive_table()`).
//   TYPES      - rom / rom8 / __normalized__ and the base types, from
//                docs/DIRECTIVES.md §"Custom types".
//   BUILTINS   - built-in and standard-library calls from docs/API.md.
//
// `test/data.test.js` re-reads compiler/src/directives.cpp when the toolchain is
// checked out next to the extension, so the table cannot silently drift.

// `args: null` means the directive takes no argument.
const DIRECTIVES = [
  {
    name: 'if',
    args: 'EXPR',
    context: ['preprocess'],
    target: 'any',
    doc: 'Conditional compilation. Every `#if` must be closed by `#endif`.',
  },
  {
    name: 'ifdef',
    args: 'NAME',
    context: ['preprocess'],
    target: 'any',
    doc: 'True when `NAME` is defined.',
  },
  {
    name: 'ifndef',
    args: 'NAME',
    context: ['preprocess'],
    target: 'any',
    doc: 'True when `NAME` is not defined.',
  },
  {
    name: 'elif',
    args: 'EXPR',
    context: ['preprocess'],
    target: 'any',
    doc: 'Else-if branch of a conditional (inside `#if`).',
  },
  {
    name: 'elifdef',
    args: 'NAME',
    context: ['preprocess'],
    target: 'any',
    doc: 'C23 `#elif defined(NAME)`. Inside `#if`.',
  },
  {
    name: 'elifndef',
    args: 'NAME',
    context: ['preprocess'],
    target: 'any',
    doc: 'C23 `#elif !defined(NAME)`. Inside `#if`.',
  },
  {
    name: 'else',
    args: null,
    context: ['preprocess'],
    target: 'any',
    doc: 'Final branch of a conditional, once per `#if`.',
  },
  {
    name: 'endif',
    args: null,
    context: ['preprocess'],
    target: 'any',
    doc: 'Close the matching `#if` / `#ifdef` / `#ifndef`.',
  },
  {
    name: 'define',
    args: 'NAME ...',
    context: ['preprocess'],
    target: 'any',
    doc: 'Object-like or function-like macro. `__VA_OPT__` is supported.',
  },
  {
    name: 'undef',
    args: 'NAME',
    context: ['preprocess'],
    target: 'any',
    doc: 'Remove a macro definition.',
  },
  {
    name: 'pragma',
    args: 'once',
    context: ['preprocess'],
    target: 'any',
    doc: '`once` is the only supported pragma.',
  },
  {
    name: 'error',
    args: 'text',
    context: ['preprocess'],
    target: 'any',
    doc: 'Stop compilation with a message.',
  },
  {
    name: 'include',
    args: '"file"',
    context: ['preprocess'],
    target: 'any',
    doc:
      'Inline another source file, relative to the including file. ' +
      'Angle includes are accepted only for the built-in freestanding headers ' +
      '(`<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<limits.h>`, `<string.h>`, ' +
      '`<stdalign.h>`, `<stdnoreturn.h>`, `<stdarg.h>`, `<stdckdint.h>`); there is ' +
      'no host system include path.',
  },
  {
    name: 'line',
    args: 'N ["file"]',
    context: ['preprocess'],
    target: 'any',
    doc: 'Adjust the line/file mapping used by diagnostics.',
  },
  {
    name: 'embed',
    args: '"file"',
    context: ['preprocess'],
    target: 'any',
    doc: 'Expand a binary file into a byte initializer list.',
  },
  {
    name: 'for',
    args: 'TARGET[, TARGET]',
    context: ['preprocess'],
    target: '580 / 880',
    doc:
      'Start a target section. Canonical targets are `580` and `880`; `cwapp` and ' +
      '`all` are also accepted. Compatibility aliases (`#for cw`, `#for 580vnx`, ' +
      '`#for 880btg`, …) build but are reported by `preproc/noncanonical-directive`.',
  },
  {
    name: 'cwapp',
    args: null,
    context: ['preprocess'],
    target: '580 (cw)',
    doc:
      'Compile as the fx-580VN X firmware app. Must follow `#for 580` ' +
      '(`preproc/cwapp-order`).',
  },
  {
    name: 'slot',
    args: 'N',
    context: ['preprocess'],
    target: '580 (cw)',
    doc:
      'Menu slot to replace, `1`..`12`. Only after `#cwapp`, at most once ' +
      '(`preproc/slot-order`, `preproc/slot-range`, `preproc/duplicate-slot`). ' +
      'Decimal: `#slot 2`, not `#slot 0x2`.',
  },
  {
    name: 'binary',
    args: '"file.bin"',
    context: ['preprocess'],
    target: 'fwnx',
    doc: 'Use a full firmware image as the build input.',
  },
  {
    name: 'org',
    args: 'ADDR',
    context: ['preprocess', 'rop'],
    target: '580 (cwapp) / ROP',
    doc:
      'ROP payload origin, or `#cwapp` metadata (the patch slot chooses the real ' +
      'address and the value is recorded in `CwizPreprocessInfo`). Canonical spelling ' +
      'is uppercase `0x` hex (`#org 0xE9E0`); bare hex and `[0xE9E0]` are aliases ' +
      'reported by `preproc/noncanonical-directive`.',
  },
  {
    name: 'rop',
    args: null,
    context: ['rop'],
    target: '580 / 880',
    doc: 'Select the ROP gadget-chain backend. Must be the first directive.',
  },
  {
    name: 'pad',
    args: 'N',
    context: ['rop'],
    target: '580 / 880',
    doc:
      'Trailing null-buffer size in bytes, decimal (default 256). ' +
      '`#pad 0x100` is a compatibility alias reported by lint.',
  },
  {
    name: 'nopad',
    args: null,
    context: ['rop'],
    target: '580 / 880',
    doc: 'Emit no trailing null buffer.',
  },
  {
    name: 'launcher',
    args: 'common [short|medium|long] [backup] [main] | simple [address]',
    context: ['rop'],
    target: '580 / 880',
    doc:
      'Launcher stub. Canonical spellings are `#launcher common` and ' +
      '`#launcher simple [address]`; `shared`/`chung` and `stub` are aliases, and a ' +
      'bare address means `simple`.',
  },
];

const TYPES = [
  {
    name: 'rom',
    args: null,
    context: ['preprocess'],
    doc:
      '**16-bit word storage-class qualifier.** File-scope arrays and tables only: ' +
      '`rom int name[N] = { ... };` (N 16-bit words in ROM); Wiz form ' +
      '`rom name = [ ... ]`. Scalars, parameters, `rom char` and `rom int*` are ' +
      'rejected (`sema.type-placement` / `preproc.attribute-placement`). Access at ' +
      'run time with `peek`/`_rpeek` or a pointer.',
  },
  {
    name: 'rom8',
    args: null,
    context: ['preprocess'],
    doc:
      '**Byte storage-class qualifier.** File-scope arrays only: ' +
      '`rom8 char name[N] = "...";` (N bytes in ROM, plus a terminating NUL for a ' +
      'string literal); Wiz form `rom8 name = "..."`. `rom8 int` is rejected.',
  },
  {
    name: '__normalized__',
    args: null,
    context: ['preprocess'],
    doc:
      '**CWiz declaration attribute.** Rewrites the string literals on that line to ' +
      'CW Display F4 bytes before lexing (Vietnamese text). It must be the first ' +
      'attribute on a `char` / `rom8 char` array: ' +
      '`__normalized__ rom8 char title[20] = "Tiếng Việt";`. Rejected on `rom int`, ' +
      'non-char arrays, scalars and functions (`preproc.attribute-placement`).',
  },
];

// Built-in and standard-library calls.  Signatures and descriptions are copied
// from the tables in docs/API.md.
const BUILTINS = [
  { name: 'pixel', signature: 'pixel(x, y[, c])', doc: 'Set the pixel at `(x, y)`; off-screen coordinates are ignored. With three arguments, `c != 0` draws and `c == 0` clears.' },
  { name: 'clear', signature: 'clear()', doc: 'Clear the shadow buffer (SB2). Call `render()` to show it.' },
  { name: 'clearRender', signature: 'clearRender()', doc: 'Clear SB1 first, then SB2 (immediately visible).' },
  { name: 'key', signature: 'key()', doc: 'Scan index `0..63` of the key being held (`8*KO + KI`), or `-1` if none.' },
  { name: 'key_down', signature: 'key_down(k)', doc: '`1` while key `k` is held.' },
  { name: 'key_hit', signature: 'key_hit(k)', doc: '`1` only on the call where `k` went from released to held. Call it every loop iteration.' },
  { name: 'delay', signature: 'delay(n)', doc: 'Busy-wait `n` units (uncalibrated).' },
  { name: 'peek', signature: 'peek(addr)', doc: 'Unsigned byte at `addr` (RAM, SFR or ROM), `0..255`.' },
  { name: 'poke', signature: 'poke(addr, v)', doc: 'Store the low byte of `v` at `addr`.' },
  { name: 'memoryRead', signature: 'memoryRead(addr, len)', doc: 'Read 1 byte unsigned or 2 bytes as a signed 16-bit little-endian value; other lengths return 0.' },
  { name: 'memoryWrite', signature: 'memoryWrite(addr, len, data)', doc: 'Write the low 1 or 2 bytes of `data` little-endian; other lengths do nothing.' },
  { name: 'setStatus', signature: 'setStatus(icon, status)', doc: 'Set a target-supported status icon to `HIGH` (1) or `LOW` (0) in SB2. Call `render()`.' },
  { name: 'render', signature: 'render()', doc: 'Copy the shadow buffer (SB2) to the LCD (SB1). This is the only call that makes drawing visible.' },
  { name: 'renderStatus', signature: 'renderStatus()', doc: 'Copy only the first 23 status bytes from SB2 to the LCD plane(s).' },
  { name: 'buffered', signature: 'buffered(on)', doc: 'Compatibility no-op; drawing always targets SB2.' },
  { name: 'flip', signature: 'flip()', doc: 'Alias for `render()` (kept for older programs).' },
  { name: 'exit', signature: 'exit()', doc: '`cwapp` only: leave the program and restart the firmware (returns to the menu).' },
  { name: '_hspan', signature: '_hspan(x0, x1, y, c)', doc: 'Internal fast span fill, `x0..x1-1`. Requires `0 <= x0 < x1 <= 192` and `0 <= y < ROWS`.' },
  { name: '_rpeek', signature: '_rpeek(addr)', doc: 'Internal: like `peek` but assumes the data segment of ROM tables (used on `cwapp`).' },
  { name: 'abs', signature: 'abs(n)', doc: 'Absolute value.' },
  { name: 'min', signature: 'min(a, b)', doc: 'Smaller of two values.' },
  { name: 'max', signature: 'max(a, b)', doc: 'Larger of two values.' },
  { name: 'clamp', signature: 'clamp(v, lo, hi)', doc: '`v` constrained to `[lo, hi]`.' },
  { name: 'sign', signature: 'sign(n)', doc: '`-1`, `0` or `1`.' },
  { name: 'wait_key', signature: 'wait_key()', doc: 'Wait for all keys to be released, then for a press; returns the key.' },
  { name: 'srand', signature: 'srand(s)', doc: 'Seed the 16-bit LCG (`seed = seed * 25173 + 13849`). Default seed is `12345`.' },
  { name: 'rand', signature: 'rand(n)', doc: 'Next value in `0..n-1`.' },
  { name: 'hline', signature: 'hline(x0, x1, y, c = 1)', doc: 'Horizontal span `x0..x1-1` (half-open).' },
  { name: 'vline', signature: 'vline(x, y0, y1, c = 1)', doc: 'Vertical span `y0..y1-1` (half-open).' },
  { name: 'rect', signature: 'rect(x, y, w, h, c = 1)', doc: 'Rectangle outline.' },
  { name: 'fillrect', signature: 'fillrect(x, y, w, h, c = 1)', doc: 'Filled rectangle.' },
  { name: 'line', signature: 'line(x0, y0, x1, y1, c = 1)', doc: 'Bresenham line.' },
  { name: 'circle', signature: 'circle(cx, cy, r, c = 1)', doc: 'Circle outline.' },
  { name: 'disc', signature: 'disc(cx, cy, r, c = 1)', doc: 'Filled circle.' },
  { name: 'char', signature: 'char(x, y, c)', doc: 'Draw one 6x8 character, ASCII code `c`. Wiz spelling; CWiz uses `glyph()` because `char` is a type.' },
  { name: 'glyph', signature: 'glyph(x, y, c)', doc: 'Draw one 6x8 character, ASCII code `c`. CWiz spelling of `char()`.' },
  { name: 'text', signature: 'text(x, y, s)', doc: 'Draw a NUL-terminated string from ROM; returns the end `x`.' },
  { name: 'num', signature: 'num(x, y, n)', doc: 'Draw signed decimal `n`; returns the end `x`.' },
  { name: 'char_cw', signature: 'char_cw(x, y, c)', doc: 'Draw one native ClassWiz CW Display character (11x13, 1x). Wiz spelling.' },
  { name: 'glyph_cw', signature: 'glyph_cw(x, y, c)', doc: 'Draw one native ClassWiz CW Display character (11x13, 1x). CWiz spelling.' },
  { name: 'text_cw', signature: 'text_cw(x, y, s)', doc: 'Draw a string with the native CW Display font; returns the end `x`.' },
  { name: 'num_cw', signature: 'num_cw(x, y, n)', doc: 'Draw signed decimal `n` with the native CW Display font; returns the end `x`.' },
  { name: 'normalizeText', signature: 'normalizeText(s)', doc: 'Convert a UTF-8 byte string to CW Display F4 bytes in a static 256-byte buffer; returns the buffer address.' },
  { name: 'sprite', signature: 'sprite(x, y, w, h, data)', doc: 'Draw a 1-bit-per-pixel bitmap; `data` is a `rom8` table (or table + offset).' },
  { name: 'print', signature: 'print(x, y, a, b, c, ...)', doc: 'Lay out arguments left to right from `(x, y)`, rendering string literals / `rom8` tables as text and everything else as a number. Needs at least three arguments; returns the final `x`.' },
  { name: 'print_cw', signature: 'print_cw(x, y, a, b, c, ...)', doc: 'Same layout rule as `print()` using the native CW Display font (`text_cw`/`num_cw`).' },
];

const BY_NAME = new Map();
for (const item of DIRECTIVES) BY_NAME.set(item.name, item);

const TYPES_BY_NAME = new Map();
for (const item of TYPES) TYPES_BY_NAME.set(item.name, item);

const BUILTINS_BY_NAME = new Map();
for (const item of BUILTINS) BUILTINS_BY_NAME.set(item.name, item);

function findDirective(name) {
  return BY_NAME.get(name) || null;
}

function findType(name) {
  return TYPES_BY_NAME.get(name) || null;
}

function findBuiltin(name) {
  return BUILTINS_BY_NAME.get(name) || null;
}

module.exports = { DIRECTIVES, TYPES, BUILTINS, findDirective, findType, findBuiltin };
