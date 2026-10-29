"""Regression for real Windows argv and JIT's distinct supplied vector.

Run with an explicitly built compiler and C backend; no package download or
builtin module is needed. Other platforms report NOT_RUN, not a pass.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

VALUES = ['two words', '', 'quote"x', 'trail\\', '--offline', '--', '\u03b1 \u03b2', '\U0001f331', 'e\u0301']

def check(exe, cc, out, style):
    out = Path(out).resolve()
    if out.exists():
        raise RuntimeError('Use a fresh output directory; retain previous evidence')
    out.mkdir(parents=True)
    repo = Path(__file__).resolve().parent.parent
    env = dict(os.environ, GOOSE_HOME=str(out / 'home'), GOOSE_STDLIB=str(repo / 'stdlib'), GOOSE_RUNTIME=str(repo / 'src/runtime'))
    env.pop('GOOSE_CC', None)
    folder = out / 'source space \u03b1'
    folder.mkdir()
    program = folder / 'argv.goose'
    program.write_text('import os;\nlet initial = args();\nexport fn argc_seen() -> i64 { return initial.len; }\nfn main() { print("START"); for a in initial { print("[",a,"]"); } print("CURRENT"); for a in args() { print("[",a,"]"); } }\n', encoding='utf-8')
    records = []

    def run(name, argv):
        launch = argv if isinstance(argv, str) else list(map(str, argv))
        with subprocess.Popen(launch, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env) as child:
            data, error = child.communicate()
            code, pid = child.returncode, child.pid
        (out / (name + '.stdout')).write_bytes(data)
        (out / (name + '.stderr')).write_bytes(error)
        records.append({'name': name, 'argv': launch, 'raw_command_line': isinstance(argv, str), 'pid': pid, 'exit': code,
                        'stdout_sha256': hashlib.sha256(data).hexdigest(), 'stderr_sha256': hashlib.sha256(error).hexdigest()})
        (out / 'processes.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
        return code, data, error

    def require(good, name):
        if not good:
            raise RuntimeError(name + ' failed; original literal expectation and raw output retained')

    def expected(argv0, values=VALUES):
        block = b''.join(('[' + item + ']\n').encode('utf-8') for item in [str(argv0), *values])
        return b'START\n' + block + b'CURRENT\n' + block

    app = folder / 'native \u03b1.exe'
    r = run('build-separate', [exe, 'build', '--offline', '--cc', cc, '-o', app, program])
    require(r[0] == 0 and app.is_file(), 'build separate runtime')
    r = run('native', [app, *VALUES])
    require(r[0] == 0 and r[1] == expected(app) and not r[2], 'native Unicode argv and globals')
    copied = folder / 'copied \u03b2.exe'
    shutil.copyfile(app, copied)
    r = run('copied-native', [copied, *VALUES])
    require(r[0] == 0 and r[1] == expected(copied) and not r[2] and app.read_bytes() == copied.read_bytes(), 'copied argv0')
    # A string reaches CreateProcess unchanged: an argv list would re-quote it
    # and miss the CRT/Shell32 distinction in adjacent quoted double quotes.
    raw_cases = [('adjacent-quotes', '"a""b" c', ['a"b', 'c']),
                 ('unicode-quotes', '"\u03b1""\u03b2" \U0001f331', ['\u03b1"\u03b2', '\U0001f331']),
                 ('empty-tab', '""\t"two words"', ['', 'two words'])]

    def raw_arguments(label, target):
        for case, suffix, values in raw_cases:
            command = '"' + str(target) + '" ' + suffix
            r = run(label + '-raw-' + case, command)
            require(r[0] == 0 and r[1] == expected(target, values) and not r[2],
                    label + ' CRT raw quoting: ' + case)

    raw_arguments('native', app)
    raw_arguments('copied-native', copied)
    r = run('jit', [exe, 'run', '--offline', program.as_posix(), '--', *VALUES])
    require(r[0] == 0 and r[1] == expected(program.as_posix()), 'JIT supplied vector')
    cfile = folder / 'standalone.c'
    r = run('emit-standalone', [exe, '--offline', '--standalone', '-o', cfile, program])
    require(r[0] == 0 and cfile.is_file(), 'standalone emission')
    standalone = folder / 'standalone.exe'
    command = ([cc, '/nologo', '/O1', '/fp:precise', '/utf-8', cfile, '/Fe:' + str(standalone), '/Fo:' + str(folder) + '\\'] if style == 'msvc' else [cc, '-O1', '-ffp-contract=off', cfile, '-municode', '-o', standalone])
    r = run('compile-standalone', command)
    require(r[0] == 0 and standalone.is_file(), 'standalone native compile')
    r = run('standalone', [standalone, *VALUES])
    require(r[0] == 0 and r[1] == expected(standalone) and not r[2], 'standalone Unicode argv')
    raw_arguments('standalone', standalone)
    # A C host and a renamed-main batch dispatcher deliberately supply vectors
    # different from the OS process arguments. They must remain theirs.
    runtime = folder / 'runtime.c'
    r = run('emit-runtime', [exe, '--offline', '--emit-runtime', runtime])
    require(r[0] == 0 and runtime.is_file(), 'runtime emission')

    def compile_host(name, sources):
        target = folder / (name + '.exe')
        command = ([cc, '/nologo', '/O1', '/fp:precise', '/utf-8', *sources,
                    '/Fe:' + str(target), '/Fo:' + str(folder) + '\\'] if style == 'msvc' else
                   [cc, '-O1', '-ffp-contract=off', *sources, '-o', target])
        result = run('compile-' + name, command)
        require(result[0] == 0 and target.is_file(), name + ' compile')
        return target

    def supplied(first, value):
        block = b''.join(('[' + item + ']\n').encode('utf-8')
                         for item in [first, value, '', '--offline', '--'])
        return b'START\n' + block + b'CURRENT\n' + block

    library, header = folder / 'embedded.c', folder / 'embedded.h'
    r = run('emit-library', [exe, '--offline', '--header', header, '-o', library, program])
    require(r[0] == 0 and library.is_file() and header.is_file(), 'library emission')
    host = folder / 'embedding-host.c'
    host.write_text('#include "embedded.h"\nint main(void) { char *a[]={"embedding-supplied","host value","","--offline","--",0}; char *b[]={"ignored",0}; goose_init(5,a); if(argc_seen()!=5) return 31; goose_init(1,b); if(argc_seen()!=5) return 32; return 0; }\n', encoding='utf-8')
    embedded = compile_host('embedding', [library, runtime, host])
    r = run('embedding-vector', [embedded, 'different OS arguments', *VALUES])
    require(r[0] == 0 and r[1] == supplied('embedding-supplied', 'host value') and not r[2],
            'embedding explicit vector and repeated initialization')
    batch_c = folder / 'batch-program.c'
    r = run('emit-batch', [exe, '--offline', '-o', batch_c, program])
    require(r[0] == 0 and batch_c.is_file(), 'batch emission')
    dispatch = folder / 'batch-host.c'
    dispatch.write_text('#define main gs_batch_entry\n#include "batch-program.c"\n#undef main\nint main(void) { char *a[]={"batch-supplied","different slice","","--offline","--",0}; return gs_batch_entry(5,a); }\n', encoding='utf-8')
    batch = compile_host('batch', [dispatch, runtime])
    r = run('batch-vector', [batch, 'different OS arguments', *VALUES])
    require(r[0] == 0 and r[1] == supplied('batch-supplied', 'different slice') and not r[2],
            'renamed-main explicit vector')
    r = run('invalid-utf16', [app, '\ud800'])
    require(r[0] == 1 and b'cannot convert native Unicode argument to UTF-8' in r[2] and not r[1], 'invalid UTF-16 refusal')
    if (out / 'home/pkg').exists():
        require(not any((out / 'home/pkg').iterdir()), 'empty package cache')
    print('Windows native/JIT argv regression passed')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--cc-style', choices=['msvc', 'gcc'], default='msvc')
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    if os.name != 'nt':
        print('NOT_RUN: Windows-only native argv regression')
        raise SystemExit(2)
    check(str(Path(args.exe).resolve()), str(Path(args.cc).resolve()), args.out, args.cc_style)