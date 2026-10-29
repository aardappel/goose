"""`goose format`: the layout it gives a file, and its command line. Each
golden case formats its input through standard input and must print the
expected text, which must be a fixed point: formatting it again changes
nothing. The command-line cases cover rewriting in place, --check, the
directory walk, files that do not parse or cannot be written, standard
input and the exit codes: 0 formatted, 1 --check found a file that is
not, 2 any error."""
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

import toolchain as tc
from doc_comments import SOURCE as DOC_SOURCE

# (name, input, expected output)
GOLDEN = [
    ("spacing",
     "fn  f(a:i64,b :i64)->i64{a+b}\n",
     "fn f(a: i64, b: i64) -> i64 { a + b }\n"),
    ("unary and binary operators",
     "fn f(a: i64) -> i64 { let b = a- -a; let c = -a*-1; let d = !(a>0); let e = a& &b; a-b }\n",
     "fn f(a: i64) -> i64 { let b = a - -a; let c = -a * -1; let d = !(a > 0); let e = a & &b; a - b }\n"),
    ("generics and comparisons",
     "struct Pair<A,B>{a:A,b:B}\n"
     "fn f<T>(x:T)->bool{let p=Pair<i64,Foo<Bar<i64>>>{1,g<i64>(2)};x<y&&y>z}\n",
     "struct Pair<A, B> { a: A, b: B }\n"
     "fn f<T>(x: T) -> bool { let p = Pair<i64, Foo<Bar<i64>>> { 1, g<i64>(2) }; x < y && y > z }\n"),
    ("types",
     "fn f(p: Node&<u32 in pool>, q: u8 [ : ], r: i32 [ > .. < ], s: Shape .. ?, t: u8[ ..16 ]) "
     "{ let x = y as ! u8; let z = w [1 ..] [.. ^1]; }\n",
     "fn f(p: Node&<u32 in pool>, q: u8[:], r: i32[>..<], s: Shape..?, t: u8[..16]) "
     "{ let x = y as! u8; let z = w[1..][..^1]; }\n"),
    ("root structs and component paths",
     "root  struct State<T>{items:T[>..],scratch:i64[>..<]}\n"
     "var pool=State<Node>{[],[]};\n"
     "fn f(p:Node&<u32 in pool . items>,i:u32 in pool.items){let root=pool.items[i];}\n",
     "root struct State<T> { items: T[>..], scratch: i64[>..<] }\n"
     "var pool = State<Node> { [], [] };\n"
     "fn f(p: Node&<u32 in pool.items>, i: u32 in pool.items) { let root = pool.items[i]; }\n"),
    ("imports, const for, initializers and qualified names",
     "import .a.b;\n"
     "fn f() { const for <k,v> in fields<P> { print(k); } let p = P{x:1,..}; let q = ::a + ns :: b; "
     "for i in 0 .. n {} }\n",
     "import .a.b;\n"
     "fn f() { const for <k, v> in fields<P> { print(k); } let p = P { x: 1, .. }; let q = ::a + ns::b; "
     "for i in 0..n {} }\n"),
    ("closures and match arms",
     "fn f() { xs.each(){x=>print(x)}; let n = match k {1=>2,-1 .. 4=>3,_=>3}; }\n",
     "fn f() { xs.each() { x => print(x) }; let n = match k { 1 => 2, -1..4 => 3, _ => 3 }; }\n"),
    ("trailing commas added",
     "struct P {\n    x: i64,\n    y: i64\n}\n"
     "enum E {\n    A,\n    B { v: i64 }\n}\n"
     "fn f(a: i64,\n     b: i64) {\n"
     "    let p = P {\n        x: 1,\n        y: 2\n    };\n"
     "    g(\n        1,\n        2 // two\n    );\n"
     "    let xs = [\n        1,\n        2\n    ];\n"
     "    let n = match a {\n        1 => 2,\n        _ => { 3 }\n    };\n"
     "}\n",
     "struct P {\n    x: i64,\n    y: i64,\n}\n"
     "enum E {\n    A,\n    B { v: i64 },\n}\n"
     "fn f(a: i64,\n     b: i64) {\n"
     "    let p = P {\n        x: 1,\n        y: 2,\n    };\n"
     "    g(\n        1,\n        2, // two\n    );\n"
     "    let xs = [\n        1,\n        2,\n    ];\n"
     "    let n = match a {\n        1 => 2,\n        _ => { 3 },\n    };\n"
     "}\n"),
    ("trailing commas not added",
     "fn f() {\n"
     "    let p = P {\n        x: 1,\n        ..\n    };\n"
     "    let z = [\n        0; 8\n    ];\n"
     "    let w = (\n        1\n    );\n"
     "    let n = match a {\n        _ => return 5\n    };\n"
     "    loop {\n        match a {\n            _ => break\n        }\n    }\n"
     "    let v = a\n        + b;\n"
     "}\n",
     None),
    ("indentation from tabs and stray columns",
     "fn f() {\n\tlet x = 1;\n  \tif x {\n  print(x);\n      }\n}\n",
     "fn f() {\n    let x = 1;\n    if x {\n        print(x);\n    }\n}\n"),
    ("aligned continuations move with their brackets",
     "  fn f() {\n      let x = foo(a,\n                  b);\n      bar(1,\n          2)\n  }\n",
     "fn f() {\n    let x = foo(a,\n                b);\n    bar(1,\n        2)\n}\n"),
    ("continuations",
     "fn f() -> bool {\n    if a ||\n       b {\n        return c &&\n    d;\n    }\n"
     "    let p = if a { 1 }\n            else { 2 };\n    true\n}\n",
     "fn f() -> bool {\n    if a ||\n       b {\n        return c &&\n        d;\n    }\n"
     "    let p = if a { 1 }\n            else { 2 };\n    true\n}\n"),
    ("a chain continued at its statement's indentation",
     "fn f() -> bool {\n    let x = a +\n    b +\n    c;\n    x > 1 &&\n    x < 9 &&\n        x != 5\n}\n",
     None),
    ("bodies after wrapped headers and conditions",
     "fn f(a: i64,\n     b: i64) {\n  a;\n}\n"
     "fn g() {\n    if a &&\n       b {\n            c;\n    }\n}\n"
     "fn h()\n{\n    a;\n}\n",
     "fn f(a: i64,\n     b: i64) {\n    a;\n}\n"
     "fn g() {\n    if a &&\n       b {\n        c;\n    }\n}\n"
     "fn h()\n{\n    a;\n}\n"),
    ("closing brackets together",
     "fn f() {\n  g(P {\n            x: 1,\n          })\n}\n",
     "fn f() {\n    g(P {\n        x: 1,\n    })\n}\n"),
    ("blank lines",
     "\n\nfn f() {\n\n    a();\n\n\n    b();\n\n}\n\n\nfn g() {}\n\n\n",
     "fn f() {\n    a();\n\n    b();\n}\n\nfn g() {}\n"),
    ("line breaks and trailing whitespace",
     "fn f() {   \r\n    a();  \r\n}",
     "fn f() {\n    a();\n}\n"),
    ("byte order mark",
     "﻿fn f() {}\n",
     "fn f() {}\n"),
    ("empty file", "\n\n", ""),
    ("comment-only file", "  // one\n\n\n// two  \n", "// one\n\n// two\n"),
    ("trailing comment moves with its line",
     "fn f() {\n      let x = 1;   // one\n  let y=2; // two\n}\n",
     "fn f() {\n    let x = 1;   // one\n    let y = 2; // two\n}\n"),
    ("aligned trailing comments stay aligned",
     "fn f() {\n    a=b+c;  // one\n    d = e;  // two\n}\n",
     "fn f() {\n    a = b + c; // one\n    d = e;     // two\n}\n"),
    ("ragged trailing comments stay ragged",
     "fn f() {\n    a = 1; // one\n    bb = 2;  // two\n}\n",
     None),
    ("a blank line ends an aligned run",
     "fn f() {\n    a=b+c;  // one\n\n    d = e;  // two\n}\n",
     "fn f() {\n    a = b + c; // one\n\n    d = e;  // two\n}\n"),
    ("block comments",
     "fn f() {\n  /* one\n     two */\n  a(/*x*/1, /* y */ 2);\n}\n",
     "fn f() {\n    /* one\n       two */\n    a(/*x*/1, /* y */ 2);\n}\n"),
    ("multi-line string moves with its line",
     'fn f() {\n  let s = """\n    text\n      \n    more\n    """;\n}\n',
     'fn f() {\n    let s = """\n      text\n        \n      more\n      """;\n}\n'),
    ("a table of declarations lines up",
     "const QUIC_FRAME_PADDING: u64 = 0x00;\nconst QUIC_FRAME_PING: u64 = 0x01;\n"
     "const QUIC_FRAME_ACK:  u64 = 0x02;\nconst QUIC_FRAME_ACK_ECN: u64 = 0x03;\n"
     "\nconst A: u8 = 1; // a\nconst BBBB: u64 = 2; // b\nconst C: Foo<i64> = 3;\n"
     "struct P {\n    x: i64,\n    longer: u8, // c\n    z: Foo<T>,\n}\n"
     "fn f() {\n    let p = P {\n        x: 1,\n        longer: 2,\n    };\n"
     "    let a: i64 = 1;\n    let bbb: i64 = 1;\n    var c: i64 = 1;\n}\n",
     "const QUIC_FRAME_PADDING: u64 = 0x00;\nconst QUIC_FRAME_PING:    u64 = 0x01;\n"
     "const QUIC_FRAME_ACK:     u64 = 0x02;\nconst QUIC_FRAME_ACK_ECN: u64 = 0x03;\n"
     "\nconst A:    u8       = 1; // a\nconst BBBB: u64      = 2; // b\nconst C:    Foo<i64> = 3;\n"
     "struct P {\n    x:      i64,\n    longer: u8, // c\n    z:      Foo<T>,\n}\n"
     "fn f() {\n    let p = P {\n        x:      1,\n        longer: 2,\n    };\n"
     "    let a:   i64 = 1;\n    let bbb: i64 = 1;\n    var c: i64 = 1;\n}\n"),
    ("a table of statements after semicolons",
     "let a = 1;      let b = 2;\nlet cc = 3;     let d = 4;\n",
     None),
]


def run(exe, args, stdin=b"", cwd=None):
    r = subprocess.run([str(exe), "format", *args], input=stdin, capture_output=True, cwd=cwd,
                       timeout=60)
    return r.returncode, r.stdout, r.stderr.decode("utf-8", "replace")


def check_golden(exe, name, src, want):
    want = src if want is None else want
    code, out, err = run(exe, [], src.encode("utf-8"))
    if code != 0 or out.decode("utf-8") != want:
        raise RuntimeError(f"format golden case {name!r}: exit {code}, printed:\n"
                           f"{out.decode('utf-8', 'replace')}{err}\nnot:\n{want}")
    code, again, err = run(exe, [], out)
    if code != 0 or again != out:
        raise RuntimeError(f"format golden case {name!r}: not a fixed point, formatting it "
                           f"again printed:\n{again.decode('utf-8', 'replace')}{err}")


def check_cli(exe, root):
    def write(rel, text):
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(text.encode("utf-8"))
        return p

    messy, tidy = "fn f(){a;}\n", "fn f() { a; }\n"

    def expect(what, ok, detail=""):
        if not ok:
            raise RuntimeError(f"format: {what}\n{detail}")

    # Rewriting in place, and leaving a formatted file untouched.
    f = write("one.goose", messy)
    code, out, err = run(exe, [f])
    expect("rewrites a file in place", code == 0 and not out and f.read_text() == tidy,
           f"exit {code}: {out}{err}{f.read_text()}")
    old = 1_000_000_000
    os.utime(f, (old, old))
    code, out, err = run(exe, [f])
    expect("leaves a formatted file and its time alone",
           code == 0 and int(f.stat().st_mtime) == old, f"exit {code}: {out}{err}")

    # --check lists what is not formatted and writes nothing.
    g = write("two.goose", messy)
    code, out, err = run(exe, ["--check", f, g])
    expect("--check lists the file that is not formatted",
           code == 1 and out.decode().splitlines() == [str(g)] and g.read_text() == messy,
           f"exit {code}: {out}{err}")
    code, out, err = run(exe, ["--check", f])
    expect("--check on a formatted file", code == 0 and not out, f"exit {code}: {out}{err}")

    # The walk: .goose files only, sorted, not in dot-directories or build
    # trees, unless named.
    tree = root / "tree"
    for rel in ["b.goose", "a/c.goose", ".hidden/d.goose", "build/e.goose", "notes.txt"]:
        write(f"tree/{rel}", messy)
    write("tree/build/CMakeCache.txt", "")
    code, out, err = run(exe, ["--check", tree])
    listed = [Path(p).relative_to(tree).as_posix() for p in out.decode().splitlines()]
    expect("walks a directory", code == 1 and listed == ["a/c.goose", "b.goose"],
           f"exit {code}: {listed}{err}")
    code, out, err = run(exe, ["--check", tree / ".hidden"])
    expect("formats a dot-directory it is given", code == 1 and len(out.decode().splitlines()) == 1,
           f"exit {code}: {out}{err}")

    # A file that does not parse is left as it is, its errors printed, and
    # the others are formatted; an error wins over --check's 1.
    bad = write("errs/bad.goose", "fn f( {\n")
    good = write("errs/good.goose", messy)
    code, out, err = run(exe, ["--check", root / "errs"])
    expect("--check with a difference and a parse error exits 2", code == 2, f"exit {code}: {out}{err}")
    code, out, err = run(exe, [root / "errs"])
    expect("formats the files that parse and reports the one that does not",
           code == 2 and "error" in err and bad.read_text() == "fn f( {\n" and
           good.read_text() == tidy, f"exit {code}: {out}{err}")

    # A read-only file is not replaced.
    ro = write("ro.goose", messy)
    os.chmod(ro, stat.S_IREAD)
    try:
        code, out, err = run(exe, [ro])
        expect("a read-only file", code == 2 and "cannot write" in err and ro.read_text() == messy,
               f"exit {code}: {out}{err}")
    finally:
        os.chmod(ro, stat.S_IREAD | stat.S_IWRITE)

    # Standard input, with no files or with -.
    for args in ([], ["-"]):
        code, out, err = run(exe, args, messy.encode())
        expect(f"standard input with {args}", code == 0 and out.decode() == tidy, f"exit {code}: {out}{err}")
    code, out, err = run(exe, ["--check"], messy.encode())
    expect("--check on standard input", code == 1 and out.decode() == "<stdin>\n", f"exit {code}: {out}{err}")
    code, out, err = run(exe, [], b"fn f( {\n")
    expect("standard input that does not parse", code == 2 and not out and "error" in err,
           f"exit {code}: {out}{err}")
    code, out, err = run(exe, ["-", f])
    expect("- with a file", code == 2, f"exit {code}: {out}{err}")
    code, out, err = run(exe, ["--nosuch"])
    expect("an unknown option", code == 2 and "--nosuch" in err, f"exit {code}: {out}{err}")
    code, out, err = run(exe, [root / "nosuch.goose"])
    expect("a missing file", code == 2 and "cannot open" in err, f"exit {code}: {out}{err}")

    # --verify checks and never writes.
    v = write("verify.goose", messy)
    code, out, err = run(exe, ["--verify", v])
    expect("--verify", code == 0 and b"1 file(s) verified" in out and v.read_text() == messy,
           f"exit {code}: {out}{err}")

    # The help.
    for argv in ([str(exe), "help", "format"], [str(exe), "format", "--help"]):
        r = subprocess.run(argv, capture_output=True, timeout=60)
        expect(f"{' '.join(argv[1:])}", r.returncode == 0 and
               r.stdout.startswith(b"usage: goose format") and b"Exit status" in r.stdout,
               r.stdout.decode() + r.stderr.decode())


def check_docs(exe, root):
    """Documentation comments come through formatting as they were."""
    src = root / "docs.goose"
    tc.write_text(src, DOC_SOURCE)
    code, before, err = tc.run_capture([exe, "--dump-docs", src])
    if code != 0:
        raise RuntimeError(f"format: --dump-docs of the documentation sample failed:\n{err}")
    code, out, err = run(exe, [src])
    if code != 0:
        raise RuntimeError(f"format: the documentation sample does not format:\n{out}{err}")
    code, after, err = tc.run_capture([exe, "--dump-docs", src])
    if code != 0 or after != before:
        raise RuntimeError(f"format: the documentation comments changed:\n{after}{err}\nnot:\n{before}")


def check_format(exe, base):
    """Raises RuntimeError on a failure."""
    for name, src, want in GOLDEN:
        check_golden(exe, name, src, want)
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="format-", dir=base) as tmp:
        root = Path(tmp)
        check_cli(exe, root)
        check_docs(exe, root)


def check_format_sweep(exe, dirs):
    """Every Goose file in the tree formats and passes the formatter's own
    checks, formatting the result again changes nothing, and the only files
    skipped as not parsing are the error fixtures and the fixtures that are
    broken on purpose."""
    r = subprocess.run([str(exe), "format", "--verify", *map(str, dirs)], capture_output=True,
                       timeout=600)
    out, err = r.stdout.decode("utf-8", "replace"), r.stderr.decode("utf-8", "replace")
    if r.returncode != 0:
        raise RuntimeError(f"format --verify (exit {r.returncode}):\n{out}{err}")
    broken = {"test/lsp/broken_decls.goose", "test/lsp/complete.goose", "test/lsp/exemplar.goose",
              "test/syntax/lexer_tokens.goose"}
    repo = Path(__file__).resolve().parent.parent
    for line in out.splitlines():
        if not line.startswith("skip "):
            continue
        # The path may hold a drive's colon: what ends it is the message.
        path = Path(line[5:].rsplit(": does not parse", 1)[0]).resolve().relative_to(repo).as_posix()
        if "/errors/" not in path and "/errors_tc/" not in path and path not in broken:
            raise RuntimeError(f"format --verify skipped a file that should parse: {line}")
    return out.splitlines()[-1] if out else ""


if __name__ == "__main__":
    check_format(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ".")
    print("ok")
