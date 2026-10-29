"""Diagnostics as data: `goose check --diagnostics json` prints the errors and
warnings a check found as a JSON array on stdout, one object each with its
severity, message, place and related lines, and the text the text mode would
have printed; the progress lines go to stderr. Checks the two modes agree, and
the places: a lexer error's column is its caret's, the errors after a raw
string the lexer rejected are at their own lines, a typecheck error ranges
over the node it is at with its instantiation chain at the calls, a warning
has a range too, an import error covers the whole import, a checkfree
function's kept checks are several errors at once, and an error with no
place in the program is reported as its text alone."""
import json
import re
import tempfile
from pathlib import Path

import toolchain as tc


def check_diagnostics_json(exe, base):
    """Raises RuntimeError on a failure."""
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="diagnostics-json-", dir=base) as tmp:
        root = Path(tmp)

        def require(good, what, text):
            if not good:
                raise RuntimeError(f"diagnostics json: {what}:\n{text}")

        def both(name, source=None):
            """The fixture checked in text mode and in JSON mode, as the parsed
            JSON and the text mode's diagnostics, once what the two modes have
            to agree on is checked: the exit code, the text of each
            diagnostic, where it says it is, and that stdout holds the JSON
            alone."""
            if source is not None:
                tc.write_text(root / name, source)
            code, out, err = tc.run_capture([exe, "check", "--stats", name], cwd=root)
            jcode, jout, jerr = tc.run_capture([exe, "check", "--stats", "--diagnostics", "json", name],
                                               cwd=root)
            require(code == jcode, f"{name}: exit code {code} in text mode, {jcode} as JSON",
                    err + jerr)
            try:
                diags = json.loads(jout)
            except ValueError as e:
                diags = None
                require(False, f"{name}: stdout is not JSON ({e})", jout + jerr)
            require(isinstance(diags, list), f"{name}: not an array", jout)
            # A check's progress lines share stderr with the diagnostics in
            # text mode (they are a JIT run's, which stdout belongs to the
            # program in); the rest of stderr is the diagnostics' text.
            progress = re.compile(r"^(?:parsed ok|roundtrip ok|typechecked ok|bce|bce-line):")
            text = "".join(line for line in err.splitlines(True) if not progress.match(line))
            require(text == "".join(d["text"] + "\n" for d in diags),
                    f"{name}: the text mode's stderr is not the JSON's text",
                    err + "\n---\n" + jout)
            require("parsed ok" not in jout and not out.strip(),
                    f"{name}: stdout holds more than the JSON", out + jout)
            for d in diags:
                require(d.get("severity") in ("error", "warning") and "message" in d,
                        f"{name}: a diagnostic without a severity or message", jout)
                if "line" in d:
                    require(d["file"] == name and
                            d["text"].startswith(f"{name}:{d['line']}: {d['severity']}: "),
                            f"{name}: the text names another place than the data", jout)
                if "endLine" in d:
                    require((d["endLine"], d["endCol"]) > (d["line"], d["col"]),
                            f"{name}: an empty or reversed range", jout)
                for r in d.get("related", []):
                    require(r["message"] in d["text"], f"{name}: a related line not in the text",
                            jout)
            return diags, text

        # A lexer error: at the caret's column, one byte wide.
        lexer = 'fn main() {\n    let s = "bad\\q";\n}\n'
        diags, err = both("lexer.goose", lexer)
        require(len(diags) == 1 and diags[0]["line"] == 2, "lexer error", err)
        caret = err.splitlines()[2]
        require(diags[0]["col"] == caret.index("^") + 1 and
                diags[0]["endCol"] == diags[0]["col"] + 1 and diags[0]["endLine"] == 2,
                "lexer error at its caret", err + "\n" + json.dumps(diags))

        # The errors after a """ string the lexer rejected, which the parse
        # skips past, at their own lines: a line short of the margin, text
        # before the closing quotes, and text after the opening ones.
        raw = ('fn main() {\n'
               '    let a = """\n'
               '        ok\n'
               '      short\n'
               '        """;\n'
               '    let x: i64 = ;\n'
               '    let b = """\n'
               '        ok\n'
               '      x """;\n'
               '    let y: i64 = ;\n'
               '    let c = """text\n'
               '        """;\n'
               '    let z: i64 = ;\n'
               '}\n')
        diags, err = both("rawstr.goose", raw)
        require([d["line"] for d in diags] == [4, 6, 9, 10, 11, 13],
                "the lines of the errors after a rejected \"\"\" string", err)

        # A typecheck error, over the node it is at, reached through a call:
        # the chain's entries are at the calls that made the instantiations.
        chain = ("fn get<T>(a: T) -> i64 { a.nope }\n"
                 "\n"
                 "fn main() {\n"
                 "    let x = get(1);\n"
                 "    print(x);\n"
                 "}\n")
        lines = chain.splitlines()
        diags, err = both("chain.goose", chain)
        require(len(diags) == 1 and diags[0]["line"] == 1 and
                diags[0]["col"] == lines[0].index("a.nope") + 1 and
                diags[0]["endLine"] == 1 and
                diags[0]["endCol"] == lines[0].index("a.nope") + 1 + len("a.nope"),
                "typecheck error over its node", err + "\n" + json.dumps(diags))
        related = diags[0].get("related", [])
        require(len(related) == 2 and "instantiated from chain.goose:4" in related[0]["message"]
                and related[0]["file"] == "chain.goose" and related[0]["line"] == 4 and
                related[0]["col"] == lines[3].index("get(1)") + 1 and
                related[1]["line"] == 3,
                "the instantiation chain at its calls", json.dumps(diags))

        library = ("import slot_pool;\n"
                   "struct Node { next: Node&<u32>? }\n"
                   "fn main() {\n"
                   "    var pool = slot_pool<Node> {};\n"
                   "    pool.alloc_ref(Node { null });\n"
                   "}\n")
        diags, err = both("library.goose", library)
        require(len(diags) == 1 and diags[0]["line"] == 5 and
                "contains self-relative references" in diags[0]["message"] and
                any(r["file"].endswith("slot_pool.goose") and
                    r["message"] == "while checking this standard library expression"
                    for r in diags[0].get("related", [])),
                "stdlib errors point to the user's call and retain the definition", err)

        # A warning, with the program accepted.
        warn = ("fn main() {\n"
                "    let x: i64 = 1;\n"
                "    let y = x as i64;\n"
                "    print(y);\n"
                "}\n")
        lines = warn.splitlines()
        diags, err = both("warn.goose", warn)
        require(len(diags) == 1 and diags[0]["severity"] == "warning" and
                diags[0]["message"].startswith("redundant `as i64`") and
                diags[0]["line"] == 3 and diags[0]["col"] == lines[2].index("x as i64") + 1 and
                diags[0]["endCol"] == lines[2].index("x as i64") + 1 + len("x as i64"),
                "a warning over its cast", err + "\n" + json.dumps(diags))

        # An import error, over the whole import.
        diags, err = both("imp.goose", "import no_such_module;\nfn main() {}\n")
        require(len(diags) == 1 and diags[0]["line"] == 1 and diags[0]["col"] == 1 and
                diags[0]["endLine"] == 1 and diags[0]["endCol"] == len("import no_such_module;") + 1
                and "cannot find the module no_such_module" in diags[0]["message"],
                "an import error over the import", err + "\n" + json.dumps(diags))

        # The checks a checkfree function keeps: several errors at once, each
        # at its own check.
        cf = ("checkfree fn f(a: i64[:], i: i64) -> i64 {\n"
              "    a[i] +\n"
              "    a[i + 1]\n"
              "}\n"
              "fn main() { let a = [1, 2, 3]; print(f(a[..], hardware_threads())); }\n")
        lines = cf.splitlines()
        diags, err = both("cf.goose", cf)
        require([d["line"] for d in diags] == [2, 3] and
                all(d["col"] == lines[d["line"] - 1].index("[") + 1 and "endLine" not in d
                    and "keeps a bounds check" in d["message"] for d in diags),
                "a checkfree function's kept checks", err + "\n" + json.dumps(diags))

        # Nothing to report, and an error with no place in the program.
        diags, err = both("ok.goose", "fn main() { print(1); }\n")
        require(diags == [] and err == "", "a program with nothing to report", err)
        diags, err = both("missing.goose")
        require(len(diags) == 1 and "line" not in diags[0] and "file" not in diags[0] and
                diags[0]["message"].startswith("cannot open file") and
                diags[0]["text"] == diags[0]["message"],
                "an error with no place", err + "\n" + json.dumps(diags))

        code, out, err = tc.run_capture([exe, "check", "--diagnostics", "yaml", "ok.goose"],
                                        cwd=root)
        require(code == 1 and "--diagnostics takes json or text" in err, "--diagnostics yaml",
                out + err)
