"""Included C adapters can use every declared extern's types, even when unused."""
import os
import subprocess
import tempfile
from pathlib import Path

import toolchain as tc


def check_extern_types(exe, cc, base, *, jit, runtime, extra=()):
    base = (Path(base) / "extern-types").resolve()
    base.mkdir(parents=True, exist_ok=True)
    # Import one interface from each application; none needs all its functions.
    tc.write_text(base / "adapter.goose", '''
struct Input { values: i16[2] }
struct Result { value: i32 }
extern fn sum(values: const f32[:]) -> f32 = "native_sum";
extern fn input(value: const Input&) -> i32 = "native_input";
extern fn result() -> Result = "native_result";
extern fn missing(values: const f64[:]) = "native_missing";
''')
    header = base / "adapter.h"
    tc.write_text(header, '''
static float native_sum(sl_f32 values) {
    float result = 0;
    for (int64_t i = 0; i < values.len; ++i) result += values.data[i];
    return result;
}
static int32_t native_input(Input_g *value) {
    return value->values_g.e[0] + value->values_g.e[1];
}
static Result_g native_result(void) {
    Result_g result = { 42 };
    return result;
}
/* The declaration needs its slice type, but never a linked implementation. */
static int64_t adapter_length(sl_f64 values) { return values.len; }
''')

    def run(command):
        code, out, err = tc.run_capture(command)
        if code or tc.sanitizer_failure(err):
            raise RuntimeError(out + err)
        return out

    def check(command):
        if run(command).strip() != "extern types passed":
            raise RuntimeError("unexpected extern-types output")

    cases = {
        "unused": "",
        "slice": "let xs: f32[2] = [1.25, 2.75]; assert(sum(xs) == 4.0);",
        "aggregate": "let x = Input { [20, 22] }; assert(input(x) == result().value);",
    }
    for name, body in cases.items():
        source = base / f"{name}.goose"
        tc.write_text(source, 'import adapter;\nfn main() {\n' + body +
                      '\nprint("extern types passed");\n}\n')
        for level in (0, 2):
            flags = [f"-O{level}", "--include", header]
            if cc:
                cfile = base / f"{name}-{level}.c"
                run([exe, *flags, "-o", cfile, source])
                binary = cfile.with_suffix(tc.EXE_SUFFIX)
                ok, log = cc.compile(cfile, binary, opt=level if not extra else 1,
                                     extra=extra, strict_decls=True, runtime=runtime)
                if not ok:
                    raise RuntimeError(f"{name} -O{level}:\n{log}")
                check([binary])
            if jit:
                check([exe, *flags, "--jit", source])

    # Supply the adapters through a package sidecar. Run outside the source
    # tree and emit elsewhere to check that paths are package-relative.
    with tempfile.TemporaryDirectory(prefix="headers-", dir=base) as tmp:
        root = Path(tmp)
        pkg = root / "adapter package"
        pkg.mkdir()
        output = root / "output"
        output.mkdir()
        tc.write_text(pkg / "adapter.goose", (base / "adapter.goose").read_text())
        adapter = pkg / "adapter.h"
        tc.write_text(adapter, header.read_text())
        tc.write_text(pkg / "adapter.build", 'c_headers = ["adapter.h"];\n')
        source = root / "app.goose"
        tc.write_text(source, 'import adapter from "./adapter package";\n'
                      'extern fn platform_value() -> i32;\n'
                      'extern fn cli_value() -> i32;\n'
                      'fn main() {\n    let x = Input { [20, 22] };\n'
                      '    assert(input(x) == 42);\n'
                      '    print(result().value + platform_value() + cli_value());\n}\n')
        tc.write_text(root / "common.h", '#define HEADER_OFFSET 3\n')
        tc.write_text(root / "platform.h",
                      'static int32_t platform_value(void) { return HEADER_OFFSET; }\n')
        cli = root / "cli.h"
        tc.write_text(cli, 'static int32_t cli_value(void) { return 5; }\n')
        platform = "windows" if tc.IS_WINDOWS else "macos" if tc.IS_MACOS else "linux"
        sidecar = 'c_headers = ["common.h"];\n'
        for p in ("windows", "macos", "linux"):
            path = "platform.h" if p == platform else "missing.h"
            sidecar += f'c_headers_{p} = ["{path}"];\n'
        tc.write_text(root / "app.build", sidecar)
        env = dict(os.environ, GOOSE_HOME=str(root / "home"))

        def expect(command, want=None, *, no_toolchain=False):
            runenv = dict(env, PATH="", CC="", CXX="") if no_toolchain else env
            r = subprocess.run([str(a) for a in command], cwd=output, env=runenv,
                               capture_output=True, text=True, errors="replace", timeout=600)
            if r.returncode or tc.sanitizer_failure(r.stderr) or (
                    want is not None and r.stdout.strip() != want):
                raise RuntimeError(f"package headers: {command}:\n{r.stdout}{r.stderr}")

        flags = ["--include", cli]
        if jit:
            expect([exe, "run", *flags, source], "50", no_toolchain=True)
        if cc:
            binary = output / ("app" + tc.EXE_SUFFIX)
            expect([exe, "build", "--cc", cc.cc, *flags, "-o", binary, source])
            expect([binary], "50")
            cfile = output / "app.c"
            expect([exe, *flags, "-o", cfile, source])
            ok, log = cc.compile(cfile, binary, opt=2 if not extra else 1,
                                 extra=extra, strict_decls=True, runtime=runtime)
            if not ok:
                raise RuntimeError(f"package header emitted C:\n{log}")
            expect([binary], "50")
            # A header-only package must not leave stale code in a native cache.
            tc.write_text(adapter, header.read_text().replace("{ 42 }", "{ 61 }"))
            expect([exe, "build", "--cc", cc.cc, *flags, "-o", binary, source])
            expect([binary], "69")
        # Even with no live extern calls, the header's generated types exist.
        tc.write_text(source, 'import adapter from "./adapter package";\n'
                      'fn main() { print("unused"); }\n')
        if jit:
            expect([exe, "run", source], "unused", no_toolchain=True)
