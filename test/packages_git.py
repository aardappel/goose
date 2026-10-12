"""Packages from git sources (docs/design/packages.md §4.6), offline: a bare
repository this runner makes, declared by its file:// URL at a tag and at a
commit, fetched into GOOSE_HOME (which run_tests.py points under build/),
fetched again by `goose fetch --update` after the tag moved, and a library
requiring the package at another ref than the root declares. Concurrent
fetches share a cold cache, including a submodule and an abandoned checkout."""
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
                            "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", *args],
                           cwd=cwd, capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            raise RuntimeError(f"git {' '.join(args)} failed:\n{r.stdout}{r.stderr}")
        return r.stdout.strip()

    def entry(code):
        tc.write_text(work / "gitpkg.goose",
                      "namespace gitpkg;\nimport .sub.helper;\n"
                      f"fn code() -> i64 {{ helper() + {code} }}\n")

    sub = base / "subrepo"
    sub.mkdir()
    tc.write_text(sub / "helper.goose", "namespace gitpkg;\nfn helper() -> i64 { 0 }\n")
    run_git("init", "-q", "-b", "main", ".", cwd=sub)
    run_git("add", ".", cwd=sub)
    run_git("commit", "-q", "-m", "submodule", cwd=sub)

    entry(5)
    run_git("init", "-q", "-b", "main", ".")
    run_git("-c", "protocol.file.allow=always", "submodule", "add", "-q", sub.as_uri(), "sub")
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

    # These repositories and their submodule are local; allow Git's file
    # transport only in these test processes, without changing user config.
    env = dict(os.environ, GIT_ALLOW_PROTOCOL="file")

    def goose(*args):
        r = subprocess.run([exe, *args], cwd=base, env=env, capture_output=True, timeout=60)
        out, err = tc.decode(r.stdout), tc.decode(r.stderr)
        if r.returncode != 0 or tc.sanitizer_failure(err):
            raise RuntimeError(f"goose {' '.join(str(a) for a in args)} "
                               f"(exit {r.returncode}):\n{out}{err}")
        return out, err

    def concurrent_fetch(path):
        workers = []
        outputs = []
        try:
            for _ in range(8):
                workers.append(subprocess.Popen([exe, "fetch", path], cwd=base, env=env,
                                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT))
            for worker in workers:
                output, _ = worker.communicate(timeout=60)
                output = tc.decode(output)
                outputs.append(output)
                if worker.returncode != 0 or tc.sanitizer_failure(output):
                    raise RuntimeError(f"concurrent fetch {path.name} "
                                       f"(exit {worker.returncode}):\n{output}")
        finally:
            for worker in workers:
                if worker.poll() is None:
                    worker.kill()
                worker.communicate()
        fetches = sum(output.count("fetching the package gitpkg") for output in outputs)
        if fetches != 1:
            raise RuntimeError(f"concurrent fetch {path.name}: fetched {fetches} times, not once")

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

    concurrent_fetch(use_tag)
    expect(use_tag, "5")
    # Simulate an interrupted commit fetch. Only the lock holder may clean
    # this up, and the waiting processes must use its completed checkout.
    caches = list((Path(home) / "pkg").glob("gitpkg-*/v1"))
    if len(caches) != 1:
        raise RuntimeError(f"expected one cached v1 checkout, found {caches}")
    stale = caches[0].parent / f"{second[:12]}.tmp"
    stale.mkdir()
    tc.write_text(stale / "unfinished", "interrupted fetch\n")
    concurrent_fetch(use_hash)
    if stale.exists():
        raise RuntimeError("commit fetch left its temporary checkout behind")
    expect(use_hash, "6")
    expect(use_lib, "5", warning="declares at v1")
    # A failed clone must release the lock and leave the ref retryable.
    use_retry = program("use_retry", [f'import gitpkg from "{url}@retry";',
                                      "fn main() { print(gitpkg::code()); }"])
    failed = subprocess.run([exe, "fetch", use_retry], cwd=base, env=env,
                            capture_output=True, timeout=60)
    err = tc.decode(failed.stderr)
    if (failed.returncode != 1 or "cannot fetch the package gitpkg" not in err or
            tc.sanitizer_failure(err)):
        raise RuntimeError(f"expected a failed package fetch (exit {failed.returncode}):\n{err}")
    if any((caches[0].parent / name).exists() for name in ("retry", "retry.tmp")):
        raise RuntimeError("failed fetch left a checkout behind")
    run_git("tag", "retry", "HEAD", cwd=bare)
    concurrent_fetch(use_retry)
    expect(use_retry, "6")
    # The tag moves; the cached checkout stays until --update.
    run_git("tag", "-f", "v1", "HEAD")
    run_git("fetch", "-q", "--tags", "--force", str(work), cwd=bare)
    expect(use_tag, "5")
    out, err = goose("fetch", "--update", use_tag)
    if "updating the package gitpkg" not in err:
        raise RuntimeError(f"goose fetch --update did not report the update:\n{out}{err}")
    expect(use_tag, "6")
    return None
