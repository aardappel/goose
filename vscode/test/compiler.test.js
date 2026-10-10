'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { compilerPath } = require('../src/config');
const { runCompiler, parseDiagnostics } = require('../src/diagnostics');

const root = path.resolve(__dirname, '../..');
const compiler = process.env.GOOSE_TEST_COMPILER || compilerPath('', root);
const available = fs.existsSync(compiler);
const options = { skip: available ? false : 'Build Goose or set GOOSE_TEST_COMPILER to run compiler integration tests.' };
const config = file => ({ compiler, cwd: root, file: path.resolve(root, file), stdlib: path.join(root, 'stdlib'), timeout: 15000 });

function removeFixture(dir) {
    assert.equal(path.dirname(path.resolve(dir)), path.resolve(os.tmpdir()));
    assert.ok(path.basename(dir).startsWith('goose vscode '));
    fs.rmSync(dir, { recursive: true, force: true });
}

test('real compiler lexer and type errors resolve to the correct source files', options, async () => {
    for (const file of ['test/errors/bad_escape.goose', 'test/errors_tc/let_assign.goose']) {
        const result = await runCompiler(config(file)).promise;
        assert.ok(result.error);
        const diagnostics = parseDiagnostics(result.output, root);
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
        const entry = { ...config(file), module: false };
        assert.match((await runCompiler(entry).promise).output, /exactly one global fn main/);
        const module = { ...entry, module: true };
        const ok = await runCompiler(module).promise;
        assert.equal(ok.error, null, ok.output);
        fs.writeFileSync(file, 'fn helper(x: i64) -> i64 { x + "s" }\n');
        const [diagnostic] = parseDiagnostics((await runCompiler(module).promise).output, root);
        assert.equal(diagnostic.file, file);
        assert.equal(diagnostic.line, 0);
    } finally {
        removeFixture(dir);
    }
});

test('every Goose tooling module checks on its own, so editing it shows no false errors', options, async () => {
    const directory = path.join(root, 'tools/gls');
    for (const name of fs.readdirSync(directory).filter(file => file.endsWith('.goose'))) {
        const result = await runCompiler({ ...config(path.join(directory, name)), module: true }).promise;
        assert.equal(result.error, null, `${name}\n${result.output}`);
    }
});

test('real errors in imported modules point to that module and include caller locations', options, async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'goose vscode imports '));
    try {
        fs.writeFileSync(path.join(dir, 'main.goose'), 'import helper;\nfn main() { helper(); }\n');
        fs.writeFileSync(path.join(dir, 'helper.goose'), 'fn helper() { let x = 1; x = 2; }\n');
        const result = await runCompiler(config(path.join(dir, 'main.goose'))).promise;
        const [diagnostic] = parseDiagnostics(result.output, root);
        assert.equal(diagnostic.file, path.join(dir, 'helper.goose'));
        assert.ok(diagnostic.related.some(frame => frame.file === path.join(dir, 'main.goose')));
    } finally {
        removeFixture(dir);
    }
});
