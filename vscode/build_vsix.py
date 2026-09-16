#!/usr/bin/env python3
"""Install build dependencies, test, and package the Goose VS Code extension.

Run from any directory: python path/to/vscode/build_vsix.py
Requires Node.js and npm on PATH; Python needs no extra packages.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--skip-install", action="store_true",
        help="reuse installed node_modules instead of running npm ci",
    )
    args = parser.parse_args()
    # Windows supplies npm as a .cmd launcher, not an executable named npm.
    npm = shutil.which("npm.cmd" if os.name == "nt" else "npm")
    if not npm or not shutil.which("node"):
        parser.error("Node.js and npm are required on PATH (Node.js 22+ recommended).")

    def run(*arguments):
        print("+ npm " + " ".join(arguments), flush=True)
        subprocess.run([npm, *arguments], cwd=HERE, check=True)

    try:
        if not args.skip_install:
            run("ci", "--include=dev", "--no-audit", "--no-fund")
        # The package script's prepublish hook runs the extension tests.
        run("run", "package")
    except subprocess.CalledProcessError as error:
        print(f"VSIX build failed (exit code {error.returncode}).", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"Could not run npm: {error}", file=sys.stderr)
        return 1

    print(f"Built {HERE / 'goose-language.vsix'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
