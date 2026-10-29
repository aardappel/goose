"""Paths on the command line and in imports: -o into a directory that does not
exist yet, for the C and for `goose build`'s executable; a package source of
"." or ".."; an --include path rooted by a leading separator kept as it is
when -o names a file in the working directory; an import that resolves
nowhere, which names the module and where it was looked for, or says that no
standard library was found; a directory where a file is read, a source or
an embed_file, which a POSIX system opens and then fails to read; and
--gen-runtime-header from a compiler with no source tree around it, which
says it cannot find the runtime's sources."""
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import toolchain as tc


def check_cli_paths(exe, cc, base):
    """Raises RuntimeError on a failure."""
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cli-paths-", dir=base) as tmp:
        root = Path(tmp)
        env = {k: v for k, v in os.environ.items() if k not in ("GOOSE_STDLIB", "GOOSE_RUNTIME")}

        def run(argv, cwd=root):
            r = subprocess.run([str(a) for a in argv], cwd=cwd, env=env, capture_output=True,
                               text=True, errors="replace", timeout=300)
            return r.returncode, r.stdout + r.stderr

        def require(good, what, text):
            if not good:
                raise RuntimeError(f"command line paths: {what}:\n{text}")

        probe = root / "probe.goose"
        tc.write_text(probe, 'import std;\nfn main() { print("paths"); }\n')

        cfile = root / "new" / "c" / "probe.c"
        code, text = run([exe, "-o", cfile, probe])
        require(code == 0 and cfile.is_file(), "-o into a new directory", text)
        if cc:
            built = root / "new" / "bin" / ("probe" + tc.EXE_SUFFIX)
            code, text = run([exe, "build", "-o", built, probe])
            require(code == 0 and built.is_file(), "goose build -o into a new directory", text)

        # A package whose root is the program's directory, and its parent.
        (root / "dots" / "app").mkdir(parents=True)
        tc.write_text(root / "dots" / "dots.goose", "namespace dots;\nfn seven() -> i64 { 7 }\n")
        tc.write_text(root / "dots" / "app" / "app.goose",
                      'import std;\nimport dots from "..";\nfn main() { print(dots::seven()); }\n')
        code, text = run([exe, "check", root / "dots" / "app" / "app.goose"])
        require(code == 0, 'a package from ".."', text)
        tc.write_text(root / "dots" / "here.goose",
                      'import std;\nimport dots from ".";\nfn main() { print(dots::seven()); }\n')
        code, text = run([exe, "check", root / "dots" / "here.goose"])
        require(code == 0, 'a package from "."', text)

        # Rooted by its separator alone, as a path is on POSIX, or on Windows
        # on the current drive.
        header = root / "inc_probe.h"
        tc.write_text(header, "/* An --include header. */\n")
        rooted = header.as_posix()
        if tc.IS_WINDOWS:
            rooted = rooted[2:]
        code, text = run([exe, "-o", "rooted.c", "--include", rooted, probe])
        written = (root / "rooted.c").read_text(encoding="utf-8") if code == 0 else ""
        require(f'#include "{rooted}"' in written,
                f"--include {rooted} with -o rooted.c",
                text + "".join(line for line in written.splitlines(True) if "#include" in line))

        tc.write_text(root / "missing.goose", "import std;\nimport no_such_module;\n"
                                              "fn main() {}\n")
        code, text = run([exe, "check", root / "missing.goose"])
        require(code == 1 and "missing.goose:2: error: cannot find the module no_such_module, "
                              "looked for" in text and
                text.count("no_such_module.goose") >= 2, "an import resolving nowhere", text)
        # A standard library found from the compiler's directory is named by
        # its normalized path, which diagnostics and runtime errors print for
        # its files.
        tc.write_text(root / "libdiag.goose", "import slot_pool;\nfn main() {\n"
                                              "    var pool = slot_pool<i64> {};\n"
                                              "    let r = pool.alloc_ref(1);\n"
                                              "    pool.free_ref(r);\n}\n")
        code, text = run([exe, "check", "--diagnostics", "json", root / "libdiag.goose"])
        files = [f.replace("\\\\", "/") for f in re.findall(r'"file": *"([^"]*)"', text)]
        require(code == 1 and any(f.endswith("/slot_pool.goose") for f in files) and
                not any("/../" in f or "/./" in f for f in files),
                "a standard library file's path in a diagnostic", text)
        # The source tree's standard library, named by --stdlib, as stdlib/ in
        # the working directory and, for a compiler in its build tree, beside
        # the compiler: it is looked in once. An installed compiler's own
        # standard library is another place, listed too.
        stdlib = Path(__file__).resolve().parent.parent / "stdlib"
        code, text = run([exe, "check", "--stdlib", stdlib, root / "missing.goose"],
                         cwd=stdlib.parent)
        require(code == 1 and text.count(f"{stdlib.as_posix()}/no_such_module.goose") == 1,
                "an import resolving nowhere, with the standard library found three ways", text)

        (root / "adir").mkdir()
        code, text = run([exe, "check", root / "adir"])
        require(code == 1 and "cannot open file: " in text, "a directory as the source", text)
        tc.write_text(root / "embeds.goose", 'fn main() { print(embed_file("adir").len); }\n')
        code, text = run([exe, "check", root / "embeds.goose"])
        require(code == 1 and "error: embed_file: cannot open " in text,
                "a directory to embed_file", text)

        # A compiler with no standard library anywhere it looks.
        lone = root / "lone" / "a" / "b" / "c"
        lone.mkdir(parents=True)
        lonexe = shutil.copy2(exe, lone / Path(exe).name)
        code, text = run([lonexe, "check", probe], cwd=lone)
        require(code == 1 and "cannot find the module std" in text and
                "found no standard library" in text and "GOOSE_STDLIB" in text,
                "no standard library", text)
        # Nor the runtime's sources, which --gen-runtime-header says.
        code, text = run([lonexe, "--gen-runtime-header"], cwd=lone)
        require(code == 1 and "cannot locate the runtime sources" in text,
                "--gen-runtime-header outside a source tree", text)
