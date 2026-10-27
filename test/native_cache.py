"""A package's native part, built into one directory under GOOSE_HOME for
every program that uses it: concurrent first builds compile its C once, the
others waiting for that build and linking what it made, and a warm build
compiles nothing. The controlled C compiler, runtime_cache_cc.c, holds the
first compile until the test releases it."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

import toolchain as tc

WAITING = "waiting for another goose building the package shared's C"


def check_native_cache(exe, cc, base):
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="native-cache-", dir=base) as tmp:
        root = Path(tmp)
        compiler = root / ("cache-cc" + tc.EXE_SUFFIX)
        ok, log = cc.compile(Path(__file__).with_name("runtime_cache_cc.c"), compiler)
        if not ok:
            raise RuntimeError(log)
        # The clang-cl copy spells the flags as MSVC does (runtime_cache.py).
        msvc = root / ("clang-cl" + tc.EXE_SUFFIX)
        shutil.copy2(compiler, msvc)
        for driver in (compiler, msvc):
            case = root / f"case-{driver.stem}"
            case.mkdir()
            _check_case(exe, driver, case)


def _check_case(exe, compiler, base):
    home = base / "home"
    # A live progress line on the pipe as well, which says what a build waits for.
    env = dict(os.environ, GOOSE_HOME=str(home), GOOSE_CACHE_TEST=str(base),
               GOOSE_PROGRESS="live", COLUMNS="200")
    pkg = base / "pkgs" / "shared"
    pkg.mkdir(parents=True)
    tc.write_text(pkg / "shared.goose", "namespace shared;\nextern fn shared_one() -> i64;\n"
                                        "fn one() -> i64 { shared_one() }\n")
    tc.write_text(pkg / "shared.c", "/* What the controlled compiler compiles. */\n")
    tc.write_text(pkg / "shared.build", 'c_sources = ["shared.c"];\n')

    def start(name):
        source = base / f"{name}.goose"
        tc.write_text(source, 'import std;\nimport shared from "./pkgs/shared";\n'
                              "fn main() { print(shared::one()); }\n")
        argv = [str(a) for a in (exe, "build", "--cc", compiler, "-o",
                                 base / (name + tc.EXE_SUFFIX), source)]
        p = subprocess.Popen(argv, cwd=base, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
        # What it printed so far, read as it comes.
        p.text = []

        def read():
            for chunk in iter(lambda: p.stdout.read1(4096), b""):
                p.text.append(tc.decode(chunk))
        p.reader = threading.Thread(target=read, daemon=True)
        p.reader.start()
        return p

    def require(good, message):
        if not good:
            raise RuntimeError(f"native part cache ({base.name}): {message}")

    def compiles():
        """The objects the controlled compiler was asked for, by its markers."""
        return [m.read_text() for m in base.glob("started-*")]

    def package_compiles():
        return [c for c in compiles() if "/build/pkg/" in c.replace("\\", "/")]

    def wait_for(predicate, message):
        deadline = time.monotonic() + 20
        while not predicate():
            require(time.monotonic() < deadline, message)
            time.sleep(0.01)

    workers = []
    try:
        workers.append(start("first"))
        wait_for(lambda: package_compiles(), "the package's C did not start compiling")
        for i in range(3):
            workers.append(start(f"concurrent_{i}"))
        # Each waits for the first build, unless it compiles the C itself.
        wait_for(lambda: len(package_compiles()) > 1 or
                 all(WAITING in "".join(w.text) for w in workers[1:]),
                 "the builders neither waited for the package nor compiled it")
        (base / "release").touch()
        for w in workers:
            w.wait(timeout=60)
            w.reader.join(timeout=60)
            require(w.returncode == 0, "".join(w.text))
    finally:
        (base / "release").touch()
        for w in workers:
            if w.poll() is None:
                w.kill()
            w.wait()
    built = package_compiles()
    require(len(built) == 1, f"concurrent builds compiled the package's C {len(built)} times")
    for name in ["first"] + [f"concurrent_{i}" for i in range(3)]:
        require((base / (name + tc.EXE_SUFFIX)).read_text() == "linked",
                f"{name} did not link")
    before = len(compiles())
    warm = start("warm")
    warm.wait(timeout=60)
    warm.reader.join(timeout=60)
    require(warm.returncode == 0, "".join(warm.text))
    require(len(compiles()) == before, "a warm build compiled again")
