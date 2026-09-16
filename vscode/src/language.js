'use strict';

const path = require('node:path');

// A small lexical scan for structural editor features, not a second parser or
// typechecker. Offsets stay in UTF-16, as required by TextDocument.positionAt.
function tokens(source) {
    const result = [];
    let i = 0;
    while (i < source.length) {
        if (/\s/.test(source[i])) { i++; continue; }
        if (source.startsWith('//', i)) {
            const end = source.indexOf('\n', i);
            i = end < 0 ? source.length : end;
            continue;
        }
        if (source.startsWith('/*', i)) {
            i += 2;
            let depth = 1;
            while (i < source.length && depth) {
                if (source.startsWith('/*', i)) { depth++; i += 2; }
                else if (source.startsWith('*/', i)) { depth--; i += 2; }
                else i++;
            }
            continue;
        }
        const start = i;
        if (source[i] === '"' || source[i] === "'") {
            const quote = source[i++];
            while (i < source.length && source[i] !== '\n') {
                if (source[i] === '\\') { i = Math.min(i + 2, source.length); continue; }
                if (source[i++] === quote) break;
            }
            result.push({ text: source.slice(start, i), start, end: i, literal: true });
            continue;
        }
        if (/[A-Za-z_]/.test(source[i])) {
            while (++i < source.length && /[A-Za-z0-9_]/.test(source[i])) { /* identifier */ }
        } else if (source.startsWith('::', i)) i += 2;
        else i++;
        result.push({ text: source.slice(start, i), start, end: i });
    }
    return result;
}

function qualifiedName(ts, start) {
    let i = start;
    if (ts[i]?.text === '::') i++;
    if (!/^[A-Za-z_]\w*$/.test(ts[i]?.text || '')) return undefined;
    i++;
    while (ts[i]?.text === '::' && /^[A-Za-z_]\w*$/.test(ts[i + 1]?.text || '')) i += 2;
    return { name: ts.slice(start, i).map(t => t.text).join(''), start: ts[start].start, end: ts[i - 1].end, next: i };
}

function declarations(source) {
    const ts = tokens(source);
    const result = [];
    const kinds = { fn: 'Function', thread_fn: 'Function', struct: 'Struct', enum: 'Enum', type: 'TypeParameter', let: 'Constant', const: 'Constant', var: 'Variable', namespace: 'Namespace' };
    let depth = 0;
    for (let i = 0; i < ts.length; i++) {
        const token = ts[i];
        if (token.text === '{') depth++;
        if (token.text === '}') depth--;
        if (depth !== 0 || !kinds[token.text]) continue;
        const name = qualifiedName(ts, i + 1);
        if (!name) continue;
        const body = ['fn', 'thread_fn', 'struct', 'enum'].includes(token.text);
        let nesting = 0;
        let end = name.next;
        for (; end < ts.length; end++) {
            const text = ts[end].text;
            if (text === '{' || text === '[' || text === '(') nesting++;
            if (text === '}' || text === ']' || text === ')') {
                nesting--;
                if (body && text === '}' && nesting === 0) break;
            }
            if (text === ';' && nesting === 0) break;
        }
        result.push({ ...name, kind: kinds[token.text], detail: token.text,
            rangeStart: token.start, rangeEnd: ts[end]?.end || source.length });
        // For multi-bindings, show each top-level binding in the outline.
        if (['let', 'var', 'const'].includes(token.text)) {
            let n = name.next;
            while (ts[n]?.text === ',') {
                const sibling = qualifiedName(ts, n + 1);
                if (!sibling) break;
                result.push({ ...sibling, kind: kinds[token.text], detail: token.text,
                    rangeStart: token.start, rangeEnd: ts[end]?.end || source.length });
                n = sibling.next;
            }
        }
        i = end;
    }
    return result;
}

function imports(source) {
    const ts = tokens(source);
    const result = [];
    let depth = 0;
    for (let i = 0; i < ts.length; i++) {
        if (ts[i].text === '{') depth++;
        if (ts[i].text === '}') depth--;
        if (depth !== 0 || ts[i].text !== 'import') continue;
        let n = i + 1;
        const start = ts[n]?.start;
        const relative = ts[n]?.text === '.';
        if (relative) n++;
        const parts = [];
        while (/^[A-Za-z_]\w*$/.test(ts[n]?.text || '')) {
            parts.push(ts[n++].text);
            if (ts[n]?.text !== '.') break;
            n++;
        }
        if (parts.length && ts[n]?.text === ';') {
            result.push({ relative, parts, start, end: ts[n - 1].end });
        }
    }
    return result;
}

function importCandidates(item, currentFile, entryFile, stdlibDirs) {
    const roots = item.relative ? [path.dirname(currentFile)] : [path.dirname(entryFile), ...stdlibDirs];
    return roots.map(root => path.join(root, ...item.parts) + '.goose');
}

module.exports = { tokens, declarations, imports, importCandidates };
