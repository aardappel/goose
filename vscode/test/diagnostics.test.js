'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const path = require('node:path');
const { parseDiagnostics, CheckQueue } = require('../src/diagnostics');

test('lexer carets, warnings, explicit columns and related instantiations', () => {
    const cwd = path.resolve('workspace');
    const result = parseDiagnostics(`module.goose:3: error: expected expression\r\n\tlet x = ;\r\n\t        ^\r\n  in fn f(i64) instantiated from main.goose:9\r\nmain.goose:11: warning: unused value\r\nmain.goose:12:4: error: explicit column`, cwd);
    assert.equal(result.length, 3);
    assert.deepEqual(result[0], {
        file: path.join(cwd, 'module.goose'), line: 2, column: 9, severity: 'error', message: 'expected expression',
        related: [{ file: path.join(cwd, 'main.goose'), line: 8, message: 'Instantiated fn f(i64)' }]
    });
    assert.equal(result[1].column, undefined);
    assert.equal(result[1].severity, 'warning');
    assert.equal(result[2].column, 3);
});

test('Windows drive letters, spaces and colons in error messages are preserved', () => {
    const filename = 'C:\\source dir\\module.goose';
    const result = parseDiagnostics(`${filename}:17: error: expected type: i64\n  in fn nested() instantiated from ${filename}:2`, process.cwd());
    assert.equal(result[0].file, path.resolve(filename));
    assert.equal(result[0].line, 16);
    assert.equal(result[0].message, 'expected type: i64');
    assert.equal(result[0].related[0].line, 1);
});

test('UTF-8 byte carets become UTF-16 editor columns', () => {
    const prefix = '\tprint("é😀"); ';
    const source = prefix + 'bad;';
    const caret = '\t' + ' '.repeat(Buffer.byteLength(prefix) - 1) + '^';
    const [item] = parseDiagnostics(`test.goose:1: error: bad\n${source}\n${caret}`, process.cwd());
    assert.equal(item.column, prefix.length);
});

test('unlocated compiler failures do not become fabricated parsed errors', () => {
    assert.deepEqual(parseDiagnostics('cannot open file: missing.goose\ntypechecked ok: 2 specialization(s)', process.cwd()), []);
});

function deferredRunner() {
    const calls = [];
    return {
        calls,
        run(config) {
            let resolve;
            const promise = new Promise(done => { resolve = done; });
            const call = { config, resolve, cancelled: false };
            calls.push(call);
            return { promise, cancel() { call.cancelled = true; } };
        }
    };
}
const tick = () => new Promise(resolve => setTimeout(resolve, 10));

test('checks discard late results after an edit, and cancel superseded checks', async () => {
    const runner = deferredRunner();
    const queue = new CheckQueue(config => runner.run(config), () => {});
    const config = { file: 'main.goose', cwd: '/project' };
    const first = queue.schedule(config, 0);
    await tick();
    queue.invalidate(config.cwd);
    assert.equal(runner.calls[0].cancelled, true);
    assert.equal(await first, false);
    runner.calls[0].resolve({ output: 'old error' });
    await tick();
    assert.equal(queue.results.size, 0);
    const second = queue.schedule(config, 0);
    await tick();
    const third = queue.schedule(config, 0);
    await tick();
    runner.calls[1].resolve({ output: 'superseded error' });
    runner.calls[2].resolve({ output: 'new output' });
    assert.equal(await second, false);
    assert.equal(await third, true);
    assert.equal(queue.results.get(config.file).output, 'new output');
    queue.invalidate();
});

test('roots have separate results and workspace invalidation is scoped', async () => {
    const queue = new CheckQueue(config => ({ promise: Promise.resolve({ output: config.file }), cancel() {} }), () => {});
    await queue.schedule({ file: 'a.goose', cwd: '/one' }, 0);
    await queue.schedule({ file: 'b.goose', cwd: '/two' }, 0);
    await queue.schedule({ file: 'c.goose', cwd: '/two' }, 0);
    queue.invalidate('/one');
    assert.deepEqual([...queue.results.keys()], ['b.goose', 'c.goose']);
    queue.invalidate();
});

test('synchronous launch failures are surfaced and pending timers cancel cleanly', async () => {
    const queue = new CheckQueue(() => { throw new Error('launch failure'); }, () => {});
    await queue.schedule({ file: 'a.goose', cwd: '/one' }, 0);
    assert.match(queue.results.get('a.goose').error.message, /launch failure/);
    const pending = queue.schedule({ file: 'a.goose', cwd: '/one' }, 1000);
    queue.invalidate();
    assert.equal(await pending, false);
});
