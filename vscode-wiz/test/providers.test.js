'use strict';

// Completion / hover checks.  The extension host provides `vscode`, so the
// module is stubbed with the few classes the providers touch.

const test = require('node:test');
const assert = require('node:assert');
const Module = require('node:module');

class MarkdownString {
  constructor(value) { this.value = value; }
}

class Range {
  // Same four-argument shape as vscode.Range(startLine, startCharacter, endLine, endCharacter).
  constructor(startLine, startCharacter, endLine, endCharacter) {
    this.startLine = startLine;
    this.startCharacter = startCharacter;
    this.endLine = endLine;
    this.endCharacter = endCharacter;
    this.line = startLine;
    this.start = startCharacter;
    this.end = endCharacter;
  }
}

class CompletionItem {
  constructor(label, kind) {
    this.label = label;
    this.kind = kind;
    this.insertText = label;
    this.detail = '';
    this.documentation = null;
    this.range = null;
  }
}

class CompletionList {
  constructor(items, isIncomplete) {
    this.items = items;
    this.isIncomplete = isIncomplete;
  }
}

class Hover {
  constructor(contents, range) {
    this.contents = contents;
    this.range = range;
  }
}

const fakeVscode = {
  MarkdownString,
  Range,
  CompletionItem,
  CompletionList,
  Hover,
  CompletionItemKind: { Keyword: 13, Function: 2, TypeParameter: 24 },
};

const originalLoad = Module._load;
Module._load = function load(request, ...rest) {
  if (request === 'vscode') return fakeVscode;
  return originalLoad.call(this, request, ...rest);
};

let providers;
try {
  providers = require('../src/providers');
} finally {
  Module._load = originalLoad;
}

const { createCompletionItemProvider, createHoverProvider } = providers;

function document(text, languageId = 'cwiz') {
  const lines = text.split('\n');
  return {
    languageId,
    getText(range) {
      if (!range) return text;
      return (lines[range.line] || '').slice(range.start, range.end);
    },
    lineAt(line) { return { text: lines[line] }; },
    getWordRangeAtPosition(position, pattern) {
      const line = lines[position.line] || '';
      const re = new RegExp(pattern.source, 'g');
      let match;
      while ((match = re.exec(line)) !== null) {
        if (match.index <= position.character &&
            position.character <= match.index + match[0].length) {
          return { line: position.line, start: match.index, end: match.index + match[0].length };
        }
      }
      return null;
    },
  };
}

const completion = createCompletionItemProvider('cwiz');
const wizCompletion = createCompletionItemProvider('wiz');
const hover = createHoverProvider('cwiz');
const wizHover = createHoverProvider('wiz');

function labels(list) {
  return list.items.map((item) => item.label);
}

function hoverText(result) {
  return result ? result.contents.value : '';
}

test('directive completion replaces the half-typed #token', () => {
  const doc = document('#fo');
  const list = completion.provideCompletionItems(doc, { line: 0, character: 3 });
  assert.ok(labels(list).includes('#for'));
  assert.ok(labels(list).includes('#cwapp'));
  const item = list.items.find((entry) => entry.label === '#for');
  assert.strictEqual(item.insertText, '#for ');
  assert.strictEqual(item.range.start, 0);
  assert.strictEqual(item.range.end, 3);
  assert.strictEqual(item.kind, fakeVscode.CompletionItemKind.Keyword);
  assert.match(item.documentation.value, /Target: 580 \/ 880/);
});

test('typing # alone lists every directive', () => {
  const list = completion.provideCompletionItems(document('#'), { line: 0, character: 1 });
  const all = labels(list);
  for (const name of ['#for', '#cwapp', '#slot', '#org', '#rop', '#launcher', '#include',
    '#define', '#if', '#elifdef', '#elifndef', '#embed', '#binary', '#pad', '#nopad']) {
    assert.ok(all.includes(name), `${name} is missing`);
  }
});

test('a bare first word also offers directives', () => {
  const list = completion.provideCompletionItems(document('fo', 'cwiz'), { line: 0, character: 2 });
  assert.ok(labels(list).includes('#for'));
  const item = list.items.find((entry) => entry.label === '#for');
  assert.strictEqual(item.range.start, 0);
  assert.strictEqual(item.range.end, 2);
});

test('built-ins and types complete in both languages', () => {
  for (const provider of [completion, wizCompletion]) {
    const list = provider.provideCompletionItems(document('    re', 'wiz'), { line: 0, character: 6 });
    const all = labels(list);
    assert.ok(all.includes('render'));
    assert.ok(all.includes('renderStatus'));
    assert.ok(all.includes('rom8'));
    const render = list.items.find((entry) => entry.label === 'render');
    assert.strictEqual(render.insertText, 'render');
    assert.strictEqual(render.detail, 'render()');
    assert.strictEqual(render.kind, fakeVscode.CompletionItemKind.Function);
  }
});

test('no completion outside an identifier', () => {
  assert.strictEqual(completion.provideCompletionItems(document('    '), { line: 0, character: 4 }),
    undefined);
});

test('directive hover describes the canonical spelling', () => {
  const doc = document('#cwapp\n');
  const text = hoverText(hover.provideHover(doc, { line: 0, character: 3 }));
  assert.match(text, /\*\*#cwapp\*\*/);
  assert.match(text, /follow `#for 580`/);
  assert.match(text, /Target: 580 \(cw\)/);

  const org = hoverText(hover.provideHover(document('#org 0xE9E0\n'), { line: 0, character: 2 }));
  assert.match(org, /Canonical spelling/);
  assert.match(org, /ROP backend/);
});

test('type hover explains rom / rom8 / __normalized__', () => {
  const rom8 = hoverText(hover.provideHover(document('rom8 char t[2];'), { line: 0, character: 1 }));
  assert.match(rom8, /\*\*rom8\*\*/);
  assert.match(rom8, /Byte storage-class qualifier/);

  const rom = hoverText(hover.provideHover(document('rom int t[2];'), { line: 0, character: 1 }));
  assert.match(rom, /\*\*rom\*\*/);
  assert.match(rom, /16-bit word storage-class qualifier/);

  const normalized = hoverText(hover.provideHover(
    document('__normalized__ rom8 char t[2] = "a";'), { line: 0, character: 4 }));
  assert.match(normalized, /\*\*__normalized__\*\*/);
  assert.match(normalized, /first attribute/);
});

test('builtin hover shows the signature and description', () => {
  const doc = document('if (key_hit(K_AC)) {');
  const text = hoverText(hover.provideHover(doc, { line: 0, character: 6 }));
  assert.match(text, /\*\*key_hit\(k\)\*\*/);
  assert.match(text, /went from released to held/);
});

test('Wiz has no #directives to complete or hover', () => {
  const list = wizCompletion.provideCompletionItems(document('#for', 'wiz'), { line: 0, character: 4 });
  assert.ok(!labels(list).includes('#for'));
  assert.strictEqual(wizHover.provideHover(document('#for 580', 'wiz'), { line: 0, character: 2 }),
    undefined);
});
