'use strict';

// Integration checks for src/extension.js: the diagnostic plumbing (rule code,
// severity, hint) and the wizlint arguments.  `vscode` and `child_process` are
// stubbed, so this runs without an editor.

const test = require('node:test');
const assert = require('node:assert');
const Module = require('node:module');

class Range {
  constructor(startLine, startCharacter, endLine, endCharacter) {
    this.startLine = startLine;
    this.startCharacter = startCharacter;
    this.endLine = endLine;
    this.endCharacter = endCharacter;
  }
}

class Diagnostic {
  constructor(range, message, severity) {
    this.range = range;
    this.message = message;
    this.severity = severity;
    this.source = '';
    this.code = undefined;
    this.relatedInformation = undefined;
  }
}

class DiagnosticRelatedInformation {
  constructor(location, message) {
    this.location = location;
    this.message = message;
  }
}

class Location {
  constructor(uri, range) {
    this.uri = uri;
    this.range = range;
  }
}

class MarkdownString {
  constructor(value) { this.value = value; }
}

class CompletionItem {}

class Uri {
  constructor(fsPath) { this.fsPath = fsPath; this.scheme = 'file'; }
  toString() { return `file://${this.fsPath}`; }
}

const published = [];
let commands = new Map();
let settings = {};
let execCalls = [];

const fakeVscode = {
  Range,
  Diagnostic,
  DiagnosticRelatedInformation,
  Location,
  MarkdownString,
  CompletionItem,
  CompletionList: class { constructor(items) { this.items = items; } },
  Hover: class { constructor(contents, range) { this.contents = contents; this.range = range; } },
  Uri,
  DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
  CompletionItemKind: { Keyword: 13, Function: 2, TypeParameter: 24 },
  languages: {
    createDiagnosticCollection() {
      return {
        set(uri, diagnostics) { published.push({ uri, diagnostics }); },
        clear() { published.length = 0; },
        delete() {},
        get(uri) {
          const hit = [...published].reverse().find((entry) => entry.uri === uri);
          return hit ? hit.diagnostics : [];
        },
        dispose() {},
      };
    },
    registerCompletionItemProvider() { return { dispose() {} }; },
    registerHoverProvider() { return { dispose() {} }; },
  },
  commands: {
    registerCommand(name, callback) {
      commands.set(name, callback);
      return { dispose() {} };
    },
  },
  window: {
    activeTextEditor: null,
    showInformationMessage() {},
    showWarningMessage() {},
    showQuickPick() { return Promise.resolve(undefined); },
    createOutputChannel() {
      return { append() {}, appendLine() {}, show() {}, dispose() {} };
    },
  },
  workspace: {
    textDocuments: [],
    getConfiguration() {
      return { get: (key) => settings[key] };
    },
    onDidOpenTextDocument() { return { dispose() {} }; },
    onDidSaveTextDocument() { return { dispose() {} }; },
    onDidChangeTextDocument() { return { dispose() {} }; },
    onDidCloseTextDocument() { return { dispose() {} }; },
    onDidChangeConfiguration() { return { dispose() {} }; },
  },
};

function fakeExecFile(file, args, options, callback) {
  execCalls.push({ file, args, options });
  callback(null, JSON.stringify(settings.__compilerItems || []));
}

const realChildProcess = require('node:child_process');
const stubbedChildProcess = { ...realChildProcess, execFile: fakeExecFile };

function makeDocument(text, languageId = 'cwiz') {
  const uri = new Uri('/work/sample.cwiz');
  return {
    languageId,
    uri,
    fileName: uri.fsPath,
    isUntitled: false,
    isDirty: false,
    getText: () => text,
  };
}

let extension;
const originalLoad = Module._load;
Module._load = function load(request, ...rest) {
  if (request === 'vscode') return fakeVscode;
  if (request === 'child_process') return stubbedChildProcess;
  return originalLoad.call(this, request, ...rest);
};
try {
  extension = require('../src/extension');
} finally {
  Module._load = originalLoad;
}

const context = { subscriptions: [] };

function runLint(doc) {
  fakeVscode.window.activeTextEditor = { document: doc };
  commands.get('wiz.lint')();
  fakeVscode.window.activeTextEditor = null;
}

function activateWith(overrides) {
  published.length = 0;
  execCalls = [];
  commands = new Map();
  settings = {
    'lint.enable': true,
    'lint.onType': true,
    'lint.onSave': true,
    'lint.showHints': true,
    'lint.lineLength': 120,
    'lint.useCompiler': true,
    'lint.compilerPath': 'wizlint',
    'lint.target': 'auto',
    ...overrides,
  };
  extension.activate(context);
}

test('the compiler code is kept and shown on the diagnostic', () => {
  const doc = makeDocument('int main() { return nope; }\n');
  activateWith({
    __compilerItems: [{
      file: doc.uri.fsPath,
      line: 1,
      col: 20,
      endCol: 24,
      severity: 'error',
      rule: 'compile',
      message: "use of undeclared identifier 'nope'",
      hint: '',
      code: 'sema.undefined-identifier',
    }],
  });
  runLint(doc);

  const diagnostics = published[published.length - 1].diagnostics;
  const compiler = diagnostics.find((d) => d.code === 'sema.undefined-identifier');
  assert.ok(compiler, 'the compiler error keeps its code');
  assert.strictEqual(compiler.severity, fakeVscode.DiagnosticSeverity.Error);
  assert.strictEqual(compiler.source, 'wiz');
  assert.match(compiler.message, /\[compile\]$/);
});

test('a diagnostic without a compiler code falls back to the rule id', () => {
  const doc = makeDocument('int main() {\n    int unused = 1;\n    return 0;\n}\n');
  activateWith({ __compilerItems: [] });
  runLint(doc);

  const diagnostics = published[published.length - 1].diagnostics;
  const unused = diagnostics.find((d) => d.message.includes('unused'));
  assert.ok(unused);
  assert.strictEqual(unused.code, 'readability/unused-variable');
  assert.strictEqual(unused.severity, fakeVscode.DiagnosticSeverity.Warning);
});

test('severities are mapped consistently across both stages', () => {
  const doc = makeDocument('#for 580\nint main() { return 0; }\n');
  activateWith({
    __compilerItems: [
      { file: doc.uri.fsPath, line: 2, col: 1, severity: 'note', rule: 'compile', message: 'a note' },
      { file: doc.uri.fsPath, line: 2, col: 5, severity: 'hint', rule: 'compile', message: 'a hint' },
      { file: doc.uri.fsPath, line: 2, col: 9, severity: 'warning', rule: 'compile', message: 'a warning' },
    ],
  });
  runLint(doc);

  const diagnostics = published[published.length - 1].diagnostics;
  const byMessage = (text) => diagnostics.find((d) => d.message.startsWith(text));
  assert.strictEqual(byMessage('a note').severity, fakeVscode.DiagnosticSeverity.Information);
  assert.strictEqual(byMessage('a hint').severity, fakeVscode.DiagnosticSeverity.Hint);
  assert.strictEqual(byMessage('a warning').severity, fakeVscode.DiagnosticSeverity.Warning);
});

test('wiz.lint.lineLength is forwarded to wizlint --max-line-length', () => {
  const doc = makeDocument('int main() { return 0; }\n');
  activateWith({ 'lint.lineLength': 120 });
  runLint(doc);

  assert.strictEqual(execCalls.length, 1);
  const args = execCalls[0].args;
  assert.deepStrictEqual(args.slice(0, 4), ['--json', '--max-line-length', '120', doc.uri.fsPath]);

  // Out-of-range values are clamped to what wizlint accepts (20..10000).
  activateWith({ 'lint.lineLength': 5 });
  runLint(doc);
  assert.deepStrictEqual(execCalls[0].args.slice(0, 4),
    ['--json', '--max-line-length', '20', doc.uri.fsPath]);

  activateWith({ 'lint.lineLength': 50000 });
  runLint(doc);
  assert.deepStrictEqual(execCalls[0].args.slice(0, 4),
    ['--json', '--max-line-length', '10000', doc.uri.fsPath]);
});

test('an explicit target keeps its position before the line length', () => {
  const doc = makeDocument('int main() { return 0; }\n');
  activateWith({ 'lint.target': '880' });
  runLint(doc);
  assert.deepStrictEqual(execCalls[0].args.slice(0, 6),
    ['--json', '--target', '880', '--max-line-length', '120', doc.uri.fsPath]);
});

test('hints are attached only when wiz.lint.showHints is on', () => {
  const doc = makeDocument('#for cw\nint main() { return 0; }\n');
  activateWith({ __compilerItems: [], 'lint.showHints': true, 'lint.target': '880' });
  runLint(doc);
  let diagnostics = published[published.length - 1].diagnostics;
  const withHint = diagnostics.find((d) => d.code === 'preproc/noncanonical-directive');
  assert.ok(withHint);
  assert.ok(Array.isArray(withHint.relatedInformation));
  assert.match(withHint.relatedInformation[0].message, /^help: use '#for 580'/);

  activateWith({ __compilerItems: [], 'lint.showHints': false });
  runLint(doc);
  diagnostics = published[published.length - 1].diagnostics;
  const withoutHint = diagnostics.find((d) => d.code === 'preproc/noncanonical-directive');
  assert.ok(withoutHint);
  assert.strictEqual(withoutHint.relatedInformation, undefined);
});
