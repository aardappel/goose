"""Regression check for source-stdlib discovery beyond executable-relative paths.

python check_stdlib_discovery.py --exe /path/to/out-of-tree/goose --stdlib /source/goose/stdlib
Use an actual out-of-tree build, or --relocate to expose the fallback with an
in-tree CI build. Uses only Python's standard library; no native compilation.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--stdlib", type=Path, required=True)
    parser.add_argument("--relocate", action="store_true",
                        help="Copy the compiler away from source-relative stdlib locations")
    options = parser.parse_args()
    compiler, stdlib = options.exe.resolve(), options.stdlib.resolve()
    environment = os.environ.copy()
    environment.pop("GOOSE_STDLIB", None)
    with tempfile.TemporaryDirectory(prefix="goose stdlib discovery ") as directory:
        work = Path(directory)
        if options.relocate:
            # Keep all executable-relative probes inside this temporary tree.
            relocated = work / "tools" / "isolated" / "bin"
            relocated.mkdir(parents=True)
            compiler = Path(shutil.copy2(compiler, relocated / compiler.name))
        source = work / "main.goose"
        source.write_text('import std; fn main() { print("ok"); }\n', encoding="utf-8")

        def check(*arguments, env=environment):
            result = subprocess.run([str(compiler), "--check", *map(str, arguments), str(source)],
                                    cwd=work, env=env, capture_output=True, text=True,
                                    errors="replace")
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)

        check()  # Executable-relative paths cannot mask a missing source fallback.
        check("--stdlib", stdlib)
        configured = environment.copy()
        configured["GOOSE_STDLIB"] = str(stdlib)
        check(env=configured)
        # Explicit path wins over an environment directory containing another std.
        explicit, alternate = work / "explicit modules", work / "environment modules"
        explicit.mkdir()
        alternate.mkdir()
        (explicit / "std.goose").write_text("fn explicit_marker() -> i64 { 42 }\n", encoding="utf-8")
        (alternate / "std.goose").write_text("fn environment_marker() -> i64 { 7 }\n", encoding="utf-8")
        source.write_text("import std; fn main() { print(explicit_marker()); }\n", encoding="utf-8")
        configured["GOOSE_STDLIB"] = str(alternate)
        check("--stdlib", explicit, env=configured)
        source.write_text("import std; fn main() { print(environment_marker()); }\n", encoding="utf-8")
        check(env=configured)
        # A root-program module retains priority over all library locations.
        (work / "std.goose").write_text("fn root_marker() -> i64 { 1 }\n", encoding="utf-8")
        source.write_text("import std; fn main() { print(root_marker()); }\n", encoding="utf-8")
        check("--stdlib", explicit, env=configured)
    print("Source-stdlib fallback, explicit/environment discovery, precedence and root priority passed")


if __name__ == "__main__":
    main()
