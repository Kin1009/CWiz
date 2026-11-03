'use strict';

// Consistency checks for the editor data tables and the TextMate grammars.
//
// When the Wiz toolchain is checked out next to the extension, the tables are
// compared with the authoritative C++ sources (`compiler/src/directives.cpp`
// and `compiler/src/lint.cpp`).  A standalone install of the .vsix has no
// toolchain, so those two comparisons are skipped instead of failing.

const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const { DIRECTIVES, TYPES, BUILTINS } = require('../src/directives');
const { RULES, lintText } = require('../src/linter');

const root = path.resolve(__dirname, '..');
const toolchain = path.resolve(root, '..', 'compiler', 'src');

function readIfPresent(file) {
  try {
    return fs.readFileSync(file, 'utf8');
  } catch {
    return null;
  }
}

function grammar(name) {
  return JSON.parse(fs.readFileSync(path.join(root, 'syntaxes', name), 'utf8'));
}

function matchPatterns(grammarObject) {
  const matches = [];
  for (const repository of Object.values(grammarObject.repository)) {
    for (const pattern of repository.patterns || []) {
      if (pattern.match) matches.push(pattern.match);
    }
  }
  return matches;
}

test('directive names are unique and complete', () => {
  const names = DIRECTIVES.map((d) => d.name);
  assert.deepStrictEqual(names, [...new Set(names)]);
  assert.ok(DIRECTIVES.length >= 20);
  for (const item of DIRECTIVES) {
    assert.ok(item.doc && item.doc.length > 10, `doc for #${item.name}`);
    assert.ok(Array.isArray(item.context) && item.context.length, `context for #${item.name}`);
    assert.ok(item.target, `target for #${item.name}`);
  }
});

test('completion data covers the documented directive set', () => {
  const names = new Set(DIRECTIVES.map((d) => d.name));
  for (const name of ['for', 'cwapp', 'slot', 'org', 'rop', 'launcher', 'pad', 'nopad',
    'include', 'define', 'if', 'ifdef', 'ifndef', 'elif', 'elifdef', 'elifndef', 'else',
    'endif', 'pragma', 'error', 'line', 'embed', 'binary', 'undef']) {
    assert.ok(names.has(name), `#${name} is missing from the directive table`);
  }
  const types = new Set(TYPES.map((t) => t.name));
  assert.deepStrictEqual([...types].sort(), ['__normalized__', 'rom', 'rom8']);
});

test('builtin data has no duplicates', () => {
  const names = BUILTINS.map((b) => b.name);
  assert.deepStrictEqual(names, [...new Set(names)]);
  for (const item of BUILTINS) {
    assert.ok(item.signature.startsWith(`${item.name}(`), `signature for ${item.name}`);
    assert.ok(item.doc.length > 5, `doc for ${item.name}`);
  }
});

test('both grammars know the same directives and no phantom types', () => {
  const removedTypes = ['rom16', 'int8', 'int16', 'arr', '__asm__', '__hex__'];
  const addedDirectives = ['elifdef', 'elifndef', 'undef', 'line', 'embed', 'binary'];

  for (const file of ['wiz.tmLanguage.json', 'cwiz.tmLanguage.json']) {
    const source = fs.readFileSync(path.join(root, 'syntaxes', file), 'utf8');
    const parsed = JSON.parse(source);
    const matches = matchPatterns(parsed);
    for (const type of removedTypes) {
      for (const match of matches) {
        assert.ok(!new RegExp(`\\b${type}\\b`).test(match),
          `${file}: '${type}' must not be highlighted`);
      }
    }
    const directiveMatch = parsed.repository.directives.patterns[0].match;
    for (const name of addedDirectives) {
      assert.ok(directiveMatch.includes(`|${name}|`) || directiveMatch.includes(`|${name})`),
        `${file}: #${name} is missing from the directive alternation`);
    }
    assert.ok(!directiveMatch.includes('__asm__'), `${file}: __asm__ is not a directive`);
    assert.ok(!directiveMatch.includes('__hex__'), `${file}: __hex__ is not a directive`);
    // A duplicated JSON key silently drops one of the two blocks.
    assert.strictEqual((source.match(/"variables":/g) || []).length, 1, `${file}: duplicate "variables" key`);
  }
});

test('directive table matches compiler/src/directives.cpp when present', (t) => {
  const source = readIfPresent(path.join(toolchain, 'directives.cpp'));
  if (!source) {
    t.skip('compiler sources are not next to the extension');
    return;
  }
  const table = source.slice(source.indexOf('kDirectives'), source.indexOf('std::string trim'));
  const expected = new Map();
  const entry = /\{"([a-z0-9_]+)",\s*DirectiveArg::\w+,\s*DirectiveContext::(\w+),/g;
  let match;
  while ((match = entry.exec(table)) !== null) {
    const name = match[1];
    const context = match[2].toLowerCase();
    if (!expected.has(name)) expected.set(name, new Set());
    expected.get(name).add(context);
  }
  assert.ok(expected.size >= 20, 'parsed the compiler directive table');

  const actual = new Map(DIRECTIVES.map((d) => [d.name, new Set(d.context)]));
  assert.deepStrictEqual([...actual.keys()].sort(), [...expected.keys()].sort());

  for (const [name, contexts] of expected) {
    assert.deepStrictEqual([...actual.get(name)].sort(), [...contexts].sort(),
      `context for #${name}`);
  }
});

test('rule ids match compiler/src/lint.cpp when present', (t) => {
  const source = readIfPresent(path.join(toolchain, 'lint.cpp'));
  if (!source) {
    t.skip('compiler sources are not next to the extension');
    return;
  }
  const start = source.indexOf('lint_rules()');
  const table = source.slice(start, source.indexOf('return rules;', start));
  const expected = new Set();
  const entry = /\{"([a-z][a-z0-9/-]+)",\s*"/g;
  let match;
  while ((match = entry.exec(table)) !== null) expected.add(match[1]);
  assert.ok(expected.size >= 20, 'parsed the compiler rule table');
  assert.deepStrictEqual(Object.keys(RULES).sort(), [...expected].sort());
});

test('every table directive is accepted by the in-memory linter', () => {
  for (const item of DIRECTIVES) {
    const source = `#${item.name}${item.args ? ' 0' : ''}\nint main() { return 0; }\n`;
    const unknown = lintText(source, { languageId: 'cwiz' })
      .filter((d) => d.rule === 'preproc/unknown-directive');
    assert.deepStrictEqual(unknown, [], `#${item.name} is reported as unknown`);
  }
});
