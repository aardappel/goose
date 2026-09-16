'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const textmate = require('vscode-textmate');
const oniguruma = require('vscode-oniguruma');

const grammarPath = path.resolve(__dirname, '../syntaxes/goose.tmLanguage.json');
const grammarPromise = (async () => {
    const wasm = fs.readFileSync(require.resolve('vscode-oniguruma/release/onig.wasm'));
    await oniguruma.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));
    const registry = new textmate.Registry({
        onigLib: Promise.resolve({ createOnigScanner: patterns => new oniguruma.OnigScanner(patterns), createOnigString: value => new oniguruma.OnigString(value) }),
        loadGrammar: async () => textmate.parseRawGrammar(fs.readFileSync(grammarPath, 'utf8'), grammarPath)
    });
    return registry.loadGrammar('source.goose');
})();

async function tokenize(source) {
    const grammar = await grammarPromise;
    let state = textmate.INITIAL;
    return source.split('\n').map(line => {
        const result = grammar.tokenizeLine(line, state);
        state = result.ruleStack;
        return result.tokens.map(token => ({ text: line.slice(token.startIndex, token.endIndex), scopes: token.scopes }));
    });
}
const has = (tokens, text, scope) => tokens.some(token => token.text === text && token.scopes.some(item => item.startsWith(scope)));

test('TextMate highlights nested comments and recovers after closing them', async () => {
    const lines = await tokenize('/* outer\n /* inner */ still outer\n*/ fn main() {}');
    assert.ok(lines[1].every(token => token.scopes.includes('comment.block.goose')));
    assert.ok(has(lines[2], 'main', 'entity.name.function'));
});

test('numeric ranges, hex floats and string escapes have correct scopes', async () => {
    const [numbers, string] = await tokenize('1..2 0x1.8p3 0x1p-2 2.5e-3 18446744073709551615\n"ok\\n\\x41 bad\\q"');
    assert.ok(has(numbers, '1', 'constant.numeric.integer'));
    assert.ok(has(numbers, '..', 'keyword.operator'));
    for (const value of ['0x1.8p3', '0x1p-2', '2.5e-3']) assert.ok(has(numbers, value, 'constant.numeric.float'), value);
    assert.ok(has(string, '\\n', 'constant.character.escape'));
    assert.ok(has(string, '\\x41', 'constant.character.escape'));
    assert.ok(has(string, '\\q', 'invalid.illegal.escape'));
});

test('every compiler keyword is recognized without treating from as reserved', async () => {
    const lexer = fs.readFileSync(path.resolve(__dirname, '../../src/lexer.h'), 'utf8');
    const table = lexer.split('#define TOKENS_KEYWORDS')[1].split('enum TType')[0];
    const keywords = [...table.matchAll(/F\(\w+,\s*"(\w+)"\)/g)].map(match => match[1]);
    for (const word of keywords) {
        const [line] = await tokenize(word);
        assert.ok(line[0].scopes.length > 1, word);
    }
    const [line] = await tokenize('from');
    assert.deepEqual(line[0].scopes, ['source.goose']);
});

test('all builtin calls and namespace declarations highlight', async () => {
    const builtins = fs.readFileSync(path.resolve(__dirname, '../../src/builtins.h'), 'utf8');
    const names = [...builtins.matchAll(/F\(B_\w+,\s*"(\w+)"/g)].map(match => match[1]).filter(name => !['len', 'cap'].includes(name));
    for (const name of names) {
        const [line] = await tokenize(`${name}()`);
        assert.ok(has(line, name, 'support.function'), name);
    }
    const [line] = await tokenize('thread_fn image::worker(n: i64) {}');
    assert.ok(has(line, 'image::worker', 'entity.name.function'));
});

test('unterminated strings stop at the newline', async () => {
    const [, line] = await tokenize('"unterminated\nfn next() {}');
    assert.ok(has(line, 'next', 'entity.name.function'));
});
