'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const path = require('node:path');
const fs = require('node:fs');
const { declarations, imports, importCandidates } = require('../src/language');
const { argumentsFor, compilerPath, stdlibDirectories } = require('../src/config');

test('outline ignores comments, strings, function types and local bindings', () => {
    const source = `/* fn hidden() { /* struct Hidden {} */ } */
namespace image;
import std;
type Callback = fn(i64) -> i64;
struct Pair<T> { a: T, b: T }
enum Shape { Circle { r: f64 }, Point }
extern fn c_puts(s: const u8[:]) -> i32;
recursive fn ::fact(n: i64) -> i64 {
    let text = "fn fake() {}";
    fn local() { }
    if n == 0 { 1 } else { n * fact(n - 1) }
}
thread_fn image::worker(n: i64) { qput(n); }
const a, b = 1, 2;
reusable[] var pool: Pair<i64>[>..] = [];
`;
    const result = declarations(source);
    assert.deepEqual(result.map(item => item.name), ['image', 'Callback', 'Pair', 'Shape', 'c_puts', '::fact', 'image::worker', 'a', 'b', 'pool']);
    for (const item of result) {
        assert.equal(source.slice(item.start, item.end), item.name);
        assert.ok(item.rangeStart <= item.start && item.rangeEnd >= item.end);
    }
    assert.ok(source.slice(result[5].rangeStart, result[5].rangeEnd).endsWith('\n}'));
});

test('outline and imports skip """ strings, which may span lines', () => {
    const source = `let shader = """
    struct Hidden {
    fn fake() {}
    import nope;
    """;
import std;
fn main() { print("""one "line" """); }
`;
    assert.deepEqual(declarations(source).map(item => item.name), ['shader', 'main']);
    assert.deepEqual(imports(source).map(item => item.parts), [['std']]);
});

test('outline tolerates incomplete declarations without throwing', () => {
    assert.deepEqual(declarations('fn '), []);
    const source = 'fn unfinished(x: i64) {';
    assert.equal(declarations(source)[0].rangeEnd, source.length);
});

test('imports support whitespace and nested comments, and ignore text in literals', () => {
    const source = `import std;
import .sub /* nested /* comment */ */ . module;
// import nope;
/* import nope; */
fn main() { print("import nope;"); }
import broken.
`;
    const result = imports(source);
    assert.deepEqual(result.map(({ parts, relative }) => ({ parts, relative })), [
        { parts: ['std'], relative: false }, { parts: ['sub', 'module'], relative: true }
    ]);
    assert.equal(source.slice(result[0].start, result[0].end), 'std');
    const root = path.resolve('project');
    assert.deepEqual(importCandidates(result[0], path.join(root, 'sub', 'current.goose'), path.join(root, 'main.goose'), [path.join(root, 'stdlib')]), [path.join(root, 'std.goose'), path.join(root, 'stdlib', 'std.goose')]);
    assert.deepEqual(importCandidates(result[1], path.join(root, 'lib', 'current.goose'), path.join(root, 'main.goose'), []), [path.join(root, 'lib', 'sub', 'module.goose')]);
});

test('compiler arguments are separate, check cannot accidentally run or emit C', () => {
    const config = { file: path.resolve('with spaces', 'main.goose'), stdlib: path.resolve('std lib'), output: 'output.c', runArguments: ['hello world', '--check', '$(echo unwanted)'] };
    assert.deepEqual(argumentsFor('check', config), ['--stdlib', config.stdlib, '--check', config.file]);
    assert.deepEqual(argumentsFor('run', config), ['--stdlib', config.stdlib, '--jit', config.file, '--', ...config.runArguments]);
    assert.deepEqual(argumentsFor('generateC', config), ['--stdlib', config.stdlib, '-o', 'output.c', config.file]);
    assert.throws(() => argumentsFor('unknown', config));
    assert.equal(compilerPath('custom-goose', process.cwd()), 'custom-goose');
    assert.equal(compilerPath('${workspaceFolder}/a b/goose', process.cwd()), path.join(process.cwd(), 'a b', 'goose'));
});

test('stdlib search preserves compiler precedence', () => {
    const cwd = path.resolve('project');
    const config = { cwd, compiler: path.join(cwd, 'build', 'Release', 'goose'), stdlib: path.join(cwd, 'custom') };
    assert.deepEqual(stdlibDirectories(config, { GOOSE_STDLIB: 'env-lib' }).slice(0, 5), [
        path.join(cwd, 'custom'), path.join(cwd, 'env-lib'),
        path.join(cwd, 'build', 'Release', 'stdlib'), path.join(cwd, 'build', 'stdlib'), path.join(cwd, 'stdlib')
    ]);
});

test('all repository Goose sources can be scanned with valid ranges', () => {
    const root = path.resolve(__dirname, '../..');
    let files = 0;
    function visit(dir) {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const file = path.join(dir, entry.name);
            if (entry.isDirectory()) visit(file);
            else if (file.endsWith('.goose')) {
                const source = fs.readFileSync(file, 'utf8');
                for (const item of [...declarations(source), ...imports(source)]) {
                    assert.ok(item.start >= 0 && item.end <= source.length && item.end > item.start, file);
                }
                files++;
            }
        }
    }
    for (const dir of ['test', 'samples', 'stdlib']) visit(path.join(root, dir));
    assert.ok(files > 100);
});
