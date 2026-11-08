"""The runtime the compiler embeds is the one in the source tree:
src/runtime_inline.h, which the compiler is built with and which carries the
runtime's C into every program it writes, is byte for byte what
`goose --gen-runtime-header` writes from src/runtime/. The generator runs on
a copy of src/runtime/, named by GOOSE_RUNTIME, so the check writes nothing
in the tree. A runtime file changed without the header regenerated fails
it, and so does a header a merge took from one side when the other side
changed the runtime too.

    python test/runtime_header.py [--exe EXE]
"""
import argparse
import difflib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# The lines of a difference shown in a failure.
SHOWN = 40


def check_runtime_header(exe, base=None, root=ROOT):
    """The header's size in bytes, where it is what the generator writes from
    src/runtime/; else RuntimeError with the start of the difference."""
    root = Path(root)
    if base is not None:
        base = Path(base).resolve()
        base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="runtime-header-", dir=base) as tmp:
        # The generator writes runtime_inline.h beside the directory it reads.
        runtime = Path(tmp) / "src" / "runtime"
        shutil.copytree(root / "src" / "runtime", runtime)
        env = dict(os.environ, GOOSE_RUNTIME=str(runtime))
        p = subprocess.run([str(exe), "--gen-runtime-header"], cwd=tmp, env=env,
                           capture_output=True, text=True, errors="replace", timeout=120)
        written = runtime.parent / "runtime_inline.h"
        if p.returncode != 0 or not written.is_file():
            raise RuntimeError(f"goose --gen-runtime-header on a copy of src/runtime/ failed "
                               f"(exit {p.returncode}):\n{p.stdout}{p.stderr}")
        want = written.read_bytes()
    have = (root / "src" / "runtime_inline.h").read_bytes()
    if have != want:
        diff = list(difflib.unified_diff(have.decode("utf-8", "replace").splitlines(True),
                                         want.decode("utf-8", "replace").splitlines(True),
                                         "src/runtime_inline.h", "goose --gen-runtime-header"))
        more = f"... and {len(diff) - SHOWN} more lines\n" if len(diff) > SHOWN else ""
        raise RuntimeError("src/runtime_inline.h is not what `goose --gen-runtime-header` "
                           "writes from src/runtime/: run it and rebuild the compiler\n" +
                           "".join(diff[:SHOWN]) + more)
    return len(want)


if __name__ == "__main__":
    sys.path.insert(0, str(ROOT / "scripts"))
    import toolchain as tc
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", help="goose executable (default: the runners' choice under build/)")
    exe = tc.find_goose(ap.parse_args().exe)
    try:
        n = check_runtime_header(exe)
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as e:
        print(e)
        sys.exit(1)
    print(f"src/runtime_inline.h ({n} bytes) is what {exe} --gen-runtime-header writes "
          "from src/runtime/")
