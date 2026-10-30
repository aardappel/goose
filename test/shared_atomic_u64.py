#!/usr/bin/env python3
"""Exhaustive order legality and generated-C inspection for the u64 candidate."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
ORDERS = ['Relaxed', 'Acquire', 'Release', 'AcqRel', 'SeqCst']
FAILURES = {
    'Relaxed': {'Relaxed'},
    'Acquire': {'Relaxed', 'Acquire'},
    'Release': {'Relaxed'},
    'AcqRel': {'Relaxed', 'Acquire'},
    'SeqCst': {'Relaxed', 'Acquire', 'SeqCst'},
}


def invoke(args):
    return subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=60)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    exe = Path(parser.parse_args().exe).resolve()
    with tempfile.TemporaryDirectory(prefix='goose-atomic-orders-') as tmp:
        tmp = Path(tmp)
        src = tmp / 'order.goose'
        cases = []
        for success in ORDERS:
            for failure in ORDERS:
                cases.append((f'atomic_compare_exchange(a, 0, 1, "{success}", "{failure}");',
                              failure in FAILURES[success]))
        for order in ORDERS:
            cases.append((f'atomic_load(a, "{order}");', order not in {'Release', 'AcqRel'}))
            cases.append((f'atomic_store(a, 1, "{order}");', order not in {'Acquire', 'AcqRel'}))
            cases.append((f'atomic_fetch_add(a, 1, "{order}");', True))
        for statement, valid in cases:
            src.write_text('fn main() { let o, ok = shared_alloc(8); '
                           'let a = shared_atomic_u64(o, 0, 0); ' + statement + ' }\n')
            p = invoke([exe, 'check', src])
            assert (p.returncode == 0) == valid, (statement, p.stdout, p.stderr)
            if not valid:
                assert 'order' in p.stderr and 'error:' in p.stderr, p.stderr
        for opt in ('0', '2'):
            c = tmp / ('atomic' + opt + '.c')
            p = invoke([exe, '-O' + opt, '--standalone', '-o', c,
                        ROOT / 'test/memory/shared_atomics.goose'])
            assert p.returncode == 0, p.stderr
            text = c.read_text()
            assert '__atomic_fetch_add(' in text
            assert '__atomic_load_n(' in text and '__atomic_store_n(' in text
            assert '__atomic_always_lock_free(8, 0)' in text
            assert '__GCC_ATOMIC_LLONG_LOCK_FREE != 2' in text
            assert 'generation validation must be always lock-free' in text
            assert re.search(r'__atomic_compare_exchange_n\([^;]+, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE\)', text)
            assert '__TINYC__' in text and '#error Native shared atomics require GCC or Clang' in text
            for cc in ('gcc', 'clang'):
                binary = tmp / (cc + opt)
                p = invoke([cc, '-O' + opt, '-DGS_DEBUG=' + ('1' if opt == '0' else '0'),
                            '-pthread', c, '-lm', '-o', binary])
                assert p.returncode == 0, p.stderr
                for _ in range(10):
                    p = invoke([binary])
                    assert p.returncode == 0 and p.stdout == 'shared atomics ok\n', (p.stdout, p.stderr)
                p = invoke(['nm', '-u', binary])
                assert p.returncode == 0 and not re.search(r'\b__atomic_', p.stdout), p.stdout
        # Prove the capability gate rejects non-lock-free generation validation,
        # without requiring a 32-bit libc/sysroot.
        gate = re.search(r'#if __GCC_ATOMIC_LLONG_LOCK_FREE != 2.*?#endif', text, re.S)[0]
        for target, march, valid in [('i386-unknown-linux-gnu', 'i486', False),
                                     ('x86_64-unknown-linux-gnu', 'x86-64', True)]:
            probe = tmp / 'gate.c'
            probe.write_text(gate + '\nint probe;\n')
            p = invoke(['clang', '-target', target, '-march=' + march,
                        '-ffreestanding', '-fsyntax-only', probe])
            assert (p.returncode == 0) == valid, p.stderr
            if not valid:
                assert 'generation validation' in p.stderr, p.stderr
        sanitized = tmp / 'sanitized'
        p = invoke(['clang', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-pthread', c, '-lm', '-o', sanitized])
        assert p.returncode == 0, p.stderr
        p = invoke([sanitized])
        assert p.returncode == 0 and p.stdout == 'shared atomics ok\n', p.stderr
        print('40 order cases; O0/O2 generated-C inspection; 40 GCC/Clang exact-once runs passed')
        print('Clang ASan/UBSan atomic fixture and i486/x86-64 lock-free gates passed')


if __name__ == '__main__':
    main()
