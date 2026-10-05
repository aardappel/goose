#!/usr/bin/env python3
"""Check gfx mouse/focus setters against an actual hidden SDL window.

Requires a display and GPU, so this complements the regular headless Goose
fixture rather than running in the main suite. It does not test physical
mouse confinement or application switching; those still need manual checks.

  python test/gfx/window/run_input_test.py --exe build/Release/goose.exe
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
import toolchain as tc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", help="Goose compiler whose gfx library to test")
    args = parser.parse_args()
    tc.setup_console()
    goose = tc.find_goose(args.exe)
    cc = tc.test_cc("native")
    links = tc.gfx_link(goose, cc)
    if not links:
        print("the selected compiler has no gfx library", file=sys.stderr)
        return 1
    output = ROOT / "build" / "gfx-window-input"
    output.mkdir(parents=True, exist_ok=True)
    exe = output / ("gfx_input" + tc.EXE_SUFFIX)
    include = str(ROOT / "third_party" / "SDL" / "include")
    flags = [f"/I{include}"] if cc.style == "msvc" else [f"-I{include}"]
    ok, log = cc.compile(Path(__file__).with_name("gfx_input.c"), exe,
                         opt=2, extra=flags, libs=links, log=output / "compile.log")
    if not ok:
        print(log, file=sys.stderr)
        return 1
    env = dict(os.environ)
    env.pop("GOOSE_GFX_HEADLESS", None)
    return subprocess.run([str(exe)], env=env, timeout=30).returncode


if __name__ == "__main__":
    raise SystemExit(main())
