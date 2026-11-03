'use strict';

// Differential check for the two preprocessor rules that exist twice:
// compiler/src/lint.cpp (reported by `wizlint --json`) and src/linter.js
// (reported while typing).  Both must agree on line, column, severity, message
// and hint.
//
//     npm run test:diff
//
// It needs a built `wizlint` in the toolchain checkout next to this extension
// (`../build/wizlint`).  Without it the check is skipped: the .vsix does not
// ship the compiler.  Samples are written under temp/ (gitignored) and removed
// afterwards, per AGENTS.md.

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const { lintText } = require('../src/linter');

const root = path.resolve(__dirname, '..');
const toolchain = path.resolve(root, '..');
const compiler = path.join(toolchain, 'build', 'wizlint');
const staging = path.join(root, 'temp', 'parity');

const SAMPLES = [
  ['for-cw', '#for cw\nint main() { return 0; }\n'],
  ['for-580vnx', '#for 580vnx\nint main() { return 0; }\n'],
  ['for-880btg', '#for 880btg\nint main() { return 0; }\n'],
  ['for-canonical', '#for 580, 880\nint main() { return 0; }\n'],
  ['org-bare', '#for 580\n#cwapp\n#org E9E0\nint main() { return 0; }\n'],
  ['org-bracket', '#org [E9E0]\nint main() { return 0; }\n'],
  ['org-hex', '#org 0xE9E0\nint main() { return 0; }\n'],
  ['pad-hex', '#pad 0x100\nint main() { return 0; }\n'],
  ['slot-hex', '#for 580\n#cwapp\n#slot 0x2\nint main() { return 0; }\n'],
  ['launcher-shared', '#launcher shared\nint main() { return 0; }\n'],
  ['launcher-stub', '#launcher stub\nint main() { return 0; }\n'],
  ['launcher-addr', '#launcher 0x8154\nint main() { return 0; }\n'],
  ['launcher-simple', '#launcher simple 0x8154\nint main() { return 0; }\n'],
  ['rom8-int', 'rom8 int table[2];\nint main() { return 0; }\n'],
  ['rom-char', 'rom char table[2];\nint main() { return 0; }\n'],
  ['rom-int', 'rom int table[2] = { 1, 2 };\nint main() { return 0; table[0]; }\n'],
  ['rom8-char', 'rom8 char table[2] = "hi";\nint main() { return 0; table[0]; }\n'],
  ['normalized-bad', '__normalized__ int table[2] = "a";\nint main() { return 0; }\n'],
  ['normalized-late', 'static __normalized__ char buf[2] = "a";\nint main() { return 0; }\n'],
  ['normalized-good', '__normalized__ rom8 char title[20] = "Tieng";\nint main() { return 0; }\n'],
];

const FAMILIES = new Set(['preproc/noncanonical-directive', 'preproc/noncanonical-type']);

function digest(items) {
  return items
    .filter((item) => FAMILIES.has(item.rule))
    .map((item) => `${item.line}:${item.col} ${item.rule} | ${item.message} | ${item.hint || ''}`)
    .sort();
}

function runCompiler(file) {
  try {
    return JSON.parse(execFileSync(compiler, ['--json', file], { encoding: 'utf8' }) || '[]');
  } catch (error) {
    return JSON.parse(error.stdout || '[]');
  }
}

if (!fs.existsSync(compiler)) {
  console.log(`skipped: ${compiler} is not built`);
  process.exit(0);
}

fs.mkdirSync(staging, { recursive: true });
let mismatches = 0;
try {
  for (const [name, source] of SAMPLES) {
    const file = path.join(staging, `${name}.cwiz`);
    fs.writeFileSync(file, source);
    const expected = digest(runCompiler(file));
    const actual = digest(lintText(source, { languageId: 'cwiz' }));
    const same = JSON.stringify(expected) === JSON.stringify(actual);
    console.log(`${same ? 'ok  ' : 'DIFF'} ${name}`);
    if (!same) {
      mismatches++;
      console.log(`  wizlint: ${expected.join('\n           ') || '(nothing)'}`);
      console.log(`  linter : ${actual.join('\n           ') || '(nothing)'}`);
    }
  }
} finally {
  fs.rmSync(staging, { recursive: true, force: true });
}

console.log(mismatches ? `${mismatches} mismatch(es)` : 'all samples agree');
process.exitCode = mismatches ? 1 : 0;
