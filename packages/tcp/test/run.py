"""Run the optional TCP package's loopback tests with a local Goose compiler."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def check(argv, env, expected=None, timeout=30):
    result = subprocess.run(argv, env=env, capture_output=True, text=True, timeout=timeout)
    if result.returncode or (expected is not None and result.stdout != expected):
        raise SystemExit(f"{argv} exited with {result.returncode}\n{result.stdout}{result.stderr}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("goose", type=Path)
    parser.add_argument("--cc")
    args = parser.parse_args()
    goose = args.goose.resolve()
    cc = ["--cc", args.cc] if args.cc else []
    test_dir = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="goose-tcp-test-") as temp:
        env = dict(os.environ, GOOSE_HOME=str(Path(temp) / "cache"))
        for name in ("basic", "echo", "cancel", "handoff"):
            source = test_dir / f"{name}.goose"
            output = test_dir / f"{name}.out"
            expected = output.read_text() if output.exists() else ""
            binary = Path(temp) / (f"{name}.exe" if os.name == "nt" else name)
            check([goose, "build", source, "--offline", "--debug", "-o", binary, *cc],
                  env, timeout=60)
            check([binary], env, expected)
            print(f"{name} AOT passed")
            if name in ("basic", "handoff"):
                for level in ("-O0", "-O2"):
                    check([goose, "run", source, "--offline", "--debug", level, *cc], env, expected)
                    print(f"{name} JIT {level} passed")


if __name__ == "__main__":
    main()
