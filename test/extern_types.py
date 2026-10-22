"""Included C adapters can use every declared extern's types, even when unused."""
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
