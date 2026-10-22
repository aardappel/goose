"""Extern blocks bound from a shared library at run time (spec §7.10,
docs/design/dynamic_externs.md): a library built here is found by the
derived spelling of its name, relative to the program's resource directory
or on the loader's search path, and called through every parameter shape
an extern takes, under the JIT and in a built program alike. A library or
a symbol that is missing is a runtime error naming it, unless nothing
calls the declaration; a package whose extern is dynamic needs no native
part, and one with both kinds still links its own. The sidecar's dylibs
lists map a name to the spelling each platform opens, the program's over a
package's, and refuse a malformed entry or a name no block declares."""
import os
import shutil
import subprocess
from pathlib import Path

import toolchain as tc

LIB_SUFFIX = ".dll" if tc.IS_WINDOWS else ".dylib" if tc.IS_MACOS else ".so"
LIB_PREFIX = "" if tc.IS_WINDOWS else "lib"

LIBRARY_C = '''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "goose.h"
#ifdef _WIN32
#define DLT __declspec(dllexport)
#else
#define DLT
#endif

typedef struct { int32_t x, y; } Point;
typedef struct { int32_t *data; int64_t len; } sl_i32;
typedef struct { uint8_t *data; int64_t len; } sl_u8;

DLT int32_t dlt_add(int32_t a, int32_t b) { return a + b; }
DLT double dlt_scale(double x, float f) { return x * f; }
DLT Point dlt_swap(Point p) { Point r = { p.y, p.x }; return r; }
DLT void dlt_bump(Point *p) { p->x++; p->y++; }
DLT int64_t dlt_sum(sl_i32 xs) {
    int64_t s = 0;
    for (int64_t i = 0; i < xs.len; i++) s += xs.data[i];
    return s;
}
DLT void dlt_greet(gs_rref out, sl_u8 name) {
    gs_bld_append(out, "hello, ", 7);
    gs_bld_append(out, name.data, name.len);
}
/* A handle: an object the library owns, crossing as a u64 (design §6). */
DLT uint64_t dlt_new(int32_t v) { int32_t *p = malloc(sizeof *p); *p = v; return (uint64_t)(uintptr_t)p; }
DLT int32_t dlt_get(uint64_t h) { return *(int32_t *)(uintptr_t)h; }
DLT void dlt_free(uint64_t h) { free((void *)(uintptr_t)h); }
/* Memory the library owns, read through ffi (stdlib/ffi.goose). */
DLT uint64_t dlt_name(void) { return (uint64_t)(uintptr_t)"dltest"; }
DLT uint64_t dlt_null(void) { return 0; }
'''

# The block, with the library's spelling to fill in; the last member has
# no implementation, which an unused extern never needs.
BLOCK = '''
import ffi;
struct Point { x: i32, y: i32 }
extern "%s" {
    fn add(a: i32, b: i32) -> i32 = "dlt_add";
    fn scale(x: f64, f: f32) -> f64 = "dlt_scale";
    fn swap(p: Point) -> Point = "dlt_swap";
    fn bump(p: Point&) = "dlt_bump";
    fn sum(xs: const i32[:]) -> i64 = "dlt_sum";
    fn greet(out: u8[>..]&, name: const u8[:]) = "dlt_greet";
    fn new_counter(v: i32) -> u64 = "dlt_new";
    fn get(h: u64) -> i32 = "dlt_get";
    fn free_counter(h: u64) = "dlt_free";
    fn name() -> u64 = "dlt_name";
    fn null_ptr() -> u64 = "dlt_null";
    fn unused() = "dlt_unused";
}
'''

MAIN = '''
fn main() {
    print(add(40, 2));
    print(scale(1.5, 2.0));
    let p = swap(Point { 1, 2 });
    print(p.x, " ", p.y);
    var q = Point { 10, 20 };
    bump(q);
    print(q.x, " ", q.y);
    let xs: i32[4] = [1, 2, 3, 4];
    print(sum(xs));
    var s: u8[>..] = [];
    greet(s, "goose");
    print(s);
    let h = new_counter(7);
    print(get(h));
    free_counter(h);
    print(ffi::cstr(name()));
    var head: u8[>..] = [];
    print(ffi::bytes(name(), 2, head), " ", head);
    var none: u8[>..] = [];
    print(ffi::cstr(null_ptr(), none), " ", none.len, " ", ffi::bytes(null_ptr(), 3).len);
}
'''

EXPECTED = ["42", "3.0", "2 1", "11 21", "10", "hello, goose", "7", "dltest", "true dl",
            "false 0 0"]


def check_extern_dylib(exe, cc, base, *, jit, runtime, extra=()):
    """Raises RuntimeError on a failure. `cc` None skips the built form."""
    base = (Path(base) / "extern-dylib").resolve()
    libdir = base / "lib"
    libdir.mkdir(parents=True, exist_ok=True)
    if not cc:
        return  # The library has to be built by something.
    tc.write_text(base / "dltest.c", LIBRARY_C)
    lib = libdir / (LIB_PREFIX + "dltest" + LIB_SUFFIX)
    ok, log = cc.compile_shared(base / "dltest.c", lib, opt=1, extra=extra,
                                includes=[tc.REPO_ROOT / "src" / "runtime"])
    if not ok:
        raise RuntimeError(f"building the test library:\n{log}")

    def run(argv, cwd=base, env=None):
        full = dict(os.environ)
        full.update(env or {})
        r = subprocess.run([str(a) for a in argv], cwd=cwd, env=full, capture_output=True,
                           text=True, errors="replace", timeout=600)
        if tc.sanitizer_failure(r.stderr):
            raise RuntimeError(r.stdout + r.stderr)
        return r.returncode, r.stdout, r.stderr

    def build(source, level=0):
        """The C `goose -o` writes, compiled with the runtime object: the
        executable, beside the source as `goose build` would put it, so
        the two forms share the resource directory the library is under."""
        cfile = source.with_suffix(f".{level}.c")
        code, out, err = run([exe, f"-O{level}", "-o", cfile, source])
        if code:
            raise RuntimeError(f"{source.name} -O{level}:\n{out}{err}")
        binary = source.with_name(f"{source.stem}-{level}{tc.EXE_SUFFIX}")
        ok, log = cc.compile(cfile, binary, opt=level if not extra else 1, extra=extra,
                             strict_decls=True, runtime=runtime)
        if not ok:
            raise RuntimeError(f"{source.name} -O{level}:\n{log}")
        return binary, cfile.read_text(encoding="utf-8")

    def both(source, env=None):
        """Runs the program under the JIT and built, at -O0 and -O2, each
        yielding (how, code, out, err)."""
        for level in (0, 2):
            if jit:
                yield (f"jit -O{level}", *run([exe, f"-O{level}", "--jit", source], env=env))
            binary, _ = build(source, level)
            yield (f"built -O{level}", *run([binary], env=env))

    def expect_output(source, env=None, lines=EXPECTED):
        for how, code, out, err in both(source, env):
            if code != 0 or out.split("\n")[:-1] != lines:
                raise RuntimeError(f"{source.name} ({how}) exit {code}:\n{out}{err}\n"
                                   f"expected {lines}")

    def expect_failure(source, *texts):
        for how, code, out, err in both(source):
            if code == 0 or any(t not in err for t in texts):
                raise RuntimeError(f"{source.name} ({how}) exit {code}; expected a runtime "
                                   f"error naming {texts}:\n{out}{err}")

    # A spelling with a separator is relative to the resource directory: the
    # source's under the JIT, the executable's built, both `base` here.
    relative = base / "relative.goose"
    tc.write_text(relative, BLOCK % "lib/dltest" + MAIN)
    expect_output(relative)
    # The generated C is the same from one compile to the next, binds only
    # the externs a call reaches, and takes no prototype from the library.
    _, c1 = build(relative, 1)
    _, c2 = build(relative, 1)
    if c1 != c2:
        raise RuntimeError("the generated C differs between two compiles")
    if "gs_dl_bind(gs_dllibs, 1, gs_dlbinds, 11);" not in c1 or "dlt_unused" in c1:
        raise RuntimeError(f"the generated C binds other than the 11 called members:\n{c1}")
    # A bare name is the loader's to find, by its platform spelling.
    bare = base / "bare.goose"
    tc.write_text(bare, BLOCK % "dltest" + MAIN)
    pathvar = "PATH" if tc.IS_WINDOWS else "DYLD_LIBRARY_PATH" if tc.IS_MACOS else "LD_LIBRARY_PATH"
    expect_output(bare, {pathvar: str(libdir) + os.pathsep + os.environ.get(pathvar, "")})
    # The feature's point: a JIT run binds the library with no C toolchain
    # in sight, where a native part would need one.
    if jit:
        code, out, err = run([exe, "--jit", relative], env={"GOOSE_CC": str(base / "no-cc")})
        if code != 0 or out.split("\n")[:-1] != EXPECTED:
            raise RuntimeError(f"relative.goose (jit, no toolchain) exit {code}:\n{out}{err}")
    # A library that is not there, or a symbol the library lacks, is a
    # runtime error naming the spelling tried; declared but never called,
    # neither is looked for.
    nolib = base / "nolib.goose"
    tc.write_text(nolib, BLOCK % "lib/nosuch" + MAIN)
    expect_failure(nolib, f"cannot load lib/nosuch (", f"lib/{LIB_PREFIX}nosuch{LIB_SUFFIX}")
    nosym = base / "nosym.goose"
    tc.write_text(nosym, BLOCK % "lib/dltest" + 'extern "lib/dltest" { fn dlt_missing(); }\n'
                  'fn main() { dlt_missing(); }\n')
    expect_failure(nosym, f"lib/dltest (", f"{LIB_PREFIX}dltest{LIB_SUFFIX}) has no symbol dlt_missing")
    unused = base / "unused.goose"
    tc.write_text(unused, BLOCK % "lib/nosuch" + 'extern "lib/dltest" { fn dlt_missing(); }\n'
                  'fn main() { print("unused ok"); }\n')
    for how, code, out, err in both(unused):
        if code != 0 or out.strip() != "unused ok":
            raise RuntimeError(f"unused.goose ({how}) exit {code}:\n{out}{err}")
    # The sidecar's dylibs maps a name a block uses to the spelling to open,
    # on every platform or on one, where a dotted spelling is literal; the
    # program's sidecar maps a name for every package it uses, a package's
    # maps its own as defaults, and the program's entry wins for a platform
    # it says anything about.
    platform = "_windows" if tc.IS_WINDOWS else "_macos" if tc.IS_MACOS else "_linux"
    other = "_linux" if tc.IS_WINDOWS else "_windows"
    over = base / "over.goose"
    tc.write_text(over, BLOCK % "mapped" + MAIN)
    tc.write_text(base / "over.build", 'dylibs = ["mapped=lib/dltest"];\n')
    expect_output(over)
    _, c = build(over, 1)
    if '{ "mapped", "lib/dltest", "lib/dltest", "lib/dltest" }' not in c:
        raise RuntimeError(f"the generated C lacks the sidecar's spelling:\n{c}")
    literal = base / "literal.goose"
    tc.write_text(literal, BLOCK % "mapped" + MAIN)
    tc.write_text(base / "literal.build",
                  f'dylibs{platform} = ["mapped=lib/{lib.name}"];\n'
                  f'dylibs{other} = ["mapped=lib/nosuch"];\n')
    expect_output(literal)
    pkg = base / "pkgs" / "mapped"
    pkg.mkdir(parents=True, exist_ok=True)
    tc.write_text(pkg / "mapped.goose", 'namespace mapped;\n'
                  'extern "mappedlib" { fn add(a: i32, b: i32) -> i32 = "dlt_add"; }\n')
    tc.write_text(pkg / "mapped.build", 'dylibs = ["mappedlib=lib/dltest"];\n')
    use = 'import mapped from "./pkgs/mapped";\nfn main() { print(mapped::add(2, 3)); }\n'
    default = base / "pkgdefault.goose"
    tc.write_text(default, use)
    expect_output(default, lines=["5"])
    merged = base / "pkgmerged.goose"
    tc.write_text(merged, use)
    tc.write_text(base / "pkgmerged.build", f'dylibs{other} = ["mappedlib=lib/nosuch"];\n')
    expect_output(merged, lines=["5"])
    overridden = base / "pkgover.goose"
    tc.write_text(overridden, use)
    tc.write_text(base / "pkgover.build", f'dylibs{platform} = ["mappedlib=lib/nosuch"];\n')
    expect_failure(overridden, "cannot load mappedlib (", f"lib/{LIB_PREFIX}nosuch{LIB_SUFFIX}")
    # An entry that is not name=spelling, a name mapped twice, and a name no
    # block declares, the program's over every package and a package's over
    # its own, are errors at the sidecar.
    typo = base / "pkgs" / "typo"
    typo.mkdir(parents=True, exist_ok=True)
    tc.write_text(typo / "typo.goose", 'namespace typo;\nfn nothing() {}\n')
    tc.write_text(typo / "typo.build", 'dylibs = ["gone=lib/dltest"];\n')
    bad = {
        "nosep": ('dylibs = ["mapped"];\n', BLOCK % "mapped" + MAIN,
                  'nosep.build: dylibs entry "mapped" is written name=spelling'),
        "twice": (f'dylibs{platform} = ["mapped=a", "mapped=b"];\n', BLOCK % "mapped" + MAIN,
                  f"twice.build: dylibs{platform} maps mapped twice"),
        "unnamed": ('dylibs = ["mapped=lib/dltest", "other=lib/dltest"];\n', BLOCK % "mapped" + MAIN,
                    "unnamed.build: dylibs maps other, which no extern block in the program names"),
        "pkgtypo": ("", 'import typo from "./pkgs/typo";\nfn main() { typo::nothing(); }\n',
                    "typo.build: dylibs maps gone, which no extern block of the package names"),
    }
    for name, (sidecar, source, message) in bad.items():
        tc.write_text(base / f"{name}.goose", source)
        if sidecar:
            tc.write_text(base / f"{name}.build", sidecar)
        code, out, err = run([exe, "check", base / f"{name}.goose"])
        if code == 0 or message not in err:
            raise RuntimeError(f"{name}.goose: expected the error {message!r}, got exit {code}:"
                               f"\n{out}{err}")
    # A package's mapping may be for a module of it the program does not
    # import: the package's blocks are those of all its files.
    optional = base / "pkgs" / "optional"
    optional.mkdir(parents=True, exist_ok=True)
    tc.write_text(optional / "optional.goose", 'namespace optional;\nfn pure() -> i32 { 42 }\n')
    tc.write_text(optional / "extra.goose",
                  'namespace optional;\nextern "extra" { fn foreign() -> i32; }\n')
    tc.write_text(optional / "optional.build", 'dylibs = ["extra=lib/dltest"];\n')
    unimported = base / "pkgunimported.goose"
    tc.write_text(unimported, 'import optional from "./pkgs/optional";\n'
                              'fn main() { print(optional::pure()); }\n')
    expect_output(unimported, lines=["42"])
    # A package with a native part may declare both kinds: a program calling
    # its static extern builds and loads the part, one calling only its
    # dynamic extern does not need it, with no toolchain at all.
    if jit:
        pkg = base / "pkgs" / "mixed"
        (pkg / "src").mkdir(parents=True, exist_ok=True)
        tc.write_text(pkg / "mixed.goose",
                      'namespace mixed;\n'
                      'extern fn twice(x: i32) -> i32 = "mixed_twice";\n'
                      'extern "lib/dltest" { fn add(a: i32, b: i32) -> i32 = "dlt_add"; }\n')
        tc.write_text(pkg / "mixed.build", 'c_sources = ["src/mixed.c"];\n')
        tc.write_text(pkg / "src" / "mixed.c",
                      '#include <stdint.h>\nint32_t mixed_twice(int32_t x) { return x * 2; }\n')
        home = base / "home"
        shutil.rmtree(home, ignore_errors=True)  # An earlier run's builds.
        dyn = base / "mixed_dyn.goose"
        tc.write_text(dyn, 'import mixed from "./pkgs/mixed";\nfn main() { print(mixed::add(2, 3)); }\n')
        code, out, err = run([exe, "run", dyn], env={"GOOSE_HOME": str(home),
                                                     "GOOSE_CC": str(base / "no-cc")})
        if code != 0 or out.strip() != "5":
            raise RuntimeError(f"mixed_dyn.goose (goose run) exit {code}:\n{out}{err}")
        if list(home.glob("build/pkg/**/*.c*")) + list(home.glob("build/pkg/**/*.o*")):
            raise RuntimeError("a program calling only a dynamic extern built the native part")
        if tc.IS_WINDOWS and cc.style != "msvc":
            return  # goose run builds the part with the toolchain it finds, not this one.
        full = base / "mixed_both.goose"
        tc.write_text(full, 'import mixed from "./pkgs/mixed";\n'
                            'fn main() { print(mixed::add(2, 3)); print(mixed::twice(4)); }\n')
        code, out, err = run([exe, "run", full], env={"GOOSE_HOME": str(home)})
        if code != 0 or out.split() != ["5", "8"]:
            raise RuntimeError(f"mixed_both.goose (goose run) exit {code}:\n{out}{err}")
