"""Toolchain discovery and process measurement, shared by the runner scripts.

test/run_tests.py, samples/run_samples.py, bench/run_bench.py and
bench/bce_ab.py all need the same four things: the goose binary wherever CMake
put it, a C and a C++ compiler, a way to run what was built and read back what
it printed, and -- for the benchmarks -- the peak memory of that run. MSVC and
the gcc-style drivers disagree about the spelling of nearly every flag and the
two operating system families disagree about how to ask for a process's peak
memory, so both are wrapped here and the runners are written in terms of
intent.
"""

import ctypes
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path

IS_WINDOWS = os.name == "nt"
IS_MACOS = sys.platform == "darwin"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""
REPO_ROOT = Path(__file__).resolve().parent.parent


def setup_console():
    """Decode and encode everything as UTF-8, so a test whose output holds a
    non-ASCII string literal survives being printed and compared on a machine
    whose locale is not UTF-8 (Windows consoles usually are not)."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


def decode(data):
    """Native output as text, with CRLF folded to LF so a comparison does not
    depend on which C runtime wrote it."""
    if isinstance(data, bytes):
        data = data.decode("utf-8", errors="replace")
    return data.replace("\r\n", "\n")


def num(v, places=1):
    """A number as the reports show it: grouped, fixed decimals, and halves
    rounded away from zero. Python's own float formatting rounds halves to
    even, which would print a measured 20.95 as 20.9."""
    q = Decimal(repr(float(v))).quantize(Decimal(1).scaleb(-places), rounding=ROUND_HALF_UP)
    return f"{q:,f}"


def write_text(path, text):
    """UTF-8 without a byte order mark and with the newlines the text already
    has: the generated C, the blessed outputs and the reports are all read
    back by tools that would choke on a BOM or on a stray CR."""
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


# --- the goose compiler ------------------------------------------------------

def find_goose(explicit=None):
    if explicit:
        p = Path(explicit)
        if not p.exists():
            sys.exit(f"goose compiler not found: {p}")
        # Absolute, because the runners start programs in other directories.
        return p.resolve()
    # Single-config CMake generators put the binary straight in the build
    # directory; the Visual Studio and Xcode generators put it in a
    # per-configuration subdirectory.
    for sub in ("Debug", "", "Release", "RelWithDebInfo"):
        p = REPO_ROOT / "build" / sub / ("goose" + EXE_SUFFIX)
        if p.exists():
            return p
    sys.exit("no goose binary under build/ -- build it first, or pass --exe")


def have_jit(exe):
    """Whether this compiler was built with the TinyCC backend, answered by
    running a one-line program through it. That also proves the support
    library CMake staged alongside it is where the compiler looks for it,
    which no build-time flag could tell us."""
    probe = REPO_ROOT / "build" / "jitprobe.goose"
    probe.parent.mkdir(parents=True, exist_ok=True)
    write_text(probe, "fn main() { print(7); }\n")
    code, out, _ = run_capture([exe, "--jit", probe])
    return code == 0 and out.strip() == "7"


# The compiler's own wording for a program the TinyCC backend cannot run yet.
# The runners report those as skips rather than failures, so the coverage
# returns by itself once the backend grows the feature.
JIT_UNSUPPORTED = "JIT mode does not support"


# --- C and C++ toolchains ----------------------------------------------------

_vs_root = None
_vs_probed = False


def msvc_root():
    """The newest installed Visual Studio, with its vcvars64 environment
    imported into this process the first time it is asked for. Both cl and the
    bundled clang-cl need that environment for headers, libraries and the CRT.
    None when this is not Windows or no Visual Studio is installed."""
    global _vs_root, _vs_probed
    if _vs_probed:
        return _vs_root
    _vs_probed = True
    if not IS_WINDOWS:
        return None
    vswhere = (Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
               / "Microsoft Visual Studio" / "Installer" / "vswhere.exe")
    if not vswhere.exists():
        return None
    r = subprocess.run([str(vswhere), "-latest", "-prerelease",
                        "-property", "installationPath"],
                       capture_output=True, text=True, errors="replace")
    lines = [l for l in r.stdout.splitlines() if l.strip()]
    if r.returncode != 0 or not lines:
        return None
    root = Path(lines[0].strip())
    vcvars = root / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not vcvars.exists():
        return None
    # As one command line rather than an argument list: cmd.exe does not read
    # the backslash-escaped quotes that the list form would produce.
    env = subprocess.run(f'"{vcvars}" >nul 2>&1 && set', shell=True,
                         capture_output=True, text=True, errors="replace")
    for line in env.stdout.splitlines():
        key, sep, val = line.partition("=")
        if sep:
            os.environ[key] = val
    _vs_root = root
    return root


@dataclass
class CC:
    """One C/C++ toolchain, addressed by intent rather than by flag spelling.

    `cc` and `cxx` are the drivers for the two languages: the same executable
    under MSVC, which switches on the source extension, and a pair under the
    gcc-style drivers, where the C driver would link a C++ program without its
    standard library. `cxx` is None where only the C driver is installed, which
    is enough for everything except the C++ benchmark rows."""
    name: str
    cc: str
    cxx: str
    style: str       # "msvc" or "gcc"
    desc: str

    def compile(self, sources, out, *, opt=None, defines=(), cpp=False,
                warn="default", strict_decls=False, extra=(), log=None):
        """Compile and link `sources` into the executable `out`. Returns
        (ok, combined output), and writes that output to `log` when given."""
        if cpp and not self.cxx:
            return False, f"no C++ compiler alongside {self.cc}\n"
        if isinstance(sources, (str, Path)):
            sources = [sources]
        sources = [str(s) for s in sources]
        out = Path(out)
        argv = [self.cxx if cpp else self.cc]
        if self.style == "msvc":
            argv.append("/nologo")
            if opt is not None:
                argv.append({0: "/Od", 1: "/O1", 2: "/O2"}[opt])
            argv.append("/W3" if warn == "default" else "/w")
            if cpp:
                argv += ["/EHsc", "/std:c++20"]
            argv += [f"/D{d}" for d in defines]
            argv += list(extra) + sources
            argv += [f"/Fe:{out}", f"/Fo:{out.with_suffix('.obj')}"]
        else:
            if opt is not None:
                argv.append(f"-O{opt}")
            if warn != "default":
                argv.append("-w")
            # Calling a function that was never declared is valid pre-C99 and a
            # link error waiting to happen; MSVC has no equivalent it will fail
            # on, which is why the second compiler exists in the test runner.
            if strict_decls:
                argv.append("-Werror=implicit-function-declaration")
            if cpp:
                argv.append("-std=c++20")
            argv += [f"-D{d}" for d in defines]
            argv += list(extra) + sources
            argv += ["-o", str(out)]
            # The runtime uses threads and libm; on Windows both live in the
            # CRT the driver links anyway, and asking for them by name fails.
            if not IS_WINDOWS:
                argv += ["-pthread", "-lm"]
        r = subprocess.run(argv, capture_output=True, text=True, errors="replace")
        output = (r.stdout or "") + (r.stderr or "")
        if log:
            write_text(log, output)
        return r.returncode == 0, output


def _first_line(argv, stderr_too=False):
    try:
        r = subprocess.run(argv, capture_output=True, text=True, errors="replace")
    except OSError:
        return ""
    text = r.stdout if not stderr_too else (r.stderr or "") + (r.stdout or "")
    lines = [l for l in text.splitlines() if l.strip()]
    return lines[0].strip() if lines else ""


def _version_of(line):
    m = re.search(r"version\s+([\w.+-]+)", line, re.IGNORECASE)
    if m:
        return m.group(1)
    return line.split()[-1] if line else "unknown"


def _toolset_name(version):
    """The MSVC toolset's platform name, which is what the benchmark notes and
    the saved measurements call that column. It does not track the toolset
    version directly: VS2022's 14.30 through 14.4x are all v143, and VS2026's
    14.5x is v145."""
    parts = version.split(".")
    if len(parts) >= 2 and parts[0] == "14" and parts[1].isdigit():
        minor = int(parts[1])
        for floor, name in ((50, "v145"), (30, "v143"), (20, "v142"), (10, "v141")):
            if minor >= floor:
                return name
    return "msvc"


def _is_real_gcc(path):
    """On macOS `gcc` is a clang shim, and running the same compiler twice
    under two names would make the toolchain comparison meaningless."""
    return "clang" not in _first_line([path, "--version"]).lower()


def find_ccs():
    """Every usable C/C++ toolchain, in the order the reports list them.

    On Windows those are the two that live in the Visual Studio install, `cl`
    (under its toolset's name, v145 for VS2026) and the bundled clang-cl, so
    that both build against the same headers, libraries and CRT. Otherwise they
    are gcc and clang from PATH."""
    found = {}
    root = msvc_root()
    if root:
        toolset = os.environ.get("VCToolsVersion", "?")
        name = _toolset_name(toolset)
        # Use the compiler imported by vcvars explicitly. A parent process can
        # retain a different PATH spelling on Windows; advertising a bare `cl`
        # that subprocess cannot resolve turns discovery into a later crash.
        vc_tools = Path(os.environ.get("VCToolsInstallDir", root / "VC" / "Tools" / "MSVC" / toolset))
        clpath = vc_tools / "bin" / "Hostx64" / "x64" / "cl.exe"
        cl = str(clpath) if clpath.exists() else shutil.which("cl")
        if cl:
            ver = _version_of(_first_line([cl], stderr_too=True))
            found[name] = CC(name, cl, cl, "msvc",
                             f"MSVC {ver} (toolset {toolset})")
        clangcl = root / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang-cl.exe"
        if clangcl.exists():
            ver = _version_of(_first_line([str(clangcl), "--version"]))
            found["clang"] = CC("clang", str(clangcl), str(clangcl), "msvc",
                                f"clang-cl {ver} (bundled with VS)")
        return found
    for name, ccname, cxxname in (("gcc", "gcc", "g++"), ("clang", "clang", "clang++")):
        # The C++ driver is optional: only the benchmark suite's C++ rows need
        # it, and a machine with just cc can still build and run everything the
        # compiler generates.
        cc, cxx = shutil.which(ccname), shutil.which(cxxname)
        if not cc:
            continue
        if name == "gcc" and not _is_real_gcc(cc):
            continue
        ver = _version_of(_first_line([cc, "--version"]))
        found[name] = CC(name, cc, cxx, "gcc", f"{name} {ver}")
    if not found and IS_WINDOWS:
        sys.stderr.write("no Visual Studio found; C compilation is unavailable\n")
    return found


def find_clang_c():
    """A gcc-style clang driver, or None. The test runner uses it as a second
    C front end over the generated C; clang-cl would not do, because the flag
    that makes the check worth running (-Werror=implicit-function-declaration)
    has no MSVC-style spelling. On Windows this is the clang that ships inside
    Visual Studio unless PATH has its own, and it needs the vcvars environment
    (imported by msvc_root) to link."""
    p = shutil.which("clang")
    if not p:
        root = msvc_root()
        if root:
            inside = root / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang.exe"
            if inside.exists():
                p = str(inside)
    if not p:
        return None
    ver = _version_of(_first_line([p, "--version"]))
    return CC("clang", p, shutil.which("clang++") or p, "gcc", f"clang {ver}")


def test_cc(name=None):
    """Select a test backend; an explicitly requested one must be available."""
    found = find_ccs()
    if name in (None, "native"):
        cc = next(iter(found.values()), None)
    elif name == "clang":
        cc = find_clang_c()
    elif name == "msvc":
        cc = next((c for c in found.values()
                   if c.style == "msvc" and c.name != "clang"), None)
    else:
        cc = found.get(name)
    if name and cc is None:
        sys.exit(f"requested C toolchain is unavailable: {name}")
    return cc


# Packed Goose fields deliberately use unaligned scalar accesses. Keep the
# remaining UBSan checks, and make every finding fatal, including in programs
# which are themselves expected to abort. ASan still checks the compiler and
# runtime's C allocations; it cannot see logical object boundaries inside a
# Goose virtual-memory arena.
SANITIZER_FLAGS = ("-fsanitize=address,undefined", "-fno-sanitize=alignment",
                   "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-g")


def sanitizer_failure(stderr):
    """Do not mistake a sanitizer crash for an expected language error."""
    return bool(re.search(r"AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|"
                          r"(?m:^.*?:\d+:\d+: runtime error:)", stderr))


def find_rustc():
    """rustup installs into ~/.cargo/bin and puts it on the PATH of shells
    started afterwards, which is not necessarily this one."""
    p = shutil.which("rustc")
    if p:
        return Path(p)
    p = Path.home() / ".cargo" / "bin" / ("rustc" + EXE_SUFFIX)
    return p if p.exists() else None


# --- running what was built --------------------------------------------------

@dataclass
class RunResult:
    ms: float
    peak: int
    out: str
    err: str
    code: int


class _PMC(ctypes.Structure):
    _fields_ = [("cb", ctypes.c_uint32), ("PageFaultCount", ctypes.c_uint32),
                ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]


def _peak_windows(handle):
    try:
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
    except OSError:
        return 0
    info = _PMC()
    info.cb = ctypes.sizeof(_PMC)
    if not psapi.GetProcessMemoryInfo(ctypes.c_void_p(handle),
                                      ctypes.byref(info), info.cb):
        return 0
    return int(info.PeakWorkingSetSize)


def run_measured(exe, args=(), *, stdin_path=None):
    """Run `exe` once and report wall time, peak memory, output and exit code.

    Output goes through files rather than pipes: it is read back whole anyway,
    and on the POSIX side the process is spawned directly so that wait4 can
    hand back that one child's resource usage. Peak memory has to be asked for
    while the process is still a handle we hold -- Windows reports zero for it
    once the process is gone, which is exactly when we want to ask."""
    exe = Path(exe)
    outf, errf = exe.parent / (exe.name + ".stdout"), exe.parent / (exe.name + ".stderr")
    argv = [str(exe)] + [str(a) for a in args]
    if IS_WINDOWS:
        stdin_f = open(stdin_path, "rb") if stdin_path else subprocess.DEVNULL
        try:
            with open(outf, "wb") as fo, open(errf, "wb") as fe:
                start = time.perf_counter()
                p = subprocess.Popen(argv, stdin=stdin_f, stdout=fo, stderr=fe)
                p.wait()
                ms = (time.perf_counter() - start) * 1000.0
                # Popen keeps the process handle open until it is collected, so
                # the peak is still readable here but not after this scope.
                peak = _peak_windows(int(p._handle))
                code = p.returncode
        finally:
            if stdin_path:
                stdin_f.close()
    else:
        actions = [
            (os.POSIX_SPAWN_OPEN, 1, str(outf), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644),
            (os.POSIX_SPAWN_OPEN, 2, str(errf), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644),
            (os.POSIX_SPAWN_OPEN, 0, str(stdin_path) if stdin_path else os.devnull,
             os.O_RDONLY, 0o644),
        ]
        start = time.perf_counter()
        pid = os.posix_spawn(str(exe), argv, os.environ, file_actions=actions)
        _, status, usage = os.wait4(pid, 0)
        ms = (time.perf_counter() - start) * 1000.0
        # ru_maxrss is bytes on the BSDs, kilobytes on Linux.
        peak = usage.ru_maxrss * (1 if IS_MACOS else 1024)
        code = os.waitstatus_to_exitcode(status)
    return RunResult(ms=ms, peak=peak, out=decode(outf.read_bytes()).strip(),
                     err=decode(errf.read_bytes()).strip(), code=code)


def run_capture(argv, cwd=None, stdin_path=None):
    """Run a tool, returning (exit code, stdout, stderr) as LF-normalised
    text. Used where the output is a result rather than something to time."""
    argv = [str(a) for a in argv]
    stdin_f = open(stdin_path, "rb") if stdin_path else None
    try:
        r = subprocess.run(argv, cwd=cwd, stdin=stdin_f, capture_output=True)
    finally:
        if stdin_f:
            stdin_f.close()
    return r.returncode, decode(r.stdout), decode(r.stderr)


# --- machine description -----------------------------------------------------

def cpu_name():
    if IS_WINDOWS:
        try:
            import winreg
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                                 r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
            with key:
                return winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
        except OSError:
            pass
    elif IS_MACOS:
        line = _first_line(["sysctl", "-n", "machdep.cpu.brand_string"])
        if line:
            return line
    else:
        try:
            for line in Path("/proc/cpuinfo").read_text().splitlines():
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
        except OSError:
            pass
    return platform.processor() or platform.machine()


def ram_bytes():
    if IS_WINDOWS:
        class MemStatus(ctypes.Structure):
            _fields_ = [("dwLength", ctypes.c_uint32), ("dwMemoryLoad", ctypes.c_uint32),
                        ("ullTotalPhys", ctypes.c_uint64), ("ullAvailPhys", ctypes.c_uint64),
                        ("ullTotalPageFile", ctypes.c_uint64), ("ullAvailPageFile", ctypes.c_uint64),
                        ("ullTotalVirtual", ctypes.c_uint64), ("ullAvailVirtual", ctypes.c_uint64),
                        ("ullAvailExtendedVirtual", ctypes.c_uint64)]
        st = MemStatus()
        st.dwLength = ctypes.sizeof(MemStatus)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(st)):
            return int(st.ullTotalPhys)
        return 0
    try:
        return os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE")
    except (ValueError, OSError):
        line = _first_line(["sysctl", "-n", "hw.memsize"])
        return int(line) if line.isdigit() else 0


def os_name():
    if IS_WINDOWS:
        return f"Windows {platform.release()} {platform.version()} ({platform.machine()})"
    if IS_MACOS:
        return f"macOS {platform.mac_ver()[0]} ({platform.machine()})"
    return f"{platform.system()} {platform.release()} ({platform.machine()})"
