"""The language server, `goose lsp` (docs/design/language_server.md), driven
over its own protocol: a client of this runner's starts it on a pipe,
initializes, opens the fixtures in test/lsp/, waits for their diagnostics,
and asks what each fixture's `// lsp:` lines say to ask, comparing the
answers. Then it edits a document into an error and back, checking the
diagnostics follow, with the edits arriving in a burst so that one analysis
is cancelled by the next, and sees the server out."""
import json
import os
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
                pos, _, target = rest.split(" ", 2)
                got = client.request("textDocument/definition", {**doc, "position": position(pos)})
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
                if tokens.get((p["line"], p["character"])) != want:
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
    client.request("shutdown", None)
    client.send("exit", request=False)
    code = client.p.wait(timeout=30)
    if code != 0:
        raise RuntimeError(f"goose lsp exited with {code}:\n{tc.decode(client.p.stderr.read())}")
