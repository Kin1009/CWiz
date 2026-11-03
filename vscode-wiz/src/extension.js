// Wiz / CWiz for VS Code.
//
// The editor linter has two stages:
//   1. src/linter.js checks the document in memory (works without a compiler),
//   2. the optional `wizlint --json` pass adds the compiler's own diagnostics.
//
// Compiler diagnostics are only used for saved documents, so the squiggles
// always match the bytes on disk.  Typing uses the fast in-memory stage.
'use strict';

const vscode = require('vscode');
const path = require('path');
const { execFile } = require('child_process');
const { lintText, clampLineLength } = require('./linter');
const { createCompletionItemProvider, createHoverProvider } = require('./providers');

// One severity mapping for the in-memory linter and for `wizlint --json`, so a
// rule keeps the same weight in both stages.  `wizlint` emits error/warning/note;
// numbers are accepted for a JSON shape that already carries the enum value.
function severityOf(value) {
  if (typeof value === 'number' && value >= 0 && value <= 3) return value;
  switch (String(value || '').toLowerCase()) {
    case 'error': return vscode.DiagnosticSeverity.Error;
    case 'note':
    case 'info':
    case 'information': return vscode.DiagnosticSeverity.Information;
    case 'hint': return vscode.DiagnosticSeverity.Hint;
    case 'warning':
    default: return vscode.DiagnosticSeverity.Warning;
  }
}

function isWizDocument(doc) {
  return !!doc && (doc.languageId === 'wiz' || doc.languageId === 'cwiz');
}

function config() {
  return vscode.workspace.getConfiguration('wiz');
}

function toVscodeDiagnostics(doc, items) {
  const showHints = config().get('lint.showHints');
  return items.map((item) => {
    const line = Math.max(0, Number(item.line) - 1 || 0);
    const col = Math.max(0, Number(item.col) - 1 || 0);
    const rawEnd = Number(item.endCol) || Number(item.col) + 1;
    const endCol = Math.max(col + 1, rawEnd - 1);
    const range = new vscode.Range(line, col, line, endCol);
    const rule = item.rule || 'compile';
    const diagnostic = new vscode.Diagnostic(
      range,
      `${item.message} [${rule}]`,
      severityOf(item.severity),
    );
    diagnostic.source = 'wiz';
    // The Problems entry and the tooltip show the stable id: the compiler's
    // error category (`sema.undefined-identifier`, `syntax.expected-token`, …)
    // when it has one, otherwise the lint rule id.
    diagnostic.code = item.code || rule;
    if (item.hint && showHints) {
      diagnostic.relatedInformation = [
        new vscode.DiagnosticRelatedInformation(
          new vscode.Location(doc.uri, range),
          `help: ${item.hint}`,
        ),
      ];
    }
    return diagnostic;
  });
}

function diagnosticKey(item) {
  return `${item.line}:${item.col}:${item.rule}:${item.message}`;
}

// ---------------------------------------------------------------- activation
function activate(context) {
  const collection = vscode.languages.createDiagnosticCollection('wiz');
  context.subscriptions.push(collection);

  const timers = new Map();
  const generations = new Map();
  let compilerMissing = false;
  let buildChannel = null;

  const jsDiagnostics = (doc) => lintText(doc.getText(), {
    fileName: doc.fileName,
    languageId: doc.languageId,
    lineLength: clampLineLength(config().get('lint.lineLength')),
  });

  const publish = (doc, items) => {
    collection.set(doc.uri, toVscodeDiagnostics(doc, items));
  };

  const runCompiler = (doc, jsItems, generation) => {
    if (!config().get('lint.useCompiler') || compilerMissing || doc.isUntitled || doc.isDirty) return;
    const key = doc.uri.toString();
    if (generations.get(key) !== generation) return;

    const compiler = config().get('lint.compilerPath') || 'wizlint';
    const target = config().get('lint.target') || 'auto';
    const args = ['--json'];
    if (target && target !== 'auto') args.push('--target', target);
    // Keep both stages on the same limit: the in-memory linter clamps to the
    // range `wizlint --max-line-length` accepts (20..10000).
    args.push('--max-line-length', String(clampLineLength(config().get('lint.lineLength'))));
    args.push(doc.uri.fsPath);

    execFile(
      compiler,
      args,
      { timeout: 5000, maxBuffer: 4 * 1024 * 1024 },
      (error, stdout) => {
        if (generations.get(key) !== generation) return;
        if (error && error.code === 'ENOENT') {
          // No wizlint in PATH: keep the in-memory linter, silently.  Do not
          // retry on every keystroke.
          compilerMissing = true;
          return;
        }

        let parsed;
        try {
          parsed = JSON.parse((stdout || '').trim() || '[]');
        } catch {
          return; // A crashed/old wizlint must not erase the editor result.
        }
        if (!Array.isArray(parsed)) return;

        const compilerItems = parsed
          .filter((item) => item && item.file === doc.uri.fsPath)
          .map((item) => ({
            severity: item.severity || 'warning',
            line: Number(item.line) || 0,
            col: Number(item.col) || 1,
            endCol: Number(item.endCol) || (Number(item.col) || 1) + 1,
            rule: item.rule || 'compile',
            message: item.message || 'compiler diagnostic',
            hint: item.hint || '',
            code: item.code || '',
          }));

        // A compiler error on a line already explains the same preprocessor
        // problem; do not show the in-memory duplicate below it.
        const compilerErrorLines = new Set(
          compilerItems.filter((item) => item.rule === 'compile').map((item) => item.line),
        );
        const seen = new Set(compilerItems.map(diagnosticKey));
        const jsKept = jsItems.filter((item) => {
          if (seen.has(diagnosticKey(item))) return false;
          if (compilerErrorLines.has(item.line) && item.rule.startsWith('preproc/')) return false;
          return true;
        });
        publish(doc, compilerItems.concat(jsKept));
      },
    );
  };

  const lint = (doc, withCompiler) => {
    if (!isWizDocument(doc) || !config().get('lint.enable')) return;
    const key = doc.uri.toString();
    const generation = (generations.get(key) || 0) + 1;
    generations.set(key, generation);
    const items = jsDiagnostics(doc);
    publish(doc, items);
    if (withCompiler) runCompiler(doc, items, generation);
  };

  const clearAll = () => {
    collection.clear();
    for (const timer of timers.values()) clearTimeout(timer);
    timers.clear();
  };

  const schedule = (doc) => {
    if (!config().get('lint.enable') || !isWizDocument(doc) || !config().get('lint.onType')) return;
    const key = doc.uri.toString();
    if (timers.has(key)) clearTimeout(timers.get(key));
    timers.set(key, setTimeout(() => {
      timers.delete(key);
      lint(doc, false);
    }, 350));
  };

  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider('cwiz', createCompletionItemProvider('cwiz'), '#'),
    vscode.languages.registerCompletionItemProvider('wiz', createCompletionItemProvider('wiz')),
    vscode.languages.registerHoverProvider({ language: 'cwiz' }, createHoverProvider('cwiz')),
    vscode.languages.registerHoverProvider({ language: 'wiz' }, createHoverProvider('wiz')),
    vscode.workspace.onDidOpenTextDocument((doc) => lint(doc, true)),
    vscode.workspace.onDidSaveTextDocument((doc) => {
      if (config().get('lint.onSave')) lint(doc, true);
    }),
    vscode.workspace.onDidChangeTextDocument((event) => schedule(event.document)),
    vscode.workspace.onDidCloseTextDocument((doc) => {
      if (timers.has(doc.uri.toString())) clearTimeout(timers.get(doc.uri.toString()));
      timers.delete(doc.uri.toString());
      generations.delete(doc.uri.toString());
      collection.delete(doc.uri);
    }),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (!event.affectsConfiguration('wiz')) return;
      compilerMissing = false;
      if (!config().get('lint.enable')) {
        clearAll();
        return;
      }
      for (const doc of vscode.workspace.textDocuments) lint(doc, true);
    }),
    vscode.commands.registerCommand('wiz.lint', () => {
      const doc = vscode.window.activeTextEditor && vscode.window.activeTextEditor.document;
      if (!isWizDocument(doc)) {
        vscode.window.showInformationMessage('Wiz: open a .wz or .cwiz file first.');
        return;
      }
      if (!config().get('lint.enable')) {
        vscode.window.showInformationMessage('Wiz: linting is disabled (wiz.lint.enable).');
        return;
      }
      lint(doc, true);
      const count = collection.get(doc.uri) ? collection.get(doc.uri).length : 0;
      vscode.window.showInformationMessage(`Wiz: ${count} diagnostic(s).`);
    }),
    vscode.commands.registerCommand('wiz.lint.rules', async () => {
      const { RULES } = require('./linter');
      const rules = Object.keys(RULES).sort();
      const picked = await vscode.window.showQuickPick(
        rules.map((rule) => ({ label: rule, description: RULES[rule] })),
        { placeHolder: 'Wiz / CWiz lint rules' },
      );
      if (picked) vscode.window.showInformationMessage(`${picked.label} (${picked.description})`);
    }),
    vscode.commands.registerCommand('wiz.build', () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || !isWizDocument(editor.document)) return;
      const doc = editor.document;
      if (doc.isUntitled) {
        vscode.window.showWarningMessage('Wiz: save the file before building.');
        return;
      }
      if (!buildChannel) buildChannel = vscode.window.createOutputChannel('Wiz build');
      const channel = buildChannel;
      const compiler = config().get('compilerPath') || 'wizc';
      const target = config().get('lint.target') || 'auto';
      const out = doc.fileName.replace(/\.(wz|cwiz)$/i, '') + '.bin';
      const args = [doc.fileName, '-o', out];
      if (target && target !== 'auto') args.push('--target', target);
      channel.show(true);
      channel.appendLine(`$ ${compiler} ${args.join(' ')}`);
      execFile(compiler, args, {
        cwd: path.dirname(doc.fileName),
        timeout: 120000,
        maxBuffer: 8 * 1024 * 1024,
      }, (error, stdout, stderr) => {
        if (stdout) channel.append(stdout);
        if (stderr) channel.append(stderr);
        if (error) {
          channel.appendLine(`build failed (${error.code === undefined ? error.message : error.code})`);
        } else {
          channel.appendLine(`wrote ${out}`);
          lint(doc, true);
        }
      });
    }),
  );

  // Lint documents that were already open when the extension started.
  for (const doc of vscode.workspace.textDocuments) lint(doc, true);
}

function deactivate() {}

module.exports = { activate, deactivate };
