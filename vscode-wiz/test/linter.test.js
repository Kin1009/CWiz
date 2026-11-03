'use strict';

// Unit checks for src/linter.js.  Run with `npm test` (node --test).
//
// The two preprocessor rule families added here mirror
// compiler/src/lint.cpp; each case below is a behaviour that file defines.

const test = require('node:test');
const assert = require('node:assert');
const { RULES, lintText, clampLineLength } = require('../src/linter');

function lint(source, languageId = 'cwiz') {
  return lintText(source, { languageId });
}

function rulesOf(diagnostics) {
  return diagnostics.map((d) => d.rule);
}

function find(diagnostics, rule) {
  return diagnostics.find((d) => d.rule === rule) || null;
}

test('RULES carries both preprocessor rule families', () => {
  assert.strictEqual(RULES['preproc/noncanonical-directive'], 'warning');
  assert.strictEqual(RULES['preproc/noncanonical-type'], 'warning');
});

test('#for target aliases are reported with the canonical spelling', () => {
  const cases = [
    ['#for cw', 580],
    ['#for 580vnx', 580],
    ['#for fx-580vn', 580],
    ['#for 880btg', 880],
    ['#for cwii', 880],
  ];
  for (const [directive, canonical] of cases) {
    const diagnostic = find(lint(`${directive}\nint main() { return 0; }\n`),
      'preproc/noncanonical-directive');
    assert.ok(diagnostic, `${directive} should be flagged`);
    assert.strictEqual(diagnostic.message, '#for uses a compatibility target alias');
    assert.strictEqual(diagnostic.hint, `use '#for ${canonical}'`);
    assert.strictEqual(diagnostic.severity, 'warning');
    // The compiler anchors the diagnostic on the directive name and spans the
    // trimmed line; `npm run test:diff` compares this with wizlint --json.
    assert.strictEqual(diagnostic.col, 2);
    assert.strictEqual(diagnostic.endCol, directive.length + 1);
  }
});

test('canonical #for targets are not reported', () => {
  for (const directive of ['#for 580', '#for 880', '#for 580, 880', '#for all']) {
    const diagnostics = lint(`${directive}\nint main() { return 0; }\n`);
    assert.ok(!find(diagnostics, 'preproc/noncanonical-directive'), directive);
  }
});

test('bare-hex and bracketed #org addresses are reported', () => {
  for (const directive of ['#org E9E0', '#org [E9E0]', '#org e9e0']) {
    const diagnostic = find(lint(`${directive}\nint main() { return 0; }\n`),
      'preproc/noncanonical-directive');
    assert.ok(diagnostic, `${directive} should be flagged`);
    assert.strictEqual(diagnostic.message, '#org uses a bare hexadecimal address');
    assert.strictEqual(diagnostic.hint, "use '#org 0xE9E0'");
  }
  for (const directive of ['#org 0xE9E0', '#org 256', '#org 0x25000']) {
    const diagnostics = lint(`${directive}\nint main() { return 0; }\n`);
    assert.ok(!find(diagnostics, 'preproc/noncanonical-directive'), directive);
  }
});

test('#pad and #slot hexadecimal arguments are reported', () => {
  const pad = find(lint('#pad 0x100\nint main() { return 0; }\n'),
    'preproc/noncanonical-directive');
  assert.ok(pad);
  assert.strictEqual(pad.message, '#pad uses a hexadecimal size');
  assert.strictEqual(pad.hint, "use a decimal size, for example '#pad 256'");

  const slot = find(
    lint('#for 580\n#cwapp\n#slot 0x2\nint main() { return 0; }\n'),
    'preproc/noncanonical-directive',
  );
  assert.ok(slot);
  assert.strictEqual(slot.message, '#slot uses a hexadecimal number');
  assert.strictEqual(slot.hint, "use a decimal slot, for example '#slot 2'");

  // The slot is also outside 1..12, exactly like the compiler pass.
  const ranged = find(lint('#for 580\n#cwapp\n#slot 0x2\nint main() { return 0; }\n'),
    'preproc/slot-range');
  assert.ok(ranged);
});

test('#launcher compatibility aliases are reported', () => {
  const shared = find(lint('#launcher shared\nint main() { return 0; }\n'),
    'preproc/noncanonical-directive');
  assert.ok(shared);
  assert.strictEqual(shared.hint, "use '#launcher common'");

  const chung = find(lint('#launcher chung backup\nint main() { return 0; }\n'),
    'preproc/noncanonical-directive');
  assert.ok(chung);
  assert.strictEqual(chung.hint, "use '#launcher common'");

  const stub = find(lint('#launcher stub\nint main() { return 0; }\n'),
    'preproc/noncanonical-directive');
  assert.ok(stub);
  assert.strictEqual(stub.hint, "use '#launcher simple'");

  const address = find(lint('#launcher 0x8154\nint main() { return 0; }\n'),
    'preproc/noncanonical-directive');
  assert.ok(address);
  assert.strictEqual(address.message,
    '#launcher places the simple stub with a bare address');
  assert.strictEqual(address.hint, "use '#launcher simple 0x8154'");

  for (const directive of ['#launcher common', '#launcher simple',
    '#launcher common short backup', '#launcher simple 0x8154']) {
    const diagnostics = lint(`${directive}\nint main() { return 0; }\n`);
    assert.ok(!find(diagnostics, 'preproc/noncanonical-directive'), directive);
  }
});

test('rom8/rom element types must be char/int', () => {
  const rom8 = find(lint('rom8 int table[2];\nint main() { return 0; }\n'),
    'preproc/noncanonical-type');
  assert.ok(rom8);
  assert.strictEqual(rom8.message, 'rom8 is a byte storage qualifier');
  assert.strictEqual(rom8.hint, 'write \'rom8 char name[N] = "...";\'');
  assert.strictEqual(rom8.severity, 'warning');

  const rom = find(lint('rom char table[2];\nint main() { return 0; }\n'),
    'preproc/noncanonical-type');
  assert.ok(rom);
  assert.strictEqual(rom.message, 'rom is a 16-bit word storage qualifier');
  assert.strictEqual(rom.hint, 'write \'rom int name[N] = { ... };\'');

  for (const source of [
    'rom int table[2] = { 1, 2 };\nint main() { return 0; }\n',
    'rom8 char table[2] = "hi";\nint main() { return 0; }\n',
    'const rom int table[2] = { 1, 2 };\nint main() { return 0; }\n',
  ]) {
    assert.ok(!find(lint(source), 'preproc/noncanonical-type'), source);
  }
});

test('__normalized__ must be first on a char array', () => {
  for (const source of [
    '__normalized__ int table[2] = "a";\nint main() { return 0; }\n',
    'static __normalized__ char buf[2] = "a";\nint main() { return 0; }\n',
    'char prefix[2] = "a"; __normalized__ rom8 char title[4] = "b";\nint main() { return 0; }\n',
  ]) {
    const diagnostic = find(lint(source), 'preproc/noncanonical-type');
    assert.ok(diagnostic, source);
    assert.strictEqual(diagnostic.message,
      '__normalized__ must be the first attribute on a char array');
  }
  for (const source of [
    '__normalized__ rom8 char title[20] = "Tieng Viet";\nint main() { return 0; }\n',
    '__normalized__ char buf[8] = "abc";\nint main() { return 0; }\n',
  ]) {
    assert.ok(!find(lint(source), 'preproc/noncanonical-type'), source);
  }
});

test('the type rules stay CWiz-only and ignore directives and comments', () => {
  // 'rom' in a Wiz file is a table definition, not a C storage class.
  assert.ok(!find(lint('rom table = [1, 2]\n', 'wiz'), 'preproc/noncanonical-type'));
  // A macro body is not a declaration.
  assert.ok(!find(lint('#define ROM8_TABLE rom8 int\nint main() { return 0; }\n'),
    'preproc/noncanonical-type'));
  // Blanked comments and literals do not count.
  assert.ok(!find(lint('/* rom8 int table[2]; */\nint main() { return 0; }\n'),
    'preproc/noncanonical-type'));
  assert.ok(!find(lint('int main() { text(0, 0, "rom8 int"); return 0; }\n'),
    'preproc/noncanonical-type'));
});

test('newer directives are known, not "unknown directive"', () => {
  const diagnostics = lint(
    '#define A 1\n#if A\n#elifdef B\n#elifndef C\n#else\n#endif\n' +
    '#undef A\n#line 3 "x.cwiz"\n#embed "table.bin"\n' +
    'int main() { return 0; }\n',
  );
  assert.ok(!find(diagnostics, 'preproc/unknown-directive'));
});

test('line length uses the configured limit and clamps like wizlint', () => {
  const long = `int main() { return 0; }${' '.repeat(100)}\n`;
  assert.ok(lint(long).some((d) => d.rule === 'style/line-length'));
  assert.ok(!lintText(long, { languageId: 'cwiz', lineLength: 200 })
    .some((d) => d.rule === 'style/line-length'));
  // wizlint rejects --max-line-length outside 20..10000.
  assert.strictEqual(clampLineLength(5), 20);
  assert.strictEqual(clampLineLength(120), 120);
  assert.strictEqual(clampLineLength(99999), 10000);
  assert.strictEqual(clampLineLength(undefined), 100);
});

test('existing rules still fire', () => {
  assert.deepStrictEqual(
    rulesOf(lint('int main() {\n    int x = 1;\n    return 0;\n}\n')),
    ['readability/unused-variable'],
  );
  assert.ok(find(lint('#if 1\nint main() { return 0; }\n'),
    'preproc/unbalanced-conditional'));
});
