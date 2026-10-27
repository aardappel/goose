"""The language server, `goose lsp` (docs/design/language_server.md), driven
over its own protocol: a client of this runner's starts it on a pipe,
initializes with test/lsp/basics.goose as its entry file, opens it, waits
for its diagnostics, and asks what its `// lsp:` lines say to ask, comparing
the answers. Then it edits the document into an error and back, checking the
diagnostics follow, with the edits arriving in a burst so that one analysis
is cancelled by the next, and asks a hover right behind a change, which must
answer from the changed text. It opens the other fixtures of test/lsp/, and
errors_tc/recovery/inside_braces.goose and exemplar_unchecked.goose, waiting
for their diagnostics and asking their queries; opens and closes
roots_lib.goose and the roots_app fixtures importing it as roots come and
go; and closes the entry file while helper.goose, which only it reached,
stays open. It formats documents that are open and not saved: the edits
must turn the text into what `goose format` writes, change only the lines
that change, and be none for a formatted document; a document that does
not parse, or one not open, gets null. A second client, with two_a.goose and two_b.goose as its entry
files, renames a function both import and finds its references and callers
in both programs. Each server is seen out. The row of docs/testing.md on
this file has the details."""
import json
import re
import subprocess
import time
from pathlib import Path

import toolchain as tc

HERE = Path(__file__).resolve().parent
# The features whose workspace/<feature>/refresh the client takes.
REFRESHED = ("codeLens", "semanticTokens", "inlayHint")


def uri_of(path):
    p = Path(path).resolve().as_posix()
    if re.match(r"^[A-Za-z]:/", p):
        p = p[0].lower() + "%3A" + p[2:]
    return "file:///" + p.lstrip("/")


class Client:
    def __init__(self, exe, cwd):
        self.p = subprocess.Popen([exe, "lsp"], cwd=cwd, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.seq = 0
        self.pending = []    # Notifications read while waiting for a reply.
        self.requests = []   # The methods of the server's requests, each answered with null.
        self.published = {}  # The diagnostics last published for each URI (lower case), as an editor has them.

    def send(self, method, params=None, request=True):
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        if request:
            self.seq += 1
            msg["id"] = self.seq
        self.write(msg)
        return self.seq if request else None

    def write(self, msg):
        body = json.dumps(msg).encode()
        try:
            self.p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
            self.p.stdin.flush()
        except OSError:
            raise RuntimeError(f"the server is gone (exit {self.p.wait()}):\n"
                               f"{tc.decode(self.p.stderr.read())}")

    def read(self):
        """The next message from the server other than a request of its."""
        while True:
            m = self.read_one()
            if "method" in m and "id" in m:
                self.requests.append(m["method"])
                self.write({"jsonrpc": "2.0", "id": m["id"], "result": None})
                continue
            if m.get("method") == "textDocument/publishDiagnostics":
                self.published[m["params"]["uri"].lower()] = m["params"]["diagnostics"]
            return m

    def read_one(self):
        headers = {}
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError(f"the server closed its output (exit {self.p.wait()}):\n"
                                   f"{tc.decode(self.p.stderr.read())}")
            line = line.strip()
            if not line:
                break
            name, value = line.decode().split(":", 1)
            headers[name.strip()] = value.strip()
        return json.loads(self.p.stdout.read(int(headers["Content-Length"])))

    def reply(self, rid, timeout=30, raw=False):
        """The reply to request rid; the notifications before it wait in
        `pending`. Raw: the whole message, an error reply included."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            m = self.read()
            if m.get("id") == rid:
                if raw:
                    return m
                if "error" in m:
                    raise RuntimeError(f"request {rid} failed: {m['error']}")
                return m.get("result")
            self.pending.append(m)
        raise RuntimeError(f"no reply to request {rid} in {timeout}s")

    def request(self, method, params, raw=False):
        return self.reply(self.send(method, params), raw=raw)

    def notification(self, method, timeout=30):
        """The next notification of that method, pending or arriving."""
        for i, m in enumerate(self.pending):
            if m.get("method") == method:
                return self.pending.pop(i)["params"]
        deadline = time.time() + timeout
        while time.time() < deadline:
            m = self.read()
            if m.get("method") == method:
                return m["params"]
            self.pending.append(m)
        raise RuntimeError(f"no {method} in {timeout}s")

    def diagnostics_for(self, uri, accept=lambda diags: True, timeout=30):
        """The next diagnostics published for uri that `accept` takes:
        the ones of the text as it stands, after those of the versions a
        burst of changes went through."""
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            params = self.notification("textDocument/publishDiagnostics", timeout)
            if params["uri"].lower() != uri.lower():
                continue
            last = params["diagnostics"]
            if accept(last):
                return last
        raise RuntimeError(f"no accepted diagnostics for {uri} in {timeout}s; the last were\n"
                           f"{json.dumps(last, indent=1)[:2000]}")


def check_lsp_protocol(exe, base):
    """Raises RuntimeError on a failure."""
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    code, out, err = tc.run_capture([exe, "lsp", "--probe"])
    if code != 0 or not out.startswith("goose lsp 3."):
        raise RuntimeError(f"goose lsp --probe (exit {code}):\n{out}{err}")
    fixtures = HERE / "lsp"
    main = fixtures / "basics.goose"
    helper = fixtures / "helper.goose"
    text = main.read_text(encoding="utf-8")
    main_uri, helper_uri = uri_of(main), uri_of(helper)

    def position(spec):
        line, col = (int(x) for x in spec.split(":"))
        return {"line": line - 1, "character": col - 1}

    def fail(what, got):
        raise RuntimeError(f"lsp {what}:\n{json.dumps(got, indent=1)[:3000]}")

    client = Client(exe, fixtures)
    # The client has the commands of the VS Code extension's that code
    # lenses name.
    client.request("initialize", {"processId": None, "rootUri": uri_of(fixtures),
                                  "capabilities": {"workspace": {f: {"refreshSupport": True} for f in REFRESHED}},
                                  "initializationOptions": {"entryFiles": [str(main)],
                                                            "commands": ["goose.run", "goose.showInstantiations"]}})
    client.send("initialized", {}, request=False)

    def open_fixture(path, text):
        """Opens a fixture and returns the diagnostics published for it."""
        client.send("textDocument/didOpen", {"textDocument": {
            "uri": uri_of(path), "languageId": "goose", "version": 1, "text": text}}, request=False)
        return client.diagnostics_for(uri_of(path))

    def queries(path, text, diags):
        """Asks what the fixture's `// lsp:` lines say to ask, and checks the
        answers; its `error` lines are the lines its diagnostics must be at,
        all of them."""
        uri = uri_of(path)
        doc = {"textDocument": {"uri": uri}}
        errors = []
        lines = text.splitlines()

        def text_at(rng):
            """The fixture's text in a range on one line."""
            s, e = rng["start"], rng["end"]
            if s["line"] != e["line"]:
                return None
            return lines[s["line"]][s["character"]:e["character"]]

        def whole_range():
            return {"start": {"line": 0, "character": 0}, "end": {"line": len(lines), "character": 0}}

        for line in text.splitlines():
            m = re.match(r"// lsp: (\w+) (.*)$", line)
            if not m or line.startswith("// lsp: fixture"):
                continue
            kind, rest = m.groups()
            if kind == "error":
                errors.append(int(rest))
            elif kind == "hover":
                pos, want = rest.split(" ", 1)
                got = client.request("textDocument/hover", {**doc, "position": position(pos)})
                if not got or json.loads(want) not in got["contents"]["value"]:
                    fail(f"hover {pos} lacks {want}", got)
            elif kind == "definition":
                # `-> none`: a name that resolves to no declaration.
                pos, _, target = rest.split(" ", 2)
                got = client.request("textDocument/definition", {**doc, "position": position(pos)})
                if target == "none":
                    if got:
                        fail(f"definition {pos} is not none", got)
                    continue
                if not got:
                    fail(f"definition {pos}", got)
                if target.endswith(".goose"):
                    if not got["uri"].lower().endswith(target):
                        fail(f"definition {pos} is not in {target}", got)
                else:
                    want = position(target)
                    if got["uri"].lower() != uri.lower() or got["range"]["start"] != want:
                        fail(f"definition {pos} is not at {target}", got)
            elif kind == "symbol":
                name, symkind = rest.split()
                kinds = {"function": 12, "struct": 23, "enum": 10, "constant": 14, "variable": 13}
                got = client.request("textDocument/documentSymbol", doc)
                if not any(s["name"] == name and s["kind"] == kinds[symkind] for s in got or []):
                    fail(f"symbol {name} ({symkind})", got)
            elif kind == "signature":
                pos, want, active = re.match(r'(\S+) "(.*)" (\d+)$', rest).groups()
                got = client.request("textDocument/signatureHelp", {**doc, "position": position(pos)})
                if (not got or got["signatures"][got["activeSignature"]]["label"] != want or
                        got["activeParameter"] != int(active)):
                    fail(f"signature {pos}", got)
            elif kind == "complete":
                # `complete L:C has NAME` / `lacks NAME`: the labels offered.
                pos, mode, name = rest.split()
                got = client.request("textDocument/completion", {**doc, "position": position(pos)})
                labels = {item["label"] for item in (got or {}).get("items", [])}
                if (name in labels) != (mode == "has"):
                    fail(f"completion {pos} {mode} {name}", sorted(labels))
            elif kind == "token":
                # `none`: no token at the place.
                pos, want = rest.split()
                got = client.request("textDocument/semanticTokens/full", doc)
                legend = ["namespace", "type", "struct", "enum", "enumMember", "typeParameter",
                          "parameter", "variable", "property", "function", "method"]
                data = got["data"]
                tokens = {}
                line = char = 0
                for i in range(0, len(data), 5):
                    dline, dchar, length, ttype, mods = data[i:i + 5]
                    line += dline
                    char = char + dchar if dline == 0 else dchar
                    tokens[(line, char)] = legend[ttype]
                p = position(pos)
                if tokens.get((p["line"], p["character"]), "none") != want:
                    fail(f"token {pos} is not {want}", {"at": p, "got": tokens.get((p["line"], p["character"]))})
            elif kind == "references":
                pos, want = rest.split()
                got = client.request("textDocument/references",
                                     {**doc, "position": position(pos), "context": {"includeDeclaration": True}})
                if len(got or []) != int(want):
                    fail(f"references {pos} are not {want}", got)
            elif kind == "rename":
                # `rename L:C NAME -> N` renames to NAME with N edits, each
                # replacing the leaf of the name at the position; `-> error`
                # is a refusal.
                pos, name, _, want = rest.split()
                m = client.request("textDocument/rename", {**doc, "position": position(pos), "newName": name}, raw=True)
                if want == "error":
                    if "error" not in m:
                        fail(f"rename {pos} was not refused", m)
                    continue
                if "error" in m:
                    fail(f"rename {pos} was refused", m)
                changes = (m.get("result") or {}).get("changes", {})
                edits = [e for es in changes.values() for e in es]
                p = position(pos)
                leaf = re.match(r"\w+", lines[p["line"]][p["character"]:]).group()
                if len(edits) != int(want) or any(e["newText"] != name for e in edits):
                    fail(f"rename {pos} does not make {want} edits to {name}", m)
                for e in changes.get(uri, changes.get(uri.lower(), [])):
                    if text_at(e["range"]) != leaf:
                        fail(f"rename {pos} edits more than the name {leaf}", m)
            elif kind == "hint":
                pos, want = re.match(r'(\S+) "(.*)"$', rest).groups()
                got = client.request("textDocument/inlayHint", {**doc, "range": whole_range()})
                p = position(pos)
                if not any(h["position"] == p and h["label"] == want for h in got or []):
                    fail(f"hint {pos} is not {want}", got)
            elif kind == "hints":
                # `hints L N`: line L has N hints, no more.
                at, want = (int(x) for x in rest.split())
                got = client.request("textDocument/inlayHint", {**doc, "range": whole_range()})
                if sum(h["position"]["line"] == at - 1 for h in got or []) != want:
                    fail(f"line {at} has not {want} hints", got)
            elif kind == "lens":
                # `lens L "title"`: a lens on line L with that title. A count
                # of instantiations has as many lines as its data, which its
                # command shows; "Run" runs the file.
                at, want = re.match(r'(\d+) "(.*)"$', rest).groups()
                got = client.request("textDocument/codeLens", doc)
                lens = next((x for x in got or [] if x["range"]["start"]["line"] == int(at) - 1 and
                             x["command"]["title"] == want), None)
                if not lens:
                    fail(f"lens {at} {want}", got)
                command = lens["command"]
                count = re.match(r"(\d+) instantiations?$", want)
                if want == "Run":
                    ok = command["command"] == "goose.run" and command["arguments"] == [uri]
                elif count:
                    ok = (len(lens["data"]) == int(count.group(1)) and
                          command["command"] == "goose.showInstantiations" and command["arguments"][1] == lens["data"])
                else:
                    ok = want == "not instantiated" and lens["data"] == [] and command["command"] == ""
                if not ok:
                    fail(f"lens {at} {want}", lens)
            elif kind in ("incoming", "outgoing"):
                # `incoming L:C -> NAME@L,L ...` / `outgoing ...`: the calls
                # of (or made by) the function named at the position, by the
                # function at their other end with the lines of the calls;
                # `-` for none, `none` where no function is named there.
                pos, _, want = rest.split(" ", 2)
                items = client.request("textDocument/prepareCallHierarchy", {**doc, "position": position(pos)})
                if want == "none":
                    if items:
                        fail(f"call hierarchy {pos} is not none", items)
                    continue
                if not items or len(items) != 1:
                    fail(f"call hierarchy {pos}", items)
                calls = client.request(f"callHierarchy/{kind}Calls", {"item": items[0]})
                end = "from" if kind == "incoming" else "to"
                got = sorted(c[end]["name"] + "@" + ",".join(str(r["start"]["line"] + 1) for r in c["fromRanges"])
                             for c in calls or [])
                if got != ([] if want == "-" else sorted(want.split())):
                    fail(f"{kind} calls {pos} are not {want}", got)
            elif kind == "folding":
                a, b = (int(x) for x in rest.split("-"))
                got = client.request("textDocument/foldingRange", doc)
                if not any(r["startLine"] == a - 1 and r["endLine"] == b - 1 for r in got or []):
                    fail(f"folding {a}-{b}", got)
            elif kind == "selection":
                # `selection L:C -> L:C-L:C`: the chain at the position holds
                # that range, and each range holds the one before it.
                pos, _, want = rest.split()
                s, e = want.split("-")
                got = client.request("textDocument/selectionRange", {**doc, "positions": [position(pos)]})
                chain = []
                r = (got or [None])[0]
                while r:
                    chain.append(r["range"])
                    r = r.get("parent")
                before = lambda a, b: (a["line"], a["character"]) <= (b["line"], b["character"])
                nested = all(before(outer["start"], inner["start"]) and before(inner["end"], outer["end"])
                             for inner, outer in zip(chain, chain[1:]))
                if {"start": position(s), "end": position(e)} not in chain or not nested:
                    fail(f"selection {pos} lacks {want}", got)
            else:
                raise RuntimeError(f"lsp: unknown query {kind} in {path.name}")
        got = sorted(d["range"]["start"]["line"] + 1 for d in diags if d["severity"] == 1)
        if got != sorted(errors):
            fail(f"{path.name} has errors at lines {got}, not {sorted(errors)}", diags)

    diags = open_fixture(main, text)
    queries(main, text, diags)
    # The analysis answering the queries asked for the lenses, semantic
    # tokens and inlay hints again, since an editor asks as a file opens.
    for f in REFRESHED:
        if f"workspace/{f}/refresh" not in client.requests:
            fail(f"no workspace/{f}/refresh after an analysis", client.requests)
    doc = {"textDocument": {"uri": main_uri}}
    got = client.request("workspace/symbol", {"query": "twi"})
    if not any(s["name"] == "twice" and s["location"]["uri"].lower() == helper_uri.lower() for s in got):
        fail("workspace symbol twice", got)

    # An edit into an error, and back, as a burst of changes: the server
    # drops the analysis a change interrupts, and the diagnostics that
    # stand are the last text's.
    broken = text.replace("let sum = add(p.x, p.y);", "let sum = add(p.x, p.y) + \"s\";")
    for version, body in ((2, broken), (3, text), (4, broken)):
        client.send("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": version},
                                               "contentChanges": [{"text": body}]}, request=False)
    diags = client.diagnostics_for(main_uri, lambda d: len(d) > 0)
    if len(diags) != 1 or diags[0]["severity"] != 1 or diags[0]["range"]["start"]["line"] != 30:
        fail("diagnostics after an edit into an error", diags)
    client.send("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 5},
                                           "contentChanges": [{"text": text}]}, request=False)
    client.diagnostics_for(main_uri, lambda d: len(d) == 0)
    # The analysis stands again: a query answers as before.
    got = client.request("textDocument/hover", {**doc, "position": position("33:11")})
    if not got or "let sum: i64" not in got["contents"]["value"]:
        fail("hover after the edits", got)
    # A request right behind the change it asks about is answered from the
    # analysis that change made due, not from the one before it: a line put
    # above everything, and the hover asked at once where `sum` now is.
    client.send("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 6},
                                           "contentChanges": [{"text": "// a line above\n" + text}]}, request=False)
    got = client.request("textDocument/hover", {**doc, "position": position("34:11")})
    if not got or "let sum: i64" not in got["contents"]["value"]:
        fail("hover right after an edit", got)
    client.send("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 7},
                                           "contentChanges": [{"text": text}]}, request=False)
    client.diagnostics_for(main_uri, lambda d: len(d) == 0)
    # A program with errors in two functions, opened as a root of its own,
    # its symbols asked at once: they are its analysis's, not none from
    # before it. Both errors are published, and the queries answer from
    # what was checked.
    recovery = fixtures / "recovery.goose"
    rtext = recovery.read_text(encoding="utf-8")
    client.send("textDocument/didOpen", {"textDocument": {
        "uri": uri_of(recovery), "languageId": "goose", "version": 1, "text": rtext}}, request=False)
    got = client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri_of(recovery)}})
    if not got:
        fail("symbols right after opening", got)
    queries(recovery, rtext, client.diagnostics_for(uri_of(recovery)))
    # Declarations with errors inside them, and an import that finds
    # nothing: each published, and the rest of the program checked.
    broken = fixtures / "broken_decls.goose"
    btext = broken.read_text(encoding="utf-8")
    queries(broken, btext, open_fixture(broken, btext))
    # The names as written where the check rewrites a global's initializer
    # in place, the bindings of the arms a const for generates, and the
    # commas in literals and comments, which are no argument's.
    sindex = fixtures / "source_index.goose"
    stext = sindex.read_text(encoding="utf-8")
    queries(sindex, stext, open_fixture(sindex, stext))
    # UFCS calls on a namespaced type, which resolve in its namespace before
    # the global one: signature help, and the hover and definition of a call
    # the check resolved and of one that matched no overload.
    ufcs = fixtures / "ufcs_ns.goose"
    utext = ufcs.read_text(encoding="utf-8")
    queries(ufcs, utext, open_fixture(ufcs, utext))

    # Statements with errors inside the braces of a struct literal or a
    # match they opened: each published where it is, and nothing after
    # them lost to the function it is in.
    braces = HERE / "errors_tc" / "recovery" / "inside_braces.goose"
    ibtext = braces.read_text(encoding="utf-8")
    queries(braces, ibtext, open_fixture(braces, ibtext))
    # Completion, on a program whose last statement is the `p.` an editor
    # has when it asks.
    complete = fixtures / "complete.goose"
    ctext = complete.read_text(encoding="utf-8")
    queries(complete, ctext, open_fixture(complete, ctext))
    # Generic functions nothing calls, which the server checks as exemplars:
    # the queries answer from them, and one whose body no binding could make
    # right reports nothing, as `goose check` does not (its errors_tc
    # recovery fixture, whose one error is main's).
    for exemplar in (fixtures / "exemplar.goose",
                     HERE / "errors_tc" / "recovery" / "exemplar_unchecked.goose"):
        etext = exemplar.read_text(encoding="utf-8")
        queries(exemplar, etext, open_fixture(exemplar, etext))

    # Roots as files open and close (docs/design/language_server.md §4),
    # judged by the diagnostics an editor has once a request has had the
    # analyses due run, and by the queries answering. A module with an
    # error, opened alone, is a root of its own; a program opened after it
    # takes it over, and its error stays; the program closed, the module is
    # a root again; two programs reaching it report its error once, and it
    # stays when one stops importing it.
    lib, app, app2 = (fixtures / f"roots_{n}.goose" for n in ("lib", "app", "app2"))

    def close_doc(path):
        client.send("textDocument/didClose", {"textDocument": {"uri": uri_of(path)}}, request=False)

    def change_doc(path, body):
        client.send("textDocument/didChange", {"textDocument": {"uri": uri_of(path), "version": 2},
                                               "contentChanges": [{"text": body}]}, request=False)

    def lib_error(when):
        client.request("workspace/symbol", {"query": "nothing is named this"})
        got = client.published.get(uri_of(lib).lower())
        if [d["range"]["start"]["line"] + 1 for d in got or []] != [6]:
            fail(f"roots_lib.goose's diagnostics {when}", got)

    def hover_has(path, pos, want, when):
        got = client.request("textDocument/hover", {"textDocument": {"uri": uri_of(path)}, "position": position(pos)})
        if not got or want not in got["contents"]["value"]:
            fail(f"hover {pos} in {path.name} {when} lacks {want}", got)

    open_fixture(lib, lib.read_text(encoding="utf-8"))
    lib_error("opened alone")
    open_fixture(app, app.read_text(encoding="utf-8"))
    lib_error("once roots_app.goose, which imports it, is open")
    close_doc(app)
    lib_error("once roots_app.goose closes")
    hover_has(lib, "6:4", "fn roots_lib::helperfn", "once roots_app.goose closes")
    if not client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri_of(lib)}}):
        fail("roots_lib.goose's symbols once roots_app.goose closes", None)
    atext = app.read_text(encoding="utf-8")
    open_fixture(app, atext)
    open_fixture(app2, app2.read_text(encoding="utf-8"))
    lib_error("with two programs importing it")
    change_doc(app, atext.replace("import roots_lib;", "").replace("roots_lib::helperfn(1)", "1"))
    lib_error("once one of the two programs stops importing it")
    # The configured entry goes while a file only it reached is open: the
    # file gets a root of its own.
    open_fixture(helper, helper.read_text(encoding="utf-8"))
    close_doc(complete)
    close_doc(main)
    client.send("workspace/didChangeConfiguration", {"settings": {"goose": {"entryFiles": []}}}, request=False)
    hover_has(helper, "4:4", "fn helper::twice", "once basics.goose is no entry file")

    # Formatting, of the editor's text, which is not on disk.
    def apply(text, edits):
        lines = text.split("\n")
        starts = [0]
        for line in lines:
            starts.append(starts[-1] + len(line) + 1)
        offset = lambda pos: min(starts[pos["line"]] + pos["character"], len(text))
        for e in sorted(edits, key=lambda e: offset(e["range"]["start"]), reverse=True):
            text = text[:offset(e["range"]["start"])] + e["newText"] + text[offset(e["range"]["end"]):]
        return text

    unsaved = fixtures / "format_unsaved.goose"
    messy = ("fn  f(a:i64)->i64 {\r\n    a+1\r\n}\r\n\r\n\r\n\r\nfn g() {}\r\n"
             "fn h() {\r\n    let x = 1;\r\n    x\r\n}\r\nfn  k() {}")
    formatted = subprocess.run([str(exe), "format", "-"], input=messy.encode(), capture_output=True).stdout.decode()
    open_fixture(unsaved, messy)
    fmt = lambda: client.request("textDocument/formatting", {"textDocument": {"uri": uri_of(unsaved)},
                                                             "options": {"tabSize": 4, "insertSpaces": True}})
    edits = fmt()
    if not edits or apply(messy, edits).replace("\r\n", "\n") != formatted:
        fail("formatting a document", edits)
    # Only the lines that change: the first two, the blank lines, the last.
    touched = {line for e in edits
               for line in range(e["range"]["start"]["line"],
                                 e["range"]["end"]["line"] + (e["range"]["end"]["character"] > 0))}
    if any(line in touched for line in (2, 6, 7, 8, 9, 10)):
        fail("formatting changes only the lines that change", edits)
    change_doc(unsaved, formatted)
    if fmt() != []:
        fail("formatting a formatted document", fmt())
    change_doc(unsaved, "fn f( {\n")
    if fmt() is not None:
        fail("formatting a document that does not parse", fmt())
    close_doc(unsaved)
    if fmt() is not None:
        fail("formatting a document that is not open", fmt())

    client.request("shutdown", None)
    client.send("exit", request=False)
    code = client.p.wait(timeout=30)
    if code != 0:
        raise RuntimeError(f"goose lsp exited with {code}:\n{tc.decode(client.p.stderr.read())}")

    # Two programs importing one module, both entry files of a client of
    # their own (docs/design/language_server.md §10): the module's twice is
    # renamed, and its references and callers found, in both programs, each
    # place once, though the first program's analysis answers for the
    # module; and its second half, which only a generic function the second
    # program alone instantiates calls, is found called there.
    shared = fixtures / "two_shared.goose"
    two = Client(exe, fixtures)
    two.request("initialize", {"processId": None, "rootUri": uri_of(fixtures), "capabilities": {},
                               "initializationOptions": {"entryFiles": [str(fixtures / "two_a.goose"),
                                                                        str(fixtures / "two_b.goose")]}})
    two.send("initialized", {}, request=False)
    two.send("textDocument/didOpen", {"textDocument": {"uri": uri_of(shared), "languageId": "goose", "version": 1,
                                                       "text": shared.read_text(encoding="utf-8")}}, request=False)
    sdoc = {"textDocument": {"uri": uri_of(shared)}}

    def places(locations):
        """Each location as file:line:column, sorted."""
        return sorted(f"{loc['uri'].rsplit('/', 1)[-1]}:{loc['range']['start']['line'] + 1}:"
                      f"{loc['range']['start']['character'] + 1}" for loc in locations)

    def renamed(pos):
        changes = two.request("textDocument/rename", {**sdoc, "position": position(pos), "newName": "renamed"})
        return places({"uri": uri, "range": e["range"]} for uri, es in changes["changes"].items() for e in es)

    def callers(pos):
        items = two.request("textDocument/prepareCallHierarchy", {**sdoc, "position": position(pos)})
        calls = two.request("callHierarchy/incomingCalls", {"item": items[0]}) if items else None
        return sorted(f"{c['from']['uri'].rsplit('/', 1)[-1]}:{c['from']['name']}@" +
                      ",".join(str(r["start"]["line"] + 1) for r in c["fromRanges"]) for c in calls or [])

    for what, got, want in (
            ("rename 5:4", renamed("5:4"), ["two_a.goose:5:31", "two_b.goose:6:23", "two_shared.goose:5:4"]),
            ("references 5:4", places(two.request("textDocument/references", {
                **sdoc, "position": position("5:4"), "context": {"includeDeclaration": True}})),
             ["two_a.goose:5:19", "two_b.goose:6:11", "two_shared.goose:5:4"]),
            ("incoming 5:4", callers("5:4"), ["two_a.goose:main@5", "two_b.goose:main@6"]),
            ("rename 8:4", renamed("8:4"), ["two_shared.goose:13:44", "two_shared.goose:8:4"]),
            ("incoming 8:4", callers("8:4"), ["two_shared.goose:apply@13"])):
        if got != want:
            fail(f"two entry files: {what} are not {want}", got)
    two.request("shutdown", None)
    two.send("exit", request=False)
    code = two.p.wait(timeout=30)
    if code != 0:
        raise RuntimeError(f"goose lsp exited with {code}:\n{tc.decode(two.p.stderr.read())}")
