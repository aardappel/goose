"""Resource roots under JIT, native deployment and relocation, without chdir."""
from pathlib import Path
import shutil

import toolchain as tc


def check_resource_paths(exe, cc, base, *, jit, runtime, extra=()):
    base = (Path(base) / "resource-paths").resolve()
    source_dir = base / "source space \u00e9"
    native_dir = base / "native"
    moved_dir = base / "relocated space \u00e9"
    cwd = base / "unrelated cwd"
    for directory in (source_dir, native_dir, moved_dir, cwd):
        directory.mkdir(parents=True, exist_ok=True)
    source = source_dir / "main.goose"
    source.write_text('''import os;
fn main() {
    let argv = args();
    let root = resource_dir();
    assert(root.len > 0 && is_dir(root));
    assert(root[root.len - 1] == '/' || root[root.len - 1] == 92);
    var appended: u8[>..] = "prefix";
    assert(resource_dir(appended) && appended[6..] == root);
    var asset: u8[>..] = [];
    assert(read_file(str(root, "asset.txt"), asset) && asset == argv[1]);
    var local: u8[>..] = [];
    assert(read_file("cwd.txt", local) && local == "working directory");
    assert(write_file("cwd-output.txt", "still local"));
    print("resource paths passed");
}
''', encoding="utf-8")
    for directory, value in ((source_dir, "source"), (native_dir, "native"),
                             (moved_dir, "relocated")):
        (directory / "asset.txt").write_text(value, encoding="utf-8")
    (cwd / "cwd.txt").write_text("working directory", encoding="utf-8")

    def run(command):
        code, out, err = tc.run_capture(command, cwd=cwd)
        if code or tc.sanitizer_failure(err):
            raise RuntimeError(out + err)
        return out

    def check(command):
        if run(command).strip() != "resource paths passed":
            raise RuntimeError("unexpected resource-path output")
        if (cwd / "cwd-output.txt").read_text(encoding="utf-8") != "still local":
            raise RuntimeError("relative output did not stay in the working directory")

    if jit:
        check([exe, source, "--", "source"])
    if cc:
        for standalone in (False, True):
            tag = "standalone" if standalone else "separate"
            cfile = base / (tag + ".c")
            run([exe, "-O2", *(["--standalone"] if standalone else []),
                 "-o", cfile, source])
            binary = native_dir / (tag + tc.EXE_SUFFIX)
            ok, log = cc.compile(cfile, binary, opt=2, extra=extra, strict_decls=True,
                                 runtime=None if standalone else runtime)
            if not ok:
                raise RuntimeError(log)
            check([binary, "native"])
            relocated = moved_dir / binary.name
            shutil.copy2(binary, relocated)
            check([relocated, "relocated"])
