'use strict';

// A dependency-free analyzer for Wiz (.wz) and CWiz (.cwiz).
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
  'compile': 'warning',
  'readability/unused-variable': 'warning',
  'readability/unused-parameter': 'warning',
  'readability/empty-block': 'note',
  'readability/redundant-boolean': 'note',
  'correctness/shadowed-variable': 'warning',
  'correctness/shadowed-builtin': 'warning',
  'correctness/unreachable-code': 'warning',
  'correctness/assignment-in-condition': 'warning',
  'correctness/self-assignment': 'warning',
  'correctness/division-by-zero': 'warning',
  'correctness/constant-condition': 'note',
  'correctness/out-of-bounds': 'warning',
  'correctness/missing-return': 'warning',
  'preproc/duplicate-target': 'warning',
  'preproc/unknown-target': 'warning',
  'preproc/unknown-directive': 'error',
  'preproc/unbalanced-conditional': 'error',
  'preproc/cwapp-order': 'error',
  'preproc/slot-order': 'error',
  'preproc/slot-range': 'error',
  'preproc/duplicate-slot': 'warning',
  'style/line-length': 'warning',
  'style/trailing-whitespace': 'warning',
  'style/tabs': 'warning',
  'style/todo': 'note',
};

const BASE_TYPES = new Set(['int', 'char', 'void', 'bool', 'rom', 'rom8']);
const QUALIFIERS = new Set([
  'const', 'static', 'volatile', 'register', 'extern', 'inline',
  'signed', 'unsigned', 'short', 'long',
]);
const STATEMENT_KEYWORDS = new Set([
  'if', 'else', 'elif', 'while', 'do', 'for', 'switch', 'case', 'default',
  'return', 'break', 'continue', 'goto', 'def', 'fn', 'class', 'struct',
  'enum', 'union', 'typedef', 'sizeof',
]);
const BUILTINS = new Set([
  'print', 'print_cw', 'text', 'text_cw', 'num', 'num_cw', 'char', 'glyph',
  'char_cw', 'glyph_cw', 'pixel', 'clear', 'clearRender', 'render',
  'renderStatus', 'key', 'key_down', 'key_hit', 'wait_key', 'delay', 'peek',
  'poke', 'sprite', 'hline', 'vline', 'rect', 'fillrect', 'line', 'circle',
  'disc', 'abs', 'min', 'max', 'clamp', 'sign', 'srand', 'rand', 'exit',
  'flip', 'buffered', 'wipe', '_hspan', '_rpeek',
]);
const KNOWN_DIRECTIVES = new Set([
  'if', 'ifdef', 'ifndef', 'elif', 'else', 'endif', 'define', 'undef', 'pragma',
  'error', 'include', 'for', 'cwapp', 'org', 'binary', 'slot', 'rop', 'launcher',
  'pad', 'nopad', '__asm__', '__hex__', 'line',
]);

function isIdentStart(c) {
  return !!c && /[A-Za-z_]/.test(c);
}
function isIdentChar(c) {
  return !!c && /[A-Za-z0-9_]/.test(c);
}
function trim(text) {
  return text.trim();
}
function lower(text) {
  return text.toLowerCase();
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

// Replace comments and literals with spaces, keeping the line structure.
// `comments` keeps the comment characters so TODO markers can be restricted to
// comments, and `directives` flags CWiz preprocessor lines.
function stripNoise(text, kind) {
  const lines = text.split(/\r\n|\r|\n/);
  const code = [];
  const comments = [];
  const directives = [];
  let inBlock = false;

  for (const line of lines) {
    const out = new Array(line.length).fill(' ');
    const cm = new Array(line.length).fill(' ');
    const first = line.search(/[^ \t\r]/);
    const isDirective = kind === 'cwiz' && !inBlock && first >= 0 && line[first] === '#';
    directives.push(isDirective);
    if (isDirective) {
      code.push(out.join(''));
      comments.push(cm.join(''));
      continue;
    }

    let state = inBlock ? 'block' : 'normal'; // normal | block | line | string | char
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      const n = i + 1 < line.length ? line[i + 1] : '';
      if (state === 'line') {
        cm[i] = c;
        continue;
      }
      if (state === 'block') {
        cm[i] = c;
        if (c === '*' && n === '/') {
          cm[i + 1] = '/';
          i++;
          state = 'normal';
        }
        continue;
      }
      if (state === 'string') {
        if (c === '\\') i++;
        else if (c === '"') state = 'normal';
        continue;
      }
      if (state === 'char') {
        if (c === '\\') i++;
        else if (c === "'") state = 'normal';
        continue;
      }
      if (kind === 'cwiz' && c === '/' && n === '/') {
        cm[i] = c;
        cm[i + 1] = n;
        i++;
        state = 'line';
        continue;
      }
      if (kind === 'cwiz' && c === '/' && n === '*') {
        cm[i] = c;
        cm[i + 1] = n;
        i++;
        state = 'block';
        continue;
      }
      if (c === '"') {
        state = 'string';
        continue;
      }
      if (kind === 'cwiz' && c === "'") {
        state = 'char';
        continue;
      }
      // Wiz has no preprocessor: '#' always starts a comment.
      if (kind === 'wiz' && c === '#') {
        cm[i] = c;
        state = 'line';
        continue;
      }
      out[i] = c;
    }
    inBlock = state === 'block';
    code.push(out.join(''));
    comments.push(cm.join(''));
  }
  return { lines, code, comments, directives };
}

function indentOf(line) {
  let n = 0;
  while (n < line.length && (line[n] === ' ' || line[n] === '\t')) n++;
  return n;
}

function countIdentifiers(codeLines) {
  const counts = new Map();
  const re = /[A-Za-z_][A-Za-z0-9_]*/g;
  for (const line of codeLines) {
    let match;
    re.lastIndex = 0;
    while ((match = re.exec(line)) !== null) {
      const name = match[0];
      counts.set(name, (counts.get(name) || 0) + 1);
    }
  }
  return counts;
}

// Return declarations on one line: [{ name, col, arraySize }].
function declarationsOnLine(codeLine, kind) {
  const n = codeLine.length;
  let p = 0;
  while (p < n && /[ \t\r]/.test(codeLine[p])) p++;
  if (p >= n) return [];

  const readIdent = () => {
    if (p >= n || !isIdentStart(codeLine[p])) return '';
    const start = p;
    while (p < n && isIdentChar(codeLine[p])) p++;
    return codeLine.slice(start, p);
  };
  const skipWs = () => {
    while (p < n && /[ \t\r]/.test(codeLine[p])) p++;
  };

  if (kind === 'wiz') {
    const firstAt = p;
    const first = readIdent();
    if (!first) return [];
    if (STATEMENT_KEYWORDS.has(first)) {
      if (first === 'for') {
        skipWs();
        const varAt = p;
        const name = readIdent();
        skipWs();
        if (name && readIdent() === 'in') return [{ name, col: varAt, arraySize: -1 }];
      }
      return [];
    }
    if (first === 'def') return [];
    if (first === 'const' || first === 'rom' || first === 'rom8') {
      skipWs();
      const col = p;
      const name = readIdent();
      return name ? [{ name, col, arraySize: -1 }] : [];
    }
    // NAME = value / NAME[index] = value (but not == or compound assignment).
    let q = p;
    if (q < n && codeLine[q] === '[') {
      let depth = 0;
      while (q < n) {
        if (codeLine[q] === '[') depth++;
        else if (codeLine[q] === ']' && --depth === 0) { q++; break; }
        q++;
      }
      p = q;
      skipWs();
    }
    if (p < n && codeLine[p] === '=' && codeLine[p + 1] !== '=') {
      return [{ name: first, col: firstAt, arraySize: -1 }];
    }
    return [];
  }

  // CWiz: [qualifiers] base-type [base-type] [*] declarator [, declarator]...
  let sawBase = false;
  while (p < n) {
    const wordAt = p;
    const word = readIdent();
    if (!word) break;
    if (QUALIFIERS.has(word)) {
      skipWs();
      continue;
    }
    if (BASE_TYPES.has(word)) {
      sawBase = true;
      const save = p;
      skipWs();
      const nextAt = p;
      const next = readIdent();
      if ((word === 'rom' || word === 'rom8') && (next === 'int' || next === 'char')) {
        p = nextAt + next.length;
        continue;
      }
      p = save;
      break;
    }
    p = wordAt;
    break;
  }
  if (!sawBase) return [];

  const out = [];
  while (p < n) {
    skipWs();
    while (p < n && codeLine[p] === '*') { p++; skipWs(); }
    if (p >= n || codeLine[p] === '(') break;
    const nameAt = p;
    const name = readIdent();
    if (!name) break;
    if (STATEMENT_KEYWORDS.has(name) || BASE_TYPES.has(name)) break;
    skipWs();

    let arraySize = -1;
    if (p < n && codeLine[p] === '[') {
      const open = p++;
      let depth = 1;
      while (p < n && depth > 0) {
        if (codeLine[p] === '[') depth++;
        else if (codeLine[p] === ']') depth--;
        p++;
      }
      const inner = trim(codeLine.slice(open + 1, p - 1));
      if (/^[0-9]+$/.test(inner)) arraySize = Number(inner);
      skipWs();
    }
    // A '(' after the name means this is a function declaration.
    if (p < n && codeLine[p] === '(') return [];
    out.push({ name, col: nameAt, arraySize });

    // Skip an initializer up to the next top-level comma.
    if (p < n && codeLine[p] === '=') {
      p++;
      let depth = 0;
      while (p < n) {
        const c = codeLine[p];
        if (c === '(' || c === '[' || c === '{') depth++;
        else if (c === ')' || c === ']' || c === '}') depth = Math.max(0, depth - 1);
        else if (c === ',' && depth === 0) break;
        p++;
      }
    }
    skipWs();
    if (p < n && codeLine[p] === ',') { p++; continue; }
    break;
  }
  return out;
}

function parseParams(text, line, baseCol) {
  const out = [];
  for (const raw of splitTopLevel(text, ',')) {
    let part = trim(raw);
    if (!part || part === 'void') continue;
    const eq = part.indexOf('=');
    if (eq >= 0) part = trim(part.slice(0, eq));
    const words = part.match(/[A-Za-z_][A-Za-z0-9_]*/g) || [];
    const names = words.filter((w) => !BASE_TYPES.has(w) && !QUALIFIERS.has(w));
    if (!names.length) continue;
    const name = names[names.length - 1];
    out.push({ name, line, col: baseCol + raw.indexOf(name) + 1 });
  }
  return out;
}

// Collect function parameters and, for CWiz, whether a non-void function has a
// return statement.  Signatures may span several lines.
function collectFunctions(lines, code, kind) {
  const out = [];
  const seen = new Set();

  for (let i = 0; i < lines.length; i++) {
    const c = code[i];
    let name = '';
    let ret = 'int';
    let open = -1;
    let col = 0;

    if (kind === 'wiz') {
      const m = c.match(/^\s*def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/);
      if (!m) continue;
      name = m[1];
      ret = 'int';
      open = c.indexOf('(', m.index);
      col = open + 1;
    } else {
      const re = /(?:^|[;{}])\s*(?:(?:const|static|volatile|register|extern|inline|signed|unsigned|short|long)\s+)*(?:rom8?\s+)?(int|char|void|bool)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/g;
      let m;
      while ((m = re.exec(c)) !== null) {
        name = m[2];
        ret = m[1];
        open = m.index + m[0].length - 1;
        col = open + 1;
        break;
      }
      if (!name) continue;
    }

    // Gather the parameter list (it may span lines).
    let sig = '';
    let depth = 0;
    let closeLine = -1;
    let closePos = -1;
    for (let j = i; j < code.length; j++) {
      const lineText = code[j];
      const from = j === i ? open + 1 : 0;
      for (let k = from; k < lineText.length; k++) {
        if (lineText[k] === '(') depth++;
        else if (lineText[k] === ')') {
          if (depth === 0) { closeLine = j; closePos = k; break; }
          depth--;
        }
        if (closeLine < 0) sig += lineText[k];
      }
      if (closeLine >= 0) break;
      sig += ' ';
    }
    if (closeLine < 0) continue;
    const key = `${name}@${i + 1}`;
    if (seen.has(key)) continue;
    seen.add(key);

    const fn = { name, ret, line: i + 1, col, params: parseParams(sig, i + 1, col) };
    if (kind === 'cwiz') {
      // Find the opening brace and its matching close.
      let bodyLine = -1;
      let bodyPos = -1;
      for (let j = closeLine; j < code.length; j++) {
        const from = j === closeLine ? closePos + 1 : 0;
        const at = code[j].indexOf('{', from);
        if (at >= 0) { bodyLine = j; bodyPos = at; break; }
      }
      if (bodyLine >= 0) {
        let brace = 0;
        let started = false;
        fn.hasBody = true;
        for (let j = bodyLine; j < code.length; j++) {
          const lineText = code[j];
          const from = j === bodyLine ? bodyPos : 0;
          for (let k = from; k < lineText.length; k++) {
            if (lineText[k] === '{') { brace++; started = true; }
            else if (lineText[k] === '}') {
              brace--;
              if (started && brace === 0) break;
            }
          }
          if (/\breturn\b/.test(lineText)) fn.hasReturn = true;
          if (started && brace === 0) break;
        }
      }
    }
    out.push(fn);
  }
  return out;
}

function diagnostic(severity, line, col, endCol, rule, message, hint) {
  return {
    severity: severity || RULES[rule] || 'warning',
    line,
    col,
    endCol: Math.max(col + 1, endCol || col + 1),
    rule,
    message,
    hint: hint || '',
  };
}

function assignmentInCondition(code) {
  const kw = /\b(if|while)\b/g;
  let m;
  while ((m = kw.exec(code)) !== null) {
    const open = code.indexOf('(', kw.lastIndex);
    if (open < 0) return -1;
    let depth = 0;
    let close = -1;
    for (let i = open; i < code.length; i++) {
      if (code[i] === '(') depth++;
      else if (code[i] === ')' && --depth === 0) { close = i; break; }
    }
    if (close < 0) return -1;
    for (let i = open + 1; i < close; i++) {
      if (code[i] !== '=') continue;
      const prev = code[i - 1] || '';
      const next = code[i + 1] || '';
      if (next === '=' || '=!<>+-*/%&|^'.includes(prev)) continue;
      return i;
    }
  }
  return -1;
}

function lintText(text, options = {}) {
  const kind = options.languageId === 'wiz' || options.languageId === 'cwiz'
    ? options.languageId
    : (options.fileName && options.fileName.endsWith('.cwiz') ? 'cwiz' : 'wiz');
  const lineLimit = Math.max(20, Number(options.lineLength) || 100);
  const { lines, code, comments, directives } = stripNoise(text, kind);
  const diagnostics = [];

  // ------------------------------------------------------------ preprocessor
  let lastFor = '';
  let inCwapp = false;
  let slotSeen = false;
  const seenFor = new Set();
  const conditionalStack = [];
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const t = trim(raw);
    if (!t) continue;

    if (kind === 'cwiz' && directives[i]) {
      const m = t.match(/^#\s*([A-Za-z_][A-Za-z0-9_]*)?/);
      const name = (m && m[1]) || '';
      if (name && !KNOWN_DIRECTIVES.has(name)) {
        const hint = didYouMean(name, [...KNOWN_DIRECTIVES]);
        diagnostics.push(diagnostic('error', i + 1, 1, raw.length + 1,
          'preproc/unknown-directive', `unknown directive '#${name}'`, hint));
      }
      if (name === 'if' || name === 'ifdef' || name === 'ifndef') conditionalStack.push(i + 1);
      else if (name === 'elif' || name === 'else') {
        if (!conditionalStack.length) {
          diagnostics.push(diagnostic('error', i + 1, 1, raw.length + 1,
            'preproc/unbalanced-conditional', `#${name} without a matching #if`,
            'add #if/#ifdef before it, or remove this directive'));
        }
      } else if (name === 'endif') {
        if (!conditionalStack.length) {
          diagnostics.push(diagnostic('error', i + 1, 1, raw.length + 1,
            'preproc/unbalanced-conditional', '#endif without a matching #if',
            'remove it or add the missing #if'));
        } else conditionalStack.pop();
      }
    }

    // Target headers: `#for 580` (CWiz) or `for 580` (Wiz).
    let header = null;
    if (kind === 'cwiz' && t.startsWith('#for')) header = t.slice(4);
    else if (kind === 'wiz' && t.startsWith('for ') && !t.includes(' in ')) header = t.slice(4);
    if (header !== null) {
      const first = trim(header).split(/[\s,#]+/)[0] || '';
      if (first) {
        if (seenFor.has(first)) {
          diagnostics.push(diagnostic('warning', i + 1, 1, raw.length + 1,
            'preproc/duplicate-target', `#for ${first} is already listed earlier in this file`,
            'remove the duplicate section header'));
        }
        seenFor.add(first);
        lastFor = first;
        inCwapp = false;
        slotSeen = false;
        if (!['580', '880', 'cwapp', 'all'].includes(first)) {
          diagnostics.push(diagnostic('warning', i + 1, 1, raw.length + 1,
            'preproc/unknown-target', `unknown target '#for ${first}' (use 580 or 880)`,
            'write 580 (fx-580VN X) or 880 (fx-880BTG)'));
        }
      }
    }
    if (t === '----') {
      lastFor = '';
      inCwapp = false;
      slotSeen = false;
    }

    const cwapp = kind === 'cwiz' && /^#cwapp(?:\s|$)/.test(t);
    if (cwapp && lastFor !== '580') {
      diagnostics.push(diagnostic('error', i + 1, 1, raw.length + 1, 'preproc/cwapp-order',
        '#cwapp needs #for 580 before it' +
        (lastFor ? ` (the current section is #for ${lastFor})` : ' (no target section is active)'),
        "add '#for 580' before '#cwapp'"));
    }
    if (cwapp && lastFor === '580') {
      inCwapp = true;
      slotSeen = false;
    }
    const slot = kind === 'cwiz' ? t.match(/^#slot(?:[ \t]+(.*))?$/) : null;
    if (slot) {
      if (!inCwapp) {
        diagnostics.push(diagnostic('error', i + 1, 1, raw.length + 1, 'preproc/slot-order',
          '#slot is only available after #cwapp (inside a #for 580 section)',
          "move '#slot n' after a '#cwapp' line"));
      } else if (slotSeen) {
        diagnostics.push(diagnostic('warning', i + 1, 1, raw.length + 1, 'preproc/duplicate-slot',
          'this #cwapp section already has a #slot',
          "keep a single '#slot n' per #cwapp section"));
      } else {
        slotSeen = true;
        const value = (slot[1] || '').trim();
        const n = Number(value);
        if (!/^\d+$/.test(value) || !Number.isInteger(n) || n < 1 || n > 12) {
          diagnostics.push(diagnostic('error', i + 1, 1, raw.length + 1, 'preproc/slot-range',
            '#slot needs a slot number from 1 to 12', "for example '#slot 2'"));
        }
      }
    }
  }
  if (conditionalStack.length) {
    const line = conditionalStack[conditionalStack.length - 1];
    diagnostics.push(diagnostic('error', line, 1, 2, 'preproc/unbalanced-conditional',
      'unterminated #if (missing #endif)',
      "add '#endif' at the end of the conditional block"));
  }

  // ------------------------------------------------------------ declarations
  const counts = countIdentifiers(code);
  const globals = new Map();
  const arraySize = new Map();
  const arrayDeclLines = new Map();
  const declarations = [];
  let braceDepth = 0;
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const lineDepth = braceDepth;
    if (raw.trim() && !directives[i]) {
      const names = declarationsOnLine(code[i], kind);
      const global = kind === 'cwiz' ? lineDepth === 0 : indentOf(raw) === 0;
      const indent = indentOf(raw);
      for (const item of names) {
        declarations.push({
          name: item.name,
          line: i + 1,
          col: item.col,
          indent,
          global,
        });
        if (global && !globals.has(item.name)) globals.set(item.name, i + 1);
        if (item.arraySize >= 0) {
          arraySize.set(item.name, item.arraySize);
          if (!arrayDeclLines.has(item.name)) arrayDeclLines.set(item.name, new Set());
          arrayDeclLines.get(item.name).add(i + 1);
        }
      }
    }
    for (const c of code[i]) {
      if (c === '{') braceDepth++;
      else if (c === '}') braceDepth = Math.max(0, braceDepth - 1);
    }
  }

  // De-duplicate declarations.  Wiz assignments are not declarations after the
  // first global/local with that name.
  const seenPos = new Set();
  const seenWizGlobal = new Set();
  const seenWizLocal = new Set();
  const unique = [];
  for (const decl of declarations) {
    const key = `${decl.name}@${decl.line}@${decl.col}`;
    if (seenPos.has(key)) continue;
    seenPos.add(key);
    if (kind === 'wiz') {
      const seen = decl.global ? seenWizGlobal : seenWizLocal;
      if (seen.has(decl.name)) continue;
      seen.add(decl.name);
    }
    unique.push(decl);
  }

  for (const decl of unique) {
    if (!decl.name || decl.name.startsWith('_')) continue;
    if (!decl.global && globals.has(decl.name) && globals.get(decl.name) !== decl.line) {
      diagnostics.push(diagnostic('warning', decl.line, decl.col + 1,
        decl.col + decl.name.length + 1, 'correctness/shadowed-variable',
        `local '${decl.name}' hides the global declared on line ${globals.get(decl.name)}`,
        `rename the local, or use the 'global ${decl.name}' statement in Wiz`));
    }
    if (BUILTINS.has(decl.name)) {
      diagnostics.push(diagnostic('warning', decl.line, decl.col + 1,
        decl.col + decl.name.length + 1, 'correctness/shadowed-builtin',
        `'${decl.name}' hides the built-in function of the same name`,
        'rename the variable; built-ins are called like functions'));
    }
    if ((counts.get(decl.name) || 0) <= 1) {
      diagnostics.push(diagnostic('warning', decl.line, decl.col + 1,
        decl.col + decl.name.length + 1, 'readability/unused-variable',
        `'${decl.name}' is declared but never read`,
        "remove it, use it, or prefix it with '_' to silence this warning"));
    }
  }

  // ------------------------------------------------------------ functions
  const functions = collectFunctions(lines, code, kind);
  for (const fn of functions) {
    for (const p of fn.params) {
      if (!p.name || p.name.startsWith('_')) continue;
      if ((counts.get(p.name) || 0) <= 1) {
        diagnostics.push(diagnostic('warning', p.line, p.col, p.col + p.name.length,
          'readability/unused-parameter',
          `parameter '${p.name}' of ${fn.name}() is never used`,
          "remove the parameter or prefix it with '_' to silence this warning"));
      }
    }
    if (kind === 'cwiz' && fn.hasBody && fn.ret !== 'void' && !fn.hasReturn) {
      diagnostics.push(diagnostic('warning', fn.line, fn.col, fn.col + fn.name.length,
        'correctness/missing-return',
        `non-void function '${fn.name}' has no return statement`,
        'add a return value, or declare the function as void'));
    }
  }

  // ------------------------------------------------------------ line rules
  let reportedTabs = false;
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    if (raw.length > lineLimit) {
      diagnostics.push(diagnostic('warning', i + 1, lineLimit + 1, raw.length + 1,
        'style/line-length', `line is ${raw.length} columns (limit ${lineLimit})`,
        'wrap the line, or raise wiz.lint.lineLength'));
    }
    const trailing = raw.match(/[ \t]+$/);
    if (trailing) {
      diagnostics.push(diagnostic('warning', i + 1, raw.length - trailing[0].length + 1,
        raw.length + 1, 'style/trailing-whitespace', 'trailing whitespace',
        'delete the spaces at the end of the line'));
    }
    if (!reportedTabs && raw.includes('\t')) {
      reportedTabs = true;
      diagnostics.push(diagnostic('warning', i + 1, raw.indexOf('\t') + 1,
        raw.indexOf('\t') + 2, 'style/tabs',
        'tab character in the source (the examples use spaces)',
        'replace tabs with four spaces'));
    }
    const marker = raw.match(/\b(TODO|FIXME|XXX)\b/);
    if (marker && comments[i] && comments[i][marker.index] !== ' ') {
      diagnostics.push(diagnostic('note', i + 1, marker.index + 1,
        marker.index + marker[0].length + 1, 'style/todo',
        'unfinished marker in a comment',
        'finish the work or remove the marker'));
    }
  }

  // ------------------------------------------------------------ code rules
  for (let i = 0; i < code.length; i++) {
    const c = code[i];
    if (!trim(c)) continue;
    const line = i + 1;

    // x = x;
    {
      const re = /\b([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*\1[ \t]*;/;
      const m = c.match(re);
      if (m) {
        const at = c.indexOf(m[0]);
        if (at === 0 || c[at - 1] !== '+') {
          diagnostics.push(diagnostic('warning', line, c.indexOf(m[1]) + 1,
            c.indexOf(m[1]) + m[1].length + 1, 'correctness/self-assignment',
            `'${m[1]}' is assigned to itself`,
            'remove the statement, or use the value you meant to assign'));
        }
      }
    }

    if (kind === 'cwiz') {
      const at = assignmentInCondition(c);
      if (at >= 0) {
        diagnostics.push(diagnostic('warning', line, at + 1, at + 2,
          'correctness/assignment-in-condition',
          "assignment used as a condition (did you mean '=='?)",
          "use '==' for comparison; if assignment is intended, put it on its own line"));
      }
    }

    {
      const m = c.match(/[/%][ \t]*0(?![0-9xX])/);
      if (m) {
        const at = c.indexOf(m[0]);
        diagnostics.push(diagnostic('warning', line, at + 1, at + 2,
          'correctness/division-by-zero', 'division by zero in a constant expression',
          'check the divisor; guard the operation when it can be zero'));
      }
    }

    {
      const m = c.match(/\b(if|while)[ \t]*\([ \t]*(0|false)[ \t]*\)/);
      if (m) {
        const at = c.indexOf(m[0]);
        diagnostics.push(diagnostic('note', line, at + 1, at + m[0].length + 1,
          'correctness/constant-condition', 'the condition is always false',
          'remove the block, or replace the constant with the intended test'));
      }
    }

    {
      const m = c.match(/(==|!=)[ \t]*(true|false)\b/);
      if (m) {
        const at = c.indexOf(m[0]);
        diagnostics.push(diagnostic('note', line, at + 1, at + m[0].length + 1,
          'readability/redundant-boolean', `comparison with '${m[2]}' is redundant`,
          m[1] === '==' ? "use the value directly, or '!' for negation" : 'use the value directly'));
      }
    }

    {
      const m = c.match(/\b(if|while|for)[ \t]*\([^)]*\)[ \t]*\{[ \t]*\}/) ||
        c.match(/\belse[ \t]*\{[ \t]*\}/);
      if (m) {
        const at = c.indexOf(m[0]);
        diagnostics.push(diagnostic('note', line, at + 1, at + m[0].length + 1,
          'readability/empty-block', 'empty control-flow body',
          'add a comment explaining why it is empty, or remove the block'));
      }
    }

    {
      const re = /\b([A-Za-z_][A-Za-z0-9_]*)[ \t]*\[[ \t]*([0-9]+)[ \t]*\]/g;
      let m;
      while ((m = re.exec(c)) !== null) {
        const name = m[1];
        if (!arraySize.has(name)) continue;
        if ((arrayDeclLines.get(name) || new Set()).has(line)) continue;
        const idx = Number(m[2]);
        const size = arraySize.get(name);
        if (idx >= size) {
          diagnostics.push(diagnostic('warning', line, m.index + 1,
            m.index + m[0].length + 1, 'correctness/out-of-bounds',
            `index ${idx} is outside '${name}' (size ${size})`,
            `use an index from 0 to ${size - 1}`));
        }
      }
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
          'unreachable: the previous statement transfers control',
          'remove the statement, or move it before the return/break/continue'));
      }
      break;
    }
  }

  diagnostics.sort((a, b) => a.line - b.line || a.col - b.col || a.rule.localeCompare(b.rule));
  return diagnostics;
}

function didYouMean(word, candidates) {
  if (!word || word.length < 2) return '';
  function distance(a, b) {
    const prev = new Array(b.length + 1);
    const cur = new Array(b.length + 1);
    for (let j = 0; j <= b.length; j++) prev[j] = j;
    for (let i = 1; i <= a.length; i++) {
      cur[0] = i;
      for (let j = 1; j <= b.length; j++) {
        const cost = a[i - 1] === b[j - 1] ? 0 : 1;
        cur[j] = Math.min(prev[j - 1] + cost, prev[j] + 1, cur[j - 1] + 1);
      }
      for (let j = 0; j <= b.length; j++) prev[j] = cur[j];
    }
    return prev[b.length];
  }
  const limit = word.length <= 3 ? 1 : (word.length <= 6 ? 2 : 3);
  let best = '';
  let bestD = limit + 1;
  for (const c of candidates) {
    const d = distance(word, c);
    if (d < bestD) { best = c; bestD = d; }
  }
  return bestD <= limit ? `did you mean '${best}'?` : '';
}

module.exports = { RULES, lintText, stripNoise };
