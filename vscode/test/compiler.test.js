'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { compilerPath } = require('../src/config');
const { runCompiler, parseDiagnostics, parseJsonDiagnostics } = require('../src/diagnostics');

const root = path.resolve(__dirname, '../..');
const compiler = process.env.GOOSE_TEST_COMPILER || compilerPath('', root);
const available = fs.existsSync(compiler);
const options = { skip: available ? false : 'Build Goose or set GOOSE_TEST_COMPILER to run compiler integration tests.' };
const config = file => ({ compiler, cwd: root, file: path.resolve(root, file), stdlib: path.join(root, 'stdlib'), timeout: 15000 });
// A check's diagnostics as the extension reads them: the data of a compiler
// with --diagnostics json, or the text of one without.
const diagnosticsOf = result => result.diagnostics ? parseJsonDiagnostics(result.diagnostics, root) : parseDiagnostics(result.output, root);

function removeFixture(dir) {
    assert.equal(path.dirname(path.resolve(dir)), path.resolve(os.tmpdir()));
    assert.ok(path.basename(dir).startsWith('goose vscode '));
    fs.rmSync(dir, { recursive: true, force: true });
}

test('real compiler lexer and type errors resolve to the correct source files', options, async () => {
    for (const file of ['test/errors/bad_escape.goose', 'test/errors_tc/let_assign.goose']) {
        const result = await runCompiler(config(file)).promise;
        assert.ok(result.error);
        const diagnostics = await diagnosticsOf(result);
        assert.equal(diagnostics.length, 1);
        assert.equal(diagnostics[0].file, path.resolve(root, file));
        if (file.includes('bad_escape')) assert.ok(diagnostics[0].column >= 0);
        else assert.ok(diagnostics[0].related.length > 0);
    }
});

test('real check succeeds without C output or executing the program, including spaced paths', options, async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'goose vscode '));
    try {
        const file = path.join(dir, 'main file.goose');
        fs.writeFileSync(file, 'fn main() { print("PROGRAM_RAN"); }\n');
        const result = await runCompiler(config(file)).promise;
        assert.equal(result.error, null, result.output);
        assert.ok(!result.output.includes('PROGRAM_RAN'));
        assert.deepEqual(fs.readdirSync(dir), ['main file.goose']);
    } finally {
        removeFixture(dir);
    }
});

test('real check of a module without fn main() succeeds, and still reports its errors', options, async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'goose vscode module '));
    try {
        const file = path.join(dir, 'helper.goose');
        fs.writeFileSync(file, 'fn helper(x: i64) -> i64 { x + 1 }\n');
        const module = config(file);
        const ok = await runCompiler(module).promise;
        assert.equal(ok.error, null, ok.output);
        fs.writeFileSync(file, 'fn helper(x: i64) -> i64 { x + "s" }\n');
        const [diagnostic] = await diagnosticsOf(await runCompiler(module).promise);
        assert.equal(diagnostic.file, file);
        assert.equal(diagnostic.line, 0);
    } finally {
        removeFixture(dir);
    }
});

test('real errors in imported modules point to that module and include caller locations', options, async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'goose vscode imports '));
    try {
        fs.writeFileSync(path.join(dir, 'main.goose'), 'import helper;\nfn main() { helper(); }\n');
        fs.writeFileSync(path.join(dir, 'helper.goose'), 'fn helper() { let x = 1; x = 2; }\n');
        const result = await runCompiler(config(path.join(dir, 'main.goose'))).promise;
        const [diagnostic] = await diagnosticsOf(result);
        assert.equal(diagnostic.file, path.join(dir, 'helper.goose'));
        assert.ok(diagnostic.related.some(frame => frame.file === path.join(dir, 'main.goose')));
    } finally {
        removeFixture(dir);
    }
});

test('real compiler with --diagnostics json reports source ranges and the calls of instantiations', options, async t => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'goose vscode json '));
    try {
        const main = path.join(dir, 'main.goose');
        const helper = path.join(dir, 'helper.goose');
        const call = 'fn main() { print("é😀"); ';
        fs.writeFileSync(main, `import helper;\n${call}helper(1); }\n`);
        const body = 'fn helper<T>(a: T) -> i64 { ';
        fs.writeFileSync(helper, `${body}a.nope }\n`);
        const result = await runCompiler(config(main)).promise;
        if (!result.diagnostics) {
            t.skip('This compiler has no --diagnostics json; its text output is tested above.');
            return;
        }
        assert.ok(result.error);
        assert.deepEqual(result.args.slice(0, 3), ['check', '--diagnostics', 'json']);
        assert.ok(result.output.includes('helper.goose:1: error: '), result.output);
        const [item] = await parseJsonDiagnostics(result.diagnostics, root);
        assert.equal(item.file, helper);
        assert.deepEqual([item.line, item.column, item.endLine, item.endColumn], [0, body.length, 0, body.length + 'a.nope'.length]);
        const site = item.related.find(frame => frame.file === main);
        assert.deepEqual([site.line, site.column], [1, call.length], 'the call is at its UTF-16 column');
        assert.match(site.message, /^Instantiated fn helper/);
    } finally {
        removeFixture(dir);
    }
});
