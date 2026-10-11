"""Packages from git sources (docs/design/packages.md §4.6), offline: a bare
repository this runner makes, declared by its file:// URL at a tag and at a
commit, fetched into GOOSE_HOME (which run_tests.py points under build/),
fetched again by `goose fetch --update` after the tag moved, and a library
requiring the package at another ref than the root declares."""
import os
import shutil
import subprocess
from pathlib import Path

import toolchain as tc


def check_git_packages(exe, base, *, jit):
    """Returns why it was skipped, or None when every check passed; raises
    RuntimeError on a failure."""
    git = shutil.which("git")
    if not git:
        return "no git on PATH"
    home = os.environ.get("GOOSE_HOME")
    if not home:
        return "GOOSE_HOME not set"

    def rmtree(path):
        # A git checkout holds read-only files, which Windows will not
        # delete until they are writable.
        if not path.exists():
            return
        for p in path.rglob("*"):
            try:
                p.chmod(0o777)
            except OSError:
                pass
        shutil.rmtree(path)

    base = (Path(base) / "git-packages").resolve()
    rmtree(base)
    # What an earlier run left in the cache, so that this one fetches.
    for cached in (Path(home) / "pkg").glob("gitpkg-*"):
        rmtree(cached)
    work = base / "work"
    work.mkdir(parents=True)

    def run_git(*args, cwd=work):
        r = subprocess.run([git, "-c", "user.name=goose", "-c", "user.email=goose@example.com",
                            "-c", "commit.gpgsign=false", *args],
                           cwd=cwd, capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            raise RuntimeError(f"git {' '.join(args)} failed:\n{r.stdout}{r.stderr}")
        return r.stdout.strip()

    def entry(code):
        tc.write_text(work / "gitpkg.goose",
                      f"namespace gitpkg;\nfn code() -> i64 {{ {code} }}\n")

    entry(5)
    run_git("init", "-q", "-b", "main", ".")
    run_git("add", ".")
    run_git("commit", "-q", "-m", "one")
    run_git("tag", "v1")
    entry(6)
    run_git("commit", "-q", "-am", "two")
    run_git("tag", "v2")
    second = run_git("rev-parse", "HEAD")
    bare = base / "gitpkg.git"
    run_git("clone", "-q", "--bare", str(work), str(bare))
    # Fetching one commit by its hash is what GitHub allows; a bare
    # repository of our own has to be told to.
    run_git("config", "uploadpack.allowAnySHA1InWant", "true", cwd=bare)
    url = "file:///" + bare.as_posix().lstrip("/")

    def program(name, lines):
        path = base / f"{name}.goose"
        tc.write_text(path, "\n".join(lines) + "\n")
        return path

    use_tag = program("use_tag", [f'import gitpkg from "{url}@v1";',
                                  "fn main() { print(gitpkg::code()); }"])
    use_hash = program("use_hash", [f'import gitpkg from "{url}@{second}";',
                                    "fn main() { print(gitpkg::code()); }"])
    lib = base / "lib"
    lib.mkdir()
    tc.write_text(lib / "lib.goose",
                  f'namespace lib;\nimport gitpkg from "{url}@v2";\n'
                  "fn code() -> i64 { gitpkg::code() }\n")
    use_lib = program("use_lib", ['import lib from "./lib";',
                                  f'import gitpkg from "{url}@v1";',
                                  "fn main() { print(lib::code()); }"])

    def goose(*args):
        code, out, err = tc.run_capture([exe, *args], cwd=base)
        if code != 0 or tc.sanitizer_failure(err):
            raise RuntimeError(f"goose {' '.join(str(a) for a in args)} (exit {code}):\n{out}{err}")
        return out, err

    def expect(path, want, warning=None):
        """The program prints `want` under the JIT, or typechecks where
        there is none; `warning` is a text its compile must warn with."""
        if jit:
            out, err = goose("run", path)
            if out.strip() != want:
                raise RuntimeError(f"{path.name} printed {out!r}, not {want!r}\n{err}")
        else:
            out, err = goose("check", path)
        if warning and warning not in err:
            raise RuntimeError(f"{path.name}: no warning with {warning!r} in:\n{err}")
        if not warning and "warning:" in err:
            raise RuntimeError(f"{path.name}: unexpected warning:\n{err}")

    out, err = goose("fetch", use_tag)
    if "fetching the package gitpkg" not in err:
        raise RuntimeError(f"goose fetch did not report the fetch:\n{out}{err}")
    expect(use_tag, "5")
    expect(use_hash, "6")
    expect(use_lib, "5", warning="declares at v1")
    # The tag moves; the cached checkout stays until --update.
    run_git("tag", "-f", "v1", "HEAD")
    run_git("fetch", "-q", "--tags", "--force", str(work), cwd=bare)
    expect(use_tag, "5")
    out, err = goose("fetch", "--update", use_tag)
    if "updating the package gitpkg" not in err:
        raise RuntimeError(f"goose fetch --update did not report the update:\n{out}{err}")
    expect(use_tag, "6")
    return None
