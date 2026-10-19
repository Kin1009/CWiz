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
const { lintText } = require('./linter');

const SEVERITY = {
  error: vscode.DiagnosticSeverity.Error,
  warning: vscode.DiagnosticSeverity.Warning,
  note: vscode.DiagnosticSeverity.Information,
};

function isWizDocument(doc) {
  return !!doc && (doc.languageId === 'wiz' || doc.languageId === 'cwiz');
}

function config() {
  return vscode.workspace.getConfiguration('wiz');
}

function toVscodeDiagnostics(items) {
  return items.map((item) => {
    const line = Math.max(0, item.line - 1);
    const col = Math.max(0, item.col - 1);
    const endCol = Math.max(col + 1, (item.endCol || item.col + 1) - 1);
    const range = new vscode.Range(line, col, line, endCol);
    const diagnostic = new vscode.Diagnostic(
      range,
      `${item.message} [${item.rule}]`,
      SEVERITY[item.severity] || vscode.DiagnosticSeverity.Warning,
    );
    diagnostic.source = 'wiz';
    diagnostic.code = item.rule;
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
    lineLength: config().get('lint.lineLength'),
  });

  const publish = (doc, items) => {
    collection.set(doc.uri, toVscodeDiagnostics(items));
  };

  const runCompiler = (doc, jsItems, generation) => {
    if (!config().get('lint.useCompiler') || compilerMissing || doc.isUntitled) return;
    const key = doc.uri.toString();
    if (generations.get(key) !== generation) return;

    const compiler = config().get('lint.compilerPath') || 'wizlint';
    const target = config().get('lint.target') || '580';
    execFile(
      compiler,
      ['--json', '--target', target, doc.uri.fsPath],
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
            endCol: (Number(item.col) || 1) + 1,
            rule: item.rule || 'compile',
            message: item.message || 'compiler diagnostic',
          }));
        const seen = new Set(compilerItems.map(diagnosticKey));
        publish(doc, compilerItems.concat(jsItems.filter((item) => !seen.has(diagnosticKey(item)))));
      },
    );
  };

  const lint = (doc, withCompiler) => {
    if (!isWizDocument(doc)) return;
    const key = doc.uri.toString();
    const generation = (generations.get(key) || 0) + 1;
    generations.set(key, generation);
    const items = jsDiagnostics(doc);
    publish(doc, items);
    if (withCompiler) runCompiler(doc, items, generation);
  };

  const schedule = (doc) => {
    if (!isWizDocument(doc) || !config().get('lint.onType')) return;
    const key = doc.uri.toString();
    if (timers.has(key)) clearTimeout(timers.get(key));
    timers.set(key, setTimeout(() => {
      timers.delete(key);
      lint(doc, false);
    }, 350));
  };

  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((doc) => lint(doc, true)),
    vscode.workspace.onDidSaveTextDocument((doc) => {
      if (config().get('lint.onSave')) lint(doc, true);
    }),
    vscode.workspace.onDidChangeTextDocument((event) => schedule(event.document)),
    vscode.workspace.onDidCloseTextDocument((doc) => {
      timers.delete(doc.uri.toString());
      generations.delete(doc.uri.toString());
      collection.delete(doc.uri);
    }),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (!event.affectsConfiguration('wiz')) return;
      compilerMissing = false;
      for (const doc of vscode.workspace.textDocuments) lint(doc, true);
    }),
    vscode.commands.registerCommand('wiz.lint', () => {
      const doc = vscode.window.activeTextEditor && vscode.window.activeTextEditor.document;
      if (!isWizDocument(doc)) {
        vscode.window.showInformationMessage('Wiz: open a .wz or .cwiz file first.');
        return;
      }
      lint(doc, true);
      const count = collection.get(doc.uri) ? collection.get(doc.uri).length : 0;
      vscode.window.showInformationMessage(`Wiz: ${count} diagnostic(s).`);
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
      const out = doc.fileName.replace(/\.(wz|cwiz)$/i, '') + '.bin';
      channel.show(true);
      channel.appendLine(`$ ${compiler} ${doc.fileName} -o ${out}`);
      execFile(compiler, [doc.fileName, '-o', out], {
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
