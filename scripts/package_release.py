#!/usr/bin/env python3
"""Lays out a release of the compiler from a CMake build of it, for
.github/workflows/release.yml, and as a way to try one locally.

The compiler finds what it needs beside its own binary when the build tree
it was built in is not there (main.cpp's StdlibDirs, jit.h's JitLibPath,
builtin.h's BuiltinSrcDir and BuiltinPackageLinkFile), so a release is one
directory:

  goose[.exe]          the compiler, with the blessed packages compiled in
  stdlib/              the standard library
  tcclib/              TinyCC's headers and support archive, for in-process runs
  pkg/<package>/       each builtin package's Goose sources, sidecar, docs and
                       license, without its vendored dependencies (third_party/);
                       lib/ holding the static libraries a `goose build` of a
                       program using it links, and link-msvc.rsp and
                       link-cc.rsp naming them relative to themselves
  docs/, samples/      as in this tree (less the samples' test harness)
  vscode/              the prebuilt VS Code extension
  man/goose.1          the help as a man page (goose help --man)
  licenses/            the licenses of what the compiler and the packages'
                       libraries are built from
  README.md, LICENSE

Configure with -DGOOSE_BUILTIN_LIBS=all and build the configuration given
here first. Files from this tree are the ones git tracks, so nothing a run
left in samples/ or docs/ is shipped.

  python scripts/package_release.py --build build --config Release --out dist/goose
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
EXE_SUFFIX = ".exe" if os.name == "nt" else ""

# What the samples directory holds for the test suite rather than for people.
SAMPLES_HARNESS = {"run_samples.py", "expected"}

LICENSE_NAME = re.compile(r"^(LICEN[CS]E|COPYING|RELICENSING)", re.IGNORECASE)


def fail(msg):
    sys.exit(f"package_release: {msg}")


def tracked(subdir):
    """The files git tracks under `subdir` of this tree, relative to it."""
    out = subprocess.run(["git", "-C", str(REPO_ROOT), "ls-files", "-z", "--", subdir],
                         check=True, capture_output=True).stdout
    return [Path(p) for p in out.decode("utf-8").split("\0") if p]


def copy_file(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def copy_tracked(subdir, out, skip=()):
    for rel in tracked(subdir):
        parts = rel.relative_to(subdir).parts
        if parts[0] in skip:
            continue
        copy_file(REPO_ROOT / rel, out / rel)


def builtin_packages(build):
    """(name, source directory) of each package compiled into the compiler,
    from the header cmake/package.cmake generated for it."""
    header = build / "generated" / "builtin_packages.h"
    if not header.exists():
        fail(f"{header} is missing: configure {build} first")
    rows = re.findall(r'\{ "(\w+)", \w+, "([^"]*)",', header.read_text(encoding="utf-8"))
    return [(name, Path(srcdir)) for name, srcdir in rows]


def rsp_items(path):
    items = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if len(line) > 1 and line[0] == '"' and line[-1] == '"':
            line = line[1:-1]
        if line:
            items.append(line)
    return items


def rsp_text(items):
    return "".join((f'"{i}"' if " " in i else i) + "\n" for i in items)


def under(path, root):
    try:
        path.resolve().relative_to(root)
        return True
    except ValueError:
        return False


def package_libraries(name, rspdir, build, out):
    """Copies the libraries of the package's link closure that its build
    made into out/lib/ and writes its response files naming them there,
    relative to the response files; system libraries stay as they are."""
    build = build.resolve()
    copied = {}
    for style in ("msvc", "cc"):
        rsp = rspdir / f"link-{style}.rsp"
        if not rsp.exists():
            fail(f"{rsp} is missing: build the compiler in this configuration first")
        items = []
        for item in rsp_items(rsp):
            path = Path(item)
            if path.is_absolute() and path.is_file() and under(path, build):
                rel = f"lib/{path.name}"
                if copied.setdefault(rel, path.resolve()) != path.resolve():
                    fail(f"package {name}: two libraries named {path.name}")
                item = rel
            elif build.as_posix().lower() in item.replace("\\", "/").lower():
                fail(f"package {name}: {rsp} names the build tree in {item!r}, "
                     f"which a release cannot carry")
            items.append(item)
        (out / f"link-{style}.rsp").write_text(rsp_text(items), encoding="utf-8", newline="\n")
    for rel, src in copied.items():
        copy_file(src, out / rel)


def package_sources(name, srcdir, out):
    """The package as its repository has it, less version control and its
    vendored dependencies, whose license files go to licenses/."""
    if not (srcdir / f"{name}.goose").exists():
        fail(f"package {name}: {srcdir} has no {name}.goose")
    for entry in sorted(srcdir.iterdir()):
        if entry.name.startswith(".git") or entry.name == "third_party":
            continue
        if entry.is_dir():
            shutil.copytree(entry, out / entry.name)
        else:
            copy_file(entry, out / entry.name)


def collect_licenses(third_party, out):
    if not third_party.is_dir():
        return
    for dep in sorted(third_party.iterdir()):
        if not dep.is_dir():
            continue
        for f in sorted(dep.iterdir()):
            if f.is_file() and LICENSE_NAME.match(f.name):
                copy_file(f, out / dep.name / f.name)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", default="build", help="the CMake build directory")
    ap.add_argument("--config", default="Release", help="the configuration that was built")
    ap.add_argument("--out", required=True, help="the release directory to write (replaced)")
    args = ap.parse_args()

    build = Path(args.build).resolve()
    out = Path(args.out).resolve()
    exe_name = "goose" + EXE_SUFFIX
    exe = next((p for p in (build / args.config / exe_name, build / exe_name) if p.exists()), None)
    if not exe:
        fail(f"no {exe_name} under {build} for {args.config}")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    copy_file(exe, out / exe_name)
    tcclib = build / "tcclib"
    if not (tcclib / "include" / "stddef.h").exists():
        fail(f"{tcclib} is incomplete: build the compiler first")
    shutil.copytree(tcclib, out / "tcclib")

    packages = builtin_packages(build)
    if not packages:
        fail("the compiler has no packages compiled in: configure with -DGOOSE_BUILTIN_LIBS=all")
    for name, srcdir in packages:
        pkgout = out / "pkg" / name
        package_sources(name, srcdir, pkgout)
        rspdir = build / "pkg" / name / args.config
        if rspdir.is_dir():
            package_libraries(name, rspdir, build, pkgout)
        collect_licenses(srcdir / "third_party", out / "licenses" / name)
        print(f"package {name}: from {srcdir}")

    copy_tracked("stdlib", out)
    copy_tracked("docs", out)
    copy_tracked("samples", out, skip=SAMPLES_HARNESS)
    for rel in ("README.md", "LICENSE", "vscode/goose-language.vsix", "vscode/README.md",
                "vscode/LICENSE"):
        copy_file(REPO_ROOT / rel, out / rel)
    collect_licenses(REPO_ROOT / "third_party", out / "licenses")
    man = subprocess.run([str(out / exe_name), "help", "--man"], capture_output=True)
    if man.returncode != 0 or not man.stdout.startswith(b".TH GOOSE 1"):
        fail(f"goose help --man failed:\n{man.stderr.decode(errors='replace')}")
    (out / "man").mkdir()
    (out / "man" / "goose.1").write_bytes(man.stdout)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
