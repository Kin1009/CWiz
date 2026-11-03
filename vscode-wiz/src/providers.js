'use strict';

// Completion and hover for Wiz (.wz) and CWiz (.cwiz).
//
//   * directives  - only in CWiz, where '#' starts a preprocessor line
//                   (in Wiz '#' starts a comment);
//   * built-ins   - both languages, from docs/API.md;
//   * types       - rom / rom8 / __normalized__ (docs/DIRECTIVES.md).
//
// The data lives in ./directives.js so the tables stay reviewable and testable.

const vscode = require('vscode');
const {
  DIRECTIVES,
  TYPES,
  BUILTINS,
  findDirective,
  findType,
  findBuiltin,
} = require('./directives');

function directiveLabel(item) {
  return `#${item.name}`;
}

function directiveInsertText(item) {
  return item.args ? `#${item.name} ` : `#${item.name}`;
}

function directiveMarkdown(item) {
  const lines = [`**#${item.name}** — \`${item.args || 'no argument'}\``, ''];
  lines.push(item.doc);
  lines.push('');
  lines.push(`Target: ${item.target}`);
  lines.push(`Context: ${item.context.map(contextName).join(' / ')}`);
  return lines.join('\n');
}

function contextName(name) {
  return name === 'rop' ? 'ROP backend' : 'CWiz preprocessor';
}

function typeMarkdown(item) {
  return `**${item.name}**\n\n${item.doc}`;
}

function builtinMarkdown(item) {
  return `**${item.signature}**\n\n${item.doc}`;
}

// Build `directive  #for` entries that replace the half-typed `#for` token.
function directiveItems(range) {
  return DIRECTIVES.map((item) => {
    const completion = new vscode.CompletionItem(
      directiveLabel(item),
      vscode.CompletionItemKind.Keyword,
    );
    completion.insertText = directiveInsertText(item);
    completion.range = range;
    completion.detail = item.args ? `#${item.name} ${item.args}` : `#${item.name}`;
    completion.documentation = new vscode.MarkdownString(directiveMarkdown(item));
    completion.sortText = `0${item.name}`;
    return completion;
  });
}

function identifierItems(range) {
  const items = [];
  for (const item of BUILTINS) {
    const completion = new vscode.CompletionItem(
      item.name,
      vscode.CompletionItemKind.Function,
    );
    completion.insertText = item.name;
    completion.range = range;
    completion.detail = item.signature;
    completion.documentation = new vscode.MarkdownString(builtinMarkdown(item));
    completion.sortText = `1${item.name}`;
    items.push(completion);
  }
  for (const item of TYPES) {
    const completion = new vscode.CompletionItem(
      item.name,
      vscode.CompletionItemKind.TypeParameter,
    );
    completion.insertText = item.name;
    completion.range = range;
    completion.detail = item.name;
    completion.documentation = new vscode.MarkdownString(item.doc);
    completion.sortText = `2${item.name}`;
    items.push(completion);
  }
  return items;
}

function createCompletionItemProvider(languageId) {
  return {
    provideCompletionItems(document, position) {
      const before = document.lineAt(position.line).text.slice(0, position.character);

      // `#for` / `#f` — replace the whole token, including the '#'.
      if (languageId === 'cwiz') {
        const hashed = before.match(/#[A-Za-z_]*$/);
        if (hashed) {
          const start = position.character - hashed[0].length;
          const range = new vscode.Range(position.line, start, position.line, position.character);
          return new vscode.CompletionList(directiveItems(range), false);
        }
        // A bare first word at the start of a line: offer `#directive` too, so
        // typing `for` in a target header still suggests `#for`.  Nothing is
        // offered for plain indentation.
        const firstWord = before.match(/^[ \t]*([A-Za-z_][A-Za-z0-9_]*)$/);
        if (firstWord) {
          const word = firstWord[1];
          const start = position.character - word.length;
          const range = new vscode.Range(position.line, start, position.line, position.character);
          return new vscode.CompletionList(
            directiveItems(range).concat(identifierItems(range)),
            false,
          );
        }
      }

      // A plain identifier: built-ins and types.
      const word = before.match(/[A-Za-z_][A-Za-z0-9_]*$/);
      if (!word) return undefined;
      const start = position.character - word[0].length;
      const range = new vscode.Range(position.line, start, position.line, position.character);
      return new vscode.CompletionList(identifierItems(range), false);
    },
  };
}

function createHoverProvider(languageId) {
  return {
    provideHover(document, position) {
      const range = document.getWordRangeAtPosition(position, /#?[A-Za-z_][A-Za-z0-9_]*/);
      if (!range) return undefined;
      const word = document.getText(range);

      if (word[0] === '#') {
        if (languageId !== 'cwiz') return undefined;
        const item = findDirective(word.slice(1));
        return item ? new vscode.Hover(new vscode.MarkdownString(directiveMarkdown(item)), range) : undefined;
      }

      const type = findType(word);
      if (type) {
        return new vscode.Hover(new vscode.MarkdownString(typeMarkdown(type)), range);
      }

      const builtin = findBuiltin(word);
      if (builtin) {
        return new vscode.Hover(new vscode.MarkdownString(builtinMarkdown(builtin)), range);
      }
      return undefined;
    },
  };
}

module.exports = { createCompletionItemProvider, createHoverProvider };
