"""Data stacks on Windows, which commit memory as they grow: a program that
grows one past what the system will commit, here a job object's limit on
what its processes commit, is told it is out of memory, rather than that the
stack overran the address space it reserved."""
import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path

import toolchain as tc

PROGRAM = """\
import std;

fn main() {
    var s: u8[>..] = [];
    for i in 400000000 { s.push(1); }
    print(s.len);
}
"""

LIMIT = 100 << 20


def run_limited(app):
    """Runs `app` in a job object whose processes commit at most LIMIT bytes:
    (exit code, stderr)."""
    from ctypes import wintypes

    class Basic(ctypes.Structure):
        _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64),
                    ("PerJobUserTimeLimit", ctypes.c_int64), ("LimitFlags", wintypes.DWORD),
                    ("MinimumWorkingSetSize", ctypes.c_size_t),
                    ("MaximumWorkingSetSize", ctypes.c_size_t),
                    ("ActiveProcessLimit", wintypes.DWORD), ("Affinity", ctypes.c_size_t),
                    ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD)]

    class Extended(ctypes.Structure):
        _fields_ = [("Basic", Basic), ("IoInfo", ctypes.c_ulonglong * 6),
                    ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                    ("PeakProcessMemoryUsed", ctypes.c_size_t),
                    ("PeakJobMemoryUsed", ctypes.c_size_t)]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    ntdll = ctypes.WinDLL("ntdll")
    kernel32.CreateJobObjectW.restype = wintypes.HANDLE
    job = kernel32.CreateJobObjectW(None, None)
    info = Extended()
    info.Basic.LimitFlags = 0x100           # JOB_OBJECT_LIMIT_PROCESS_MEMORY
    info.ProcessMemoryLimit = LIMIT
    if not job or not kernel32.SetInformationJobObject(
            wintypes.HANDLE(job), 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise RuntimeError(f"commit limit: no job object (error {ctypes.get_last_error()})")
    try:
        # Started suspended, so that it commits nothing outside the job.
        p = subprocess.Popen([str(app)], creationflags=0x4, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        handle = wintypes.HANDLE(int(p._handle))
        if not kernel32.AssignProcessToJobObject(wintypes.HANDLE(job), handle):
            p.kill()
            p.communicate()
            raise RuntimeError(f"commit limit: cannot put the program in a job object "
                               f"(error {ctypes.get_last_error()})")
        ntdll.NtResumeProcess(handle)
        _, err = p.communicate(timeout=120)
        return p.returncode, tc.decode(err)
    finally:
        kernel32.CloseHandle(wintypes.HANDLE(job))


def check_commit_limit(exe, base):
    """Returns why it was skipped, or None when the check ran; raises
    RuntimeError on a failure."""
    if sys.platform != "win32":
        return "not Windows"
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="commit-limit-", dir=base) as tmp:
        root = Path(tmp)
        source = root / "grows.goose"
        tc.write_text(source, PROGRAM)
        app = root / "grows.exe"
        r = subprocess.run([str(exe), "build", "-o", str(app), str(source)], cwd=root,
                           capture_output=True, text=True, errors="replace", timeout=300)
        if r.returncode != 0:
            raise RuntimeError(f"commit limit: goose build:\n{r.stdout}{r.stderr}")
        code, err = run_limited(app)
        if code != 1 or "out of memory committing a data stack" not in err:
            raise RuntimeError(f"commit limit: a program committing past {LIMIT >> 20} MB "
                               f"ended with {code}:\n{err}")
    return None
