#!/usr/bin/env python3
"""Focused shared capability checks; run with --exe path/to/goose.

Runs GCC and Clang, debug and release, plus sanitizer lifecycle checks.
The normal test runner independently covers these Goose fixtures and diagnostics.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def run(args, expected=0):
    p = subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=90)
    if p.returncode != expected:
        raise RuntimeError(f'{args}: exit {p.returncode}\n{p.stdout}{p.stderr}')
    return p.stdout + p.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    args = parser.parse_args()
    exe = Path(args.exe).resolve()
    with tempfile.TemporaryDirectory(prefix='goose-shared-') as tmp:
        tmp = Path(tmp)
        for src in sorted((ROOT / 'test/errors_tc').glob('shared_*.goose')):
            output = run([exe, 'check', src], expected=1)
            for diagnostic in re.findall(r'// error: (.*)', src.read_text()):
                assert diagnostic in output, (src, diagnostic, output)
        for cc in ('gcc', 'clang'):
            assert shutil.which(cc), f'{cc} is required'
            for opt, debug in (('0', '1'), ('2', '0')):
                for src in sorted((ROOT / 'test/memory').glob('*.goose')):
                    c = tmp / (src.stem + '.c')
                    run([exe, '-O' + opt, '--standalone', '-o', c, src])
                    binary = tmp / src.stem
                    run([cc, '-std=gnu11', '-O' + opt, '-DGS_DEBUG=' + debug,
                         '-pthread', c, '-lm', '-o', binary])
                    expected = (ROOT / 'test/expected' / (src.stem + '.out')).read_text()
                    assert run([binary]) == expected, src
                binary = tmp / 'lifecycle'
                run([cc, '-std=gnu11', '-O' + opt, '-DGS_DEBUG=' + debug,
                     '-pthread', ROOT / 'test/memory/runtime_shared_lifecycle.c',
                     '-lm', '-o', binary])
                assert 'shared runtime lifecycle ok' in run([binary])
                print(f'{cc} O{opt} GS_DEBUG={debug}: shared fixtures and lifecycle passed')
        binary = tmp / 'sanitized-lifecycle'
        run(['clang', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-pthread',
             ROOT / 'test/memory/runtime_shared_lifecycle.c', '-lm', '-o', binary])
        assert 'shared runtime lifecycle ok' in run([binary])
        print('Clang ASan/UBSan lifecycle passed; shared diagnostics passed')


if __name__ == '__main__':
    main()
