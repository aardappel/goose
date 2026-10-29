"""Shared native, standalone TinyCC, and end-to-end JIT pool measurements."""
import hashlib
import ctypes
import os
import shutil
import subprocess
import time
from pathlib import Path


class TinyCC:
    name = "tcc"

    def __init__(self, executable, library):
        self.executable, self.library = executable, library
        self.desc = str(executable)

    def compile(self, source, output, opt=2):
        argv = [str(self.executable)]
        if self.library:
            argv += ["-B", str(self.library), "-I", str(self.library / "include"),
                     "-I", str(self.library / "include/winapi")]
        p = subprocess.run([*argv, str(source), "-o", str(output)],
                           capture_output=True, text=True, timeout=180)
        return p.returncode == 0, p.stdout + p.stderr


class JIT:
    name = "jit"
    desc = "Goose compile + TinyCC JIT + execution"


def compilers(tc, goose, names):
    available = tc.find_ccs()
    build = goose.parent.parent if goose.parent.name in ("Release", "Debug", "RelWithDebInfo") else goose.parent
    tcc = build / "tcc" / ("tcc" + tc.EXE_SUFFIX)
    library = build / "tcclib"
    if not tcc.is_file():
        found = shutil.which("tcc")
        tcc, library = (Path(found), None) if found else (None, None)
    if tcc:
        available["tcc"] = TinyCC(tcc, library)
    available["jit"] = JIT()
    missing = set(names) - available.keys()
    if missing:
        raise RuntimeError("Unavailable pool benchmark compiler(s): " + ", ".join(sorted(missing)))
    return {name: available[name] for name in names}


def prepare(compiler, source, binary, goose, level, metric):
    if compiler.name == "jit":
        return [str(goose), f"-O{level}", "--jit", str(source.with_suffix(".goose"))], dict(
            metric, timing="compile-and-run", c_policy="unmodified JIT output", exe_bytes=None)
    start = time.perf_counter()
    ok, log = compiler.compile(source, binary, opt=2)
    compile_seconds = time.perf_counter() - start
    binary.with_suffix(".cc.log").write_text(log, encoding="utf-8")
    if not ok:
        raise RuntimeError(log)
    return [str(binary)], dict(metric, timing="execution", cc_seconds=compile_seconds,
                               exe_bytes=binary.stat().st_size,
                               exe_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())


def measure(argv, *, cwd=None, log=None):
    start = time.perf_counter()
    with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=cwd) as p:
        try:
            stdout, stderr = p.communicate(timeout=180)
        except subprocess.TimeoutExpired:
            p.kill()
            p.communicate()
            raise
        seconds = time.perf_counter() - start
        if log is not None:
            Path(log).write_text(stdout + stderr, encoding="utf-8")
        if p.returncode:
            raise RuntimeError(stdout + stderr)
        cycles = None
        if os.name == "nt":
            value = ctypes.c_ulonglong()
            query = ctypes.WinDLL("kernel32", use_last_error=True).QueryProcessCycleTime
            query.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulonglong)]
            if not query(int(p._handle), ctypes.byref(value)):
                raise ctypes.WinError(ctypes.get_last_error())
            cycles = value.value
    return seconds, stdout, cycles
