#!/usr/bin/env python3
"""Build the real command formatter and check its POSIX shell round trip.

Exit 1 means the shell changed at least one argument; exit 2 means a test
or build error. No compiler binary
from an unknown build is used. All build and shell files are temporary.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", default="HEAD", help="committed source to test (default: HEAD)")
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]

    def git(*words):
        return subprocess.check_output(["git", "-C", str(root), *words])

    commit = git("rev-parse", args.ref + "^{commit}").decode().strip()
    print("source_commit=" + commit, flush=True)
    print("platform=" + sys.platform, flush=True)
    print("shell=" + os.path.realpath("/bin/sh"), flush=True)
    print(subprocess.check_output([args.cxx, "--version"], text=True).splitlines()[0],
          flush=True)

    with tempfile.TemporaryDirectory(prefix="goose-f11-") as tmp:
        work = Path(tmp)
        source = work / "source"
        source.mkdir()
        for name in ("includes.h", "utils.h", "process.h", "process.cpp",
                     "progress.h", "progress.cpp"):
            data = git("show", commit + ":src/" + name)
            (source / name).write_bytes(data)
            print("sha256 src/" + name + "=" + hashlib.sha256(data).hexdigest(),
                  flush=True)
        vendor = git("show", commit + ":third_party/subprocess/subprocess.h")
        (source / "subprocess.h").write_bytes(vendor)
        print("sha256 third_party/subprocess/subprocess.h=" +
              hashlib.sha256(vendor).hexdigest(), flush=True)
        driver = Path(__file__).with_name("driver.cpp")
        print("sha256 driver.cpp=" + hashlib.sha256(driver.read_bytes()).hexdigest(),
              flush=True)
        binary = work / "formatter"
        command = [args.cxx, "-std=c++20", "-O0", "-I", str(source), str(driver),
                   str(source / "process.cpp"), str(source / "progress.cpp"),
                   "-pthread", "-ldl", "-o", str(binary)]
        print("build=" + shlex.join(command), flush=True)
        subprocess.run(command, check=True)
        print("build_exit=0", flush=True)
        print("sha256 formatter=" + hashlib.sha256(binary.read_bytes()).hexdigest(),
              flush=True)

        fixtures = work / "fixtures"
        fixtures.mkdir()
        for name in ("a.c", "b.c"):
            (fixtures / name).touch()
        print('fixture_files=["a.c", "b.c"]', flush=True)
        # set records positional arguments without calling a tool. Print
        # the count first: printf alone cannot tell zero arguments from one
        # empty argument. NUL separators preserve each argument's contents.
        # The formatter receives the fixed command words and literal input.
        cases = ("plain.c", "two words.c", "", "*.c", "?.c", "[ab].c")
        failures = 0
        for value in cases:
            printed = subprocess.check_output(
                [str(binary), "set", "--", value], text=True)
            shell = subprocess.run(
                ["/bin/sh", "-c", printed +
                 '\nprintf \'%s\\0\' "$#"\nif [ "$#" -gt 0 ]; then printf \'%s\\0\' "$@"; fi'],
                cwd=fixtures, env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                capture_output=True, check=True)
            if shell.stderr:
                raise RuntimeError(shell.stderr.decode())
            if not shell.stdout.endswith(b"\0"):
                raise RuntimeError("Missing argument separator")
            parts = shell.stdout[:-1].split(b"\0")
            count = int(parts[0])
            actual = [part.decode() for part in parts[1:]]
            if count != len(actual):
                raise RuntimeError("Argument count does not match shell output")
            passed = count == 1 and actual == [value]
            failures += not passed
            print(json.dumps({"input": [value], "printed": printed,
                              "actual_count": count, "actual": actual,
                              "result": "PASS" if passed else "FAIL"}),
                  flush=True)
        print(f"round_trip_failures={failures}/{len(cases)}", flush=True)
        return int(failures != 0)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError, RuntimeError, ValueError) as error:
        print("checker_error: " + str(error), file=sys.stderr)
        sys.exit(2)
