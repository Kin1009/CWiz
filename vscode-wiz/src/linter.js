'use strict';

// A small, dependency-free analyzer for Wiz (.wz) and CWiz (.cwiz).
//
// The command-line `wizlint` remains the authority for compiler diagnostics.
// This module covers the editor-only rules so a file can be checked without a
// toolchain installed.  Rule ids are identical to compiler/src/lint.cpp.
//
// The scanner is deliberately conservative:
//   * comments and string/char literals are blanked, preserving columns;
//   * block comments may span lines;
//   * function signatures and their parameter lists never become variables;
//   * declarations may contain arrays, pointers and several declarators;
//   * scope is brace-based for CWiz and indentation-based for Wiz.

const RULES = {
  'readability/unused-variable': 'warning',
  'correctness/shadowed-variable': 'warning',
  'correctness/unreachable-code': 'warning',
  'preproc/duplicate-target': 'warning',
  'preproc/unknown-target': 'warning',
  'preproc/cwapp-order': 'error',
  'preproc/slot-order': 'error',
  'preproc/slot-range': 'error',
  'preproc/duplicate-slot': 'warning',
  'style/line-length': 'warning',
  'style/trailing-whitespace': 'warning',
  'style/tabs': 'warning',
  'style/todo': 'note',
};

const BASE_TYPES = new Set([
  'int', 'char', 'bool', 'byte', 'short', 'long', 'unsigned', 'signed',
  'rom8', 'rom16', 'int8', 'int16', 'arr', 'void',
]);

const QUALIFIERS = new Set([
  'const', 'static', 'volatile', 'register', 'extern', 'unsigned', 'signed',
  'short', 'long',
]);

const KEYWORDS = new Set([
  'if', 'else', 'elif', 'while', 'for', 'do', 'switch', 'case', 'default',
  'return', 'break', 'continue', 'goto', 'def', 'fn', 'class', 'struct',
  'enum', 'union', 'typedef', 'sizeof', 'true', 'false', 'null', 'nullptr',
  'void',
]);

function splitLines(text) {
  return text.split(/\r\n|\r|\n/);
}

// Replace comments and literals with spaces, keeping the line structure.
function stripNoise(text) {
  const lines = splitLines(text);
  const code = [];
  let state = 'normal'; // normal | block | line | string | char
  for (const line of lines) {
    let out = '';
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      const n = i + 1 < line.length ? line[i + 1] : '';
      if (state === 'line') {
        out += ' ';
        continue;
      }
      if (state === 'block') {
        if (c === '*' && n === '/') {
          out += '  ';
          i++;
          state = 'normal';
        } else {
          out += ' ';
        }
        continue;
      }
      if (state === 'string') {
        out += ' ';
        if (c === '\\') {
          out += ' ';
          i++;
        } else if (c === '"') {
          state = 'normal';
        }
        continue;
      }
      if (state === 'char') {
        out += ' ';
        if (c === '\\') {
          out += ' ';
          i++;
        } else if (c === "'") {
          state = 'normal';
        }
        continue;
      }
      if (c === '/' && n === '/') {
        out += '  ';
        i++;
        state = 'line';
      } else if (c === '/' && n === '*') {
        out += '  ';
        i++;
        state = 'block';
      } else if (c === '"') {
        out += ' ';
        state = 'string';
      } else if (c === "'") {
        out += ' ';
        state = 'char';
      } else {
        out += c;
      }
    }
    if (state === 'line') state = 'normal';
    // An unterminated normal string/char ends at the newline.  A trailing
    // backslash is a line continuation, but the language does not use
    // multi-line literals in practice.
    if (state === 'string' || state === 'char') state = 'normal';
    code.push(out);
  }
  return { lines, code };
}

function indentOf(line) {
  let n = 0;
  while (n < line.length && (line[n] === ' ' || line[n] === '\t')) n++;
  return n;
}

function trim(text) {
  return text.trim();
}

function splitTopLevel(text, separator) {
  const parts = [];
  let depth = 0;
  let start = 0;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (c === '(' || c === '[' || c === '{') depth++;
    else if (c === ')' || c === ']' || c === '}') depth = Math.max(0, depth - 1);
    else if (c === separator && depth === 0) {
      parts.push(text.slice(start, i));
      start = i + 1;
    }
  }
  parts.push(text.slice(start));
  return parts;
}

function countIdentifiers(codeLines) {
  const counts = new Map();
  const re = /[A-Za-z_][A-Za-z0-9_]*/g;
  for (const line of codeLines) {
    let match;
    while ((match = re.exec(line)) !== null) {
      const name = match[0];
      counts.set(name, (counts.get(name) || 0) + 1);
    }
  }
  return counts;
}

// Return the variable names declared on a line.  `kind` is "cwiz" or "wiz".
// Function definitions and prototypes intentionally return an empty list.
function declarationsOnLine(codeLine, kind) {
  const s = trim(codeLine);
  if (!s || s.startsWith('#') || s.startsWith('//')) return [];
  if (/^(if|for|while|switch|case|default|else|return|break|continue|goto|do)\b/.test(s)) return [];

  if (kind === 'wiz') {
    let m = s.match(/^const\s+([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?\s*=(?!=)/);
    if (m) return [{ name: m[1], index: m.index || 0 }];
    m = s.match(/^([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?\s*=(?!=)/);
    if (m) return [{ name: m[2], index: 0 }];
    m = s.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?\s*=(?!=)/);
    if (m && !KEYWORDS.has(m[1]) && !BASE_TYPES.has(m[1])) {
      return [{ name: m[1], index: 0 }];
    }
    return [];
  }

  // CWiz: [qualifiers] type [*] declarator [, declarator]...
  let rest = s;
  let sawBase = false;
  while (rest) {
    const word = rest.match(/^([A-Za-z_][A-Za-z0-9_]*)\b/);
    if (!word) break;
    const w = word[1];
    const after = rest.slice(word[0].length).trimStart();
    const next = after.match(/^([A-Za-z_][A-Za-z0-9_]*)\b/);
    if (QUALIFIERS.has(w) && next && (QUALIFIERS.has(next[1]) || BASE_TYPES.has(next[1]))) {
      rest = after;
      continue;
    }
    if (BASE_TYPES.has(w)) {
      sawBase = true;
      rest = after;
      break;
    }
    break;
  }
  if (!sawBase) return [];
  while (rest.startsWith('*')) rest = rest.slice(1).trimStart();

  // A function pointer or a parenthesised declarator is not a variable.
  if (!rest || rest[0] === '(') return [];

  // Function definition/prototype: `name(` or `name (`.
  const first = rest.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*\(/);
  if (first) return [];

  const out = [];
  for (const part of splitTopLevel(rest, ',')) {
    const text = part.trim();
    if (!text) continue;
    const m = text.match(/^([A-Za-z_][A-Za-z0-9_]*)/);
    if (!m) continue;
    const name = m[1];
    if (KEYWORDS.has(name) || BASE_TYPES.has(name)) continue;
    // `int f(int a)` would have been caught above, but a function pointer in a
    // later declarator (`int a, (*f)(int)`) has no leading name; skip it.
    out.push({ name, index: 0 });
  }
  return out;
}

function diagnostic(severity, line, col, endCol, rule, message) {
  return {
    severity: severity || RULES[rule] || 'warning',
    line,
    col,
    endCol: Math.max(col + 1, endCol || col + 1),
    rule,
    message,
  };
}

function lintText(text, options = {}) {
  const kind = options.languageId === 'wiz' || options.languageId === 'cwiz'
    ? options.languageId
    : (options.fileName && options.fileName.endsWith('.cwiz') ? 'cwiz' : 'wiz');
  const lineLimit = Math.max(20, Number(options.lineLength) || 100);
  const { lines, code } = stripNoise(text);
  const diagnostics = [];

  // ------------------------------------------------------------ preprocessor
  let lastFor = '';
  let inCwapp = false;
  let slotSeen = false;
  const seenFor = new Set();
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const t = trim(raw);
    const m = t.match(/^#for(?:\s+(.*))?$/);
    if (m) {
      const first = (m[1] || '').trim().split(/[\s,]+/)[0];
      if (first) {
        if (seenFor.has(first)) {
          diagnostics.push(diagnostic('warning', i + 1, 1, raw.length + 1,
            'preproc/duplicate-target', `#for ${first} is already listed earlier in this file`));
        }
        seenFor.add(first);
        lastFor = first;
        inCwapp = false;
        slotSeen = false;
        if (first !== '580' && first !== '880') {
          diagnostics.push(diagnostic('warning', i + 1, 1, raw.length + 1,
            'preproc/unknown-target', `unknown target '#for ${first}' (use 580 or 880)`));
        }
      }
    }
    if (t === '----') {
      lastFor = '';
      inCwapp = false;
      slotSeen = false;
    }
    const cwapp = /^#cwapp(?:\s|$)/.test(t);
    if (cwapp && lastFor !== '580') {
      diagnostics.push(diagnostic('error', i + 1, 1, raw.length + 1, 'preproc/cwapp-order',
        '#cwapp needs #for 580 before it' +
        (lastFor ? ` (the current section is #for ${lastFor})` : '')));
    }
    if (cwapp && lastFor === '580') {
      inCwapp = true;
      slotSeen = false;
    }
    const slot = t.match(/^#slot(?:[ \t]+(.*))?$/);
    if (slot) {
      if (!inCwapp) {
        diagnostics.push(diagnostic('error', i + 1, 1, raw.length + 1, 'preproc/slot-order',
          '#slot is only available after #cwapp (inside a #for 580 section)'));
      } else if (slotSeen) {
        diagnostics.push(diagnostic('warning', i + 1, 1, raw.length + 1, 'preproc/duplicate-slot',
          'this #cwapp section already has a #slot'));
      } else {
        slotSeen = true;
        const value = (slot[1] || '').trim();
        const n = Number(value);
        if (!/^\d+$/.test(value) || !Number.isInteger(n) || n < 1 || n > 12) {
          diagnostics.push(diagnostic('error', i + 1, 1, raw.length + 1, 'preproc/slot-range',
            '#slot needs a slot number from 1 to 12'));
        }
      }
    }
  }

  // ------------------------------------------------------------ declarations
  const counts = countIdentifiers(code);
  const globals = new Map();
  const declarations = [];
  let braceDepth = 0;
  let parenDepth = 0;
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const c = code[i];
    const lineDepth = braceDepth;
    const lineParenDepth = parenDepth;

    // Count braces/parentheses only in code, so strings and comments do not
    // affect scope.
    for (const ch of c) {
      if (ch === '{') braceDepth++;
      else if (ch === '}') braceDepth = Math.max(0, braceDepth - 1);
      else if (ch === '(') parenDepth++;
      else if (ch === ')') parenDepth = Math.max(0, parenDepth - 1);
    }

    if (lineParenDepth > 0) continue;
    if (!raw.trim() || trim(c).startsWith('#')) continue;
    const names = declarationsOnLine(c, kind);
    if (!names.length) continue;

    const indent = indentOf(raw);
    const global = kind === 'cwiz' ? lineDepth === 0 : indent === 0;
    for (const item of names) {
      const decl = { name: item.name, line: i + 1, indent, depth: lineDepth, global };
      declarations.push(decl);
      if (global && !globals.has(item.name)) globals.set(item.name, i + 1);
    }
  }

  for (const decl of declarations) {
    if (!decl.name || decl.name.startsWith('_')) continue;
    if (!decl.global && globals.has(decl.name)) {
      diagnostics.push(diagnostic('warning', decl.line, decl.indent + 1,
        decl.indent + decl.name.length + 1, 'correctness/shadowed-variable',
        `local '${decl.name}' hides the global declared on line ${globals.get(decl.name)}`));
    }
    if ((counts.get(decl.name) || 0) <= 1) {
      diagnostics.push(diagnostic('warning', decl.line, decl.indent + 1,
        decl.indent + decl.name.length + 1, 'readability/unused-variable',
        `'${decl.name}' is declared but never read`));
    }
  }

  // ------------------------------------------------------------ line rules
  let reportedTabs = false;
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    if (raw.length > lineLimit) {
      diagnostics.push(diagnostic('warning', i + 1, lineLimit + 1, raw.length + 1,
        'style/line-length', `line is ${raw.length} columns (limit ${lineLimit})`));
    }
    const trailing = raw.match(/[ \t]+$/);
    if (trailing) {
      diagnostics.push(diagnostic('warning', i + 1, raw.length - trailing[0].length + 1,
        raw.length + 1, 'style/trailing-whitespace', 'trailing whitespace'));
    }
    if (!reportedTabs && raw.includes('\t')) {
      reportedTabs = true;
      diagnostics.push(diagnostic('warning', i + 1, raw.indexOf('\t') + 1,
        raw.indexOf('\t') + 2, 'style/tabs',
        'tab character in the source (the examples use spaces)'));
    }
    const comment = raw.match(/(?:\/\/|\/\*|^\s*\*)\s*.*\b(TODO|FIXME|XXX)\b/);
    if (comment) {
      diagnostics.push(diagnostic('note', i + 1, 1, raw.length + 1, 'style/todo',
        'unfinished marker in a comment'));
    }
  }

  // ------------------------------------------------------------ unreachable
  for (let i = 0; i < code.length; i++) {
    const t = trim(code[i]);
    if (!/^(return|break|continue|goto)\b/.test(t)) continue;
    const jumpIndent = code[i].length - code[i].trimStart().length;
    for (let j = i + 1; j < code.length; j++) {
      const n = trim(code[j]);
      if (!n) continue;
      if (n === '}' || /^(case\b|default\s*:|#|else\b|elif\b)/.test(n)) break;
      const indent = code[j].length - code[j].trimStart().length;
      if (indent >= jumpIndent) {
        diagnostics.push(diagnostic('warning', j + 1, indent + 1, indent + 2,
          'correctness/unreachable-code',
          'unreachable: the previous statement transfers control'));
      }
      break;
    }
  }

  diagnostics.sort((a, b) => a.line - b.line || a.col - b.col || a.rule.localeCompare(b.rule));
  return diagnostics;
}

module.exports = { RULES, lintText, stripNoise };
