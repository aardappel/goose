"""The build driver's shared runtime cache: concurrent writers and failures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

import toolchain as tc


def check_runtime_cache(exe, cc, base):
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="runtime-cache-", dir=base) as tmp:
        root = Path(tmp)
        compiler = root / ("cache-cc" + tc.EXE_SUFFIX)
        ok, log = cc.compile(Path(__file__).with_name("runtime_cache_cc.c"), compiler)
        if not ok:
            raise RuntimeError(log)
        # A clang-cl basename selects MSVC flag spelling without importing
        # Visual Studio's environment. The probe understands both styles.
        msvc = root / ("clang-cl" + tc.EXE_SUFFIX)
        shutil.copy2(compiler, msvc)
        for driver in (compiler, msvc):
            for debug in (False, True):
                case = root / f"{driver.stem}-{'debug' if debug else 'release'}"
                case.mkdir()
                _check_case(exe, driver, case, debug)


def _check_case(exe, compiler, base, debug):
    home = base / "home"
    env = dict(os.environ, GOOSE_HOME=str(home), GOOSE_CACHE_TEST=str(base))
    flags = ["--debug"] if debug else []

    def command(name, extra=()):
        source = base / f"{name}.goose"
        tc.write_text(source, 'fn main() { print("cache"); }\n')
        return [str(arg) for arg in (exe, "build", "--cc", compiler, *flags, *extra,
                                    "-o", base / (name + tc.EXE_SUFFIX), source)]

    def run(name, extra=()):
        return subprocess.run(command(name, extra), cwd=base, env=env,
                              capture_output=True, text=True, errors="replace", timeout=30)

    def require(good, message):
        if not good:
            raise RuntimeError(f"runtime cache ({base.name}): {message}")

    def cached_objects():
        return list(home.glob("build/runtime/*/*/goose_runtime.o")) + \
               list(home.glob("build/runtime/*/*/goose_runtime.obj"))

    def wait_for(predicate, message):
        deadline = time.monotonic() + 20
        while not predicate():
            require(time.monotonic() < deadline, message)
            time.sleep(0.01)

    dry = run("dry", ["--dry-run"])
    require(dry.returncode == 0 and "goose_runtime" in dry.stdout + dry.stderr,
            dry.stdout + dry.stderr)
    require(not (home / "build/runtime").exists(), "dry run changed the runtime cache")

    (base / "fail").touch()
    failed = run("failed")
    require(failed.returncode != 0 and
            "intentional runtime compile failure" in failed.stdout + failed.stderr,
            failed.stdout + failed.stderr)
    require(not cached_objects(), "a failed compile published an object")
    logs = list(home.glob("build/runtime/*/*/cc.log"))
    require(len(logs) == 1 and "intentional runtime compile failure" in logs[0].read_text(),
            "failed compile did not preserve its diagnostic log")
    (base / "fail").unlink()
    before = set(base.glob("started-*"))

    workers = []
    try:
        # The first retry leaves a partial object on disk while the other
        # builders start. Neither a cache hit nor a second writer may use it.
        workers.append(subprocess.Popen(command("concurrent_0"), cwd=base, env=env,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT))
        wait_for(lambda: set(base.glob("started-*")) != before,
                 "runtime compiler did not start")
        require(not cached_objects(), "an unfinished object became a cache hit")
        for i in range(1, 4):
            workers.append(subprocess.Popen(command(f"concurrent_{i}"), cwd=base, env=env,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT))
        wait_for(lambda: len(list(home.glob("build/prog/*/concurrent_*.c"))) == 4,
                 "concurrent builders did not generate their programs")
        (base / "release").touch()
        for worker in workers:
            output, _ = worker.communicate(timeout=30)
            require(worker.returncode == 0, tc.decode(output))
    finally:
        (base / "release").touch()
        for worker in workers:
            if worker.poll() is None:
                worker.kill()
            worker.communicate()

    require(len(set(base.glob("started-*")) - before) == 1,
            "concurrent builders compiled the same runtime more than once")
    objects = cached_objects()
    require(len(objects) == 1 and objects[0].read_text() == "complete",
            "runtime object was not published after compilation")
    for i in range(4):
        require((base / (f"concurrent_{i}" + tc.EXE_SUFFIX)).read_text() == "linked",
                "a concurrent program did not link")
    stamp = objects[0].stat().st_mtime_ns
    warm = run("warm")
    require(warm.returncode == 0, warm.stdout + warm.stderr)
    require(objects[0].stat().st_mtime_ns == stamp and
            len(set(base.glob("started-*")) - before) == 1,
            "a warm build recompiled the runtime")
