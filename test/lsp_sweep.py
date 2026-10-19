"""A robustness and latency sweep of `goose lsp` (docs/design/language_server.md
§12) over the repository's Goose programs: every sample, the goose_in_goose
bootstrap, every stdlib module, every positive fixture, and every errors/ and
errors_tc/ fixture, each opened as a root of its own. For each, the sweep
waits for the diagnostics, then asks hover, definition, completion and
signature help at every identifier start and at every position right after a
`.` (found lexically, outside comments and strings), and the incoming and
outgoing calls of a function named there, asks semantic tokens,
document symbols, folding ranges, inlay hints and code lenses once, sends a
burst of ten random one-character edits and waits for their diagnostics, puts
the text back and waits for the diagnostics to be the first ones again, and
closes the file. It fails on a server exit, a reply that is an error, a
request slower than the limit, or a server that stops answering.

    python test/lsp_sweep.py [--exe build/Release/goose.exe] [--quick]
        [--jobs N] [--limit SECONDS] [--positions N] [--filter TEXT]
        [--group NAME] [--seed N] [--json OUT] [--top N]

--quick sweeps a handful of files at a few dozen positions each, in the time
a test suite can spare; run_tests.py may run it. Without it the whole corpus
is swept (minutes, on `--jobs` servers at once, one per worker process).
"""
import argparse
import json
import os
import queue
import random
import subprocess
import sys
import threading
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(REPO / "scripts"))
sys.path.insert(0, str(HERE))

from lsp_protocol import uri_of  # noqa: E402

# The files a quick sweep covers: one of each kind, the bootstrap among them.
QUICK = [
    "samples/01_tour.goose",
    "samples/18_json.goose",
    "stdlib/vec.goose",
    "test/goose_in_goose/main.goose",
    "test/syntax/parser_all.goose",
    "test/typing/const_params.goose",
    "test/errors/missing_semi.goose",
    "test/errors_tc/recovery/statements.goose",
    "test/errors_tc/recovery/incomplete_dot.goose",
]

PER_POSITION = ("textDocument/hover", "textDocument/definition", "textDocument/completion",
                "textDocument/signatureHelp")
CALL_HIERARCHY = ("callHierarchy/incomingCalls", "callHierarchy/outgoingCalls")
PER_FILE = ("textDocument/semanticTokens/full", "textDocument/documentSymbol",
            "textDocument/foldingRange", "textDocument/inlayHint", "textDocument/codeLens")


def corpus(quick):
    """(group, path) for every root the sweep opens."""
    if quick:
        return [(group_of(REPO / f), REPO / f) for f in QUICK if (REPO / f).exists()]
    files = []
    files += [("samples", f) for f in sorted((REPO / "samples").glob("*.goose"))]
    files += [("goose_in_goose", REPO / "test" / "goose_in_goose" / "main.goose")]
    files += [("stdlib", f) for f in sorted((REPO / "stdlib").glob("*.goose"))]
    files += [("fixtures", f) for f in sorted((REPO / "test").glob("*/*.goose"))
              if f.parent.name not in ("errors", "errors_tc", "lsp", "goose_in_goose")]
    files += [("errors", f) for f in sorted((REPO / "test" / "errors").rglob("*.goose"))]
    files += [("errors_tc", f) for f in sorted((REPO / "test" / "errors_tc").rglob("*.goose"))]
    return files


def group_of(path):
    parts = path.relative_to(REPO).parts
    if parts[0] != "test":
        return parts[0]
    return parts[1] if parts[1] in ("errors", "errors_tc", "goose_in_goose") else "fixtures"


# --- the text ---------------------------------------------------------------------

def is_ident_start(c):
    return c == "_" or "a" <= c <= "z" or "A" <= c <= "Z"


def is_ident_cont(c):
    return is_ident_start(c) or "0" <= c <= "9"


def positions_in(text):
    """The offsets (in code points) of every identifier start and of the
    character after every `.`, outside comments, strings and character
    literals, as the lexer would see them."""
    found = set()
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == "/" and text.startswith("/*", i):
            depth, i = 1, i + 2
            while i < n and depth:
                if text.startswith("/*", i):
                    depth, i = depth + 1, i + 2
                elif text.startswith("*/", i):
                    depth, i = depth - 1, i + 2
                else:
                    i += 1
        elif text.startswith('"""', i):
            j = text.find('"""', i + 3)
            i = n if j < 0 else j + 3
        elif c == '"' or c == "'":
            i += 1
            while i < n and text[i] != c and text[i] != "\n":
                i += 2 if text[i] == "\\" else 1
            i += 1
        elif is_ident_start(c):
            found.add(i)
            while i < n and is_ident_cont(text[i]):
                i += 1
        elif c == ".":
            found.add(i + 1)
            i += 1
        else:
            i += 1
    return sorted(p for p in found if p <= n)


class Text:
    """A document's text with the protocol's positions of its offsets."""

    def __init__(self, text):
        self.text = text
        self.starts = [0]
        for i, c in enumerate(text):
            if c == "\n":
                self.starts.append(i + 1)

    def position(self, offset):
        import bisect
        line = bisect.bisect_right(self.starts, offset) - 1
        prefix = self.text[self.starts[line]:offset]
        return {"line": line, "character": len(prefix.encode("utf-16-le")) // 2}

    def spell(self, offset):
        """1-based line:column, as the fixtures' `// lsp:` lines spell one."""
        p = self.position(offset)
        return f"{p['line'] + 1}:{p['character'] + 1}"


# --- the server -----------------------------------------------------------------------

class Gone(Exception):
    """The server exited."""

    def __init__(self, code, stderr):
        super().__init__(f"the server exited with {code}" + (f":\n{stderr}" if stderr else ""))
        self.code = code


class Hang(Exception):
    """The server gave no reply in time, and was killed."""


class Server:
    """One `goose lsp`, with a thread reading its messages so that a reply
    can be waited for with a timeout."""

    def __init__(self, exe, cwd, env, errpath):
        self.errpath = errpath
        self.errfile = open(errpath, "wb")
        self.p = subprocess.Popen([exe, "lsp"], cwd=cwd, env=env, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=self.errfile)
        self.q = queue.Queue()
        self.seq = 0
        self.pending = []       # Notifications read while waiting for a reply.
        self.requests = []      # The methods of the server's requests, each answered with null.
        self.broken = None      # What the reader could not take as a message, if anything.
        threading.Thread(target=self.reader, daemon=True).start()

    def reader(self):
        """Reads until the server's output ends, or a message is not one:
        the queue then ends with None and `broken` says what came."""
        body = b""
        try:
            while True:
                headers = {}
                while True:
                    line = self.p.stdout.readline()
                    if not line:
                        raise EOFError("the server closed its output")
                    line = line.strip()
                    if not line:
                        break
                    name, value = line.decode().split(":", 1)
                    headers[name.strip()] = value.strip()
                body = self.p.stdout.read(int(headers["Content-Length"]))
                self.q.put((time.perf_counter(), json.loads(body)))
        except EOFError:
            pass
        except Exception as e:
            self.broken = f"{e!r} in a message starting {body[:300]!r}"
        self.q.put((time.perf_counter(), None))

    def alive(self):
        return self.p.poll() is None

    def stderr(self):
        if not self.errfile.closed:
            self.errfile.flush()
        try:
            return Path(self.errpath).read_bytes().decode("utf-8", "replace")[-2000:]
        except OSError:
            return ""

    def kill(self):
        if self.alive():
            self.p.kill()
        self.p.wait()
        self.errfile.close()
        try:
            if Path(self.errpath).stat().st_size == 0:
                os.remove(self.errpath)
        except OSError:
            pass

    def write(self, msg):
        body = json.dumps(msg).encode()
        try:
            self.p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
            self.p.stdin.flush()
        except OSError:
            self.kill()
            raise Gone(self.p.returncode, self.stderr())

    def send(self, method, params=None, request=True):
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        if request:
            self.seq += 1
            msg["id"] = self.seq
        self.write(msg)
        return self.seq if request else None

    def read(self, timeout):
        """The next message other than a request of the server's, with the
        time it arrived."""
        while True:
            try:
                at, m = self.q.get(timeout=timeout)
            except queue.Empty:
                self.kill()
                raise Hang()
            if m is None:
                # Unreadable output from a live server is as good as an exit.
                broken = self.broken
                self.kill()
                raise Gone(self.p.returncode, (broken + "\n" if broken else "") + self.stderr())
            if "method" in m and "id" in m:
                self.requests.append(m["method"])
                self.write({"jsonrpc": "2.0", "id": m["id"], "result": None})
                continue
            return at, m

    def reply(self, rid, timeout):
        while True:
            at, m = self.read(timeout)
            if m.get("id") == rid:
                return at, m
            self.pending.append(m)

    def request(self, method, params, timeout):
        """(seconds taken, the whole reply)."""
        start = time.perf_counter()
        at, m = self.reply(self.send(method, params), timeout)
        return at - start, m

    def notification(self, method, timeout):
        for i, m in enumerate(self.pending):
            if m.get("method") == method:
                return self.pending.pop(i)["params"]
        deadline = time.perf_counter() + timeout
        while True:
            left = deadline - time.perf_counter()
            if left <= 0:
                self.kill()
                raise Hang()
            at, m = self.read(left)
            if m.get("method") == method:
                return m["params"]
            self.pending.append(m)

    def diagnostics_for(self, uri, timeout, accept=lambda diags: True):
        """The next diagnostics published for uri that `accept` takes."""
        deadline = time.perf_counter() + timeout
        while True:
            left = deadline - time.perf_counter()
            if left <= 0:
                self.kill()
                raise Hang()
            params = self.notification("textDocument/publishDiagnostics", left)
            if params["uri"].lower() == uri.lower() and accept(params["diagnostics"]):
                return params["diagnostics"]


# --- one worker, one server ------------------------------------------------------------

STATE = {}


def init_worker(exe, env, options):
    STATE.update(exe=exe, env=env, options=options, server=None)
    import atexit
    atexit.register(lambda: STATE["server"] and STATE["server"].kill())


def server():
    s = STATE["server"]
    if s is not None and s.alive():
        return s
    if s is not None:
        s.kill()
    errpath = Path(STATE["options"]["errdir"]) / f"lsp_sweep_{os.getpid()}.err"
    s = Server(STATE["exe"], str(REPO), STATE["env"], errpath)
    STATE["server"] = s
    s.request("initialize", {"processId": None, "rootUri": uri_of(REPO),
                             "capabilities": {"workspace": {"codeLens": {"refreshSupport": True}}},
                             "initializationOptions": {"commands": ["goose.run", "goose.showInstantiations"]}},
              timeout=STATE["options"]["hang"])
    s.send("initialized", {}, request=False)
    return s


def edited(rng, text, step):
    """The text after one random edit: a character inserted (odd steps) or
    deleted (even steps) at a random place."""
    if step % 2 or not text:
        at = rng.randrange(len(text) + 1)
        return text[:at] + rng.choice("a(){};.\"'x1 \n") + text[at:]
    at = rng.randrange(len(text))
    return text[:at] + text[at + 1:]


def sweep_file(item):
    """Opens one root and asks everything of it; the record of what happened."""
    group, path = item
    opts = STATE["options"]
    limit, hang, analysis_timeout = opts["limit"], opts["hang"], opts["analysis"]
    rel = path.relative_to(REPO).as_posix()
    rec = {"file": rel, "group": group, "requests": 0, "slowest_ms": 0.0, "slowest": "",
           "analysis_ms": None, "edit_analysis_ms": None, "errors": 0, "first_error": "", "positions": 0,
           "failures": [], "exit": None, "seconds": 0.0}
    started = time.perf_counter()
    raw = path.read_bytes()
    text = raw.decode("utf-8", "replace")
    doc = Text(text)
    uri = uri_of(path)
    td = {"textDocument": {"uri": uri}}
    at = "start"

    def timed(method, params):
        nonlocal at
        at = f"{method} at {params['position']['line'] + 1}:{params['position']['character'] + 1}" \
            if "position" in params else method
        seconds, m = s.request(method, params, hang)
        rec["requests"] += 1
        ms = seconds * 1000
        if ms > rec["slowest_ms"]:
            rec["slowest_ms"], rec["slowest"] = ms, at
        if "error" in m:
            rec["failures"].append(f"{at}: error reply {json.dumps(m['error'])[:300]}")
        if ms > limit * 1000:
            rec["failures"].append(f"{at}: {ms:.0f} ms")
        return m

    try:
        s = server()
        at = "didOpen"
        t0 = time.perf_counter()
        s.send("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "goose",
                                                          "version": 1, "text": text}}, request=False)
        at = "the diagnostics after didOpen"
        first = s.diagnostics_for(uri, analysis_timeout)
        rec["analysis_ms"] = (time.perf_counter() - t0) * 1000
        errors = [d for d in first if d.get("severity") == 1]
        rec["errors"] = len(errors)
        if errors:
            rec["first_error"] = f"{errors[0]['range']['start']['line'] + 1}: {errors[0]['message'][:120]}"
        positions = positions_in(text)
        cap = opts["positions"]
        if cap and len(positions) > cap:
            positions = [positions[i * len(positions) // cap] for i in range(cap)]
        rec["positions"] = len(positions)
        for off in positions:
            p = {**td, "position": doc.position(off)}
            for method in PER_POSITION:
                timed(method, p)
            for item in timed("textDocument/prepareCallHierarchy", p).get("result") or []:
                for method in CALL_HIERARCHY:
                    timed(method, {"item": item})
        whole = {"start": {"line": 0, "character": 0}, "end": {"line": len(doc.starts), "character": 0}}
        for method in PER_FILE:
            timed(method, {**td, "range": whole} if method == "textDocument/inlayHint" else td)
        if not opts["edits"]:
            s.send("textDocument/didClose", td, request=False)
            return rec
        # A burst of edits, their diagnostics, and the text put back, whose
        # diagnostics are the first ones again.
        rng = random.Random(f"{opts['seed']}:{rel}")
        body = text
        for step in range(1, 11):
            body = edited(rng, body, step)
            at = f"didChange {step} of the burst"
            s.send("textDocument/didChange", {"textDocument": {"uri": uri, "version": step + 1},
                                              "contentChanges": [{"text": body}]}, request=False)
        at = "the diagnostics after the burst of edits"
        t0 = time.perf_counter()
        s.diagnostics_for(uri, analysis_timeout)
        rec["edit_analysis_ms"] = (time.perf_counter() - t0) * 1000
        s.send("textDocument/didChange", {"textDocument": {"uri": uri, "version": 12},
                                          "contentChanges": [{"text": text}]}, request=False)
        at = "the diagnostics after the text was put back"
        try:
            s.diagnostics_for(uri, analysis_timeout, lambda d: d == first)
        except Hang:
            rec["failures"].append(f"{at}: not the first diagnostics again within {analysis_timeout:.0f}s")
            return rec
        if positions:
            timed("textDocument/hover", {**td, "position": doc.position(positions[0])})
        at = "didClose"
        s.send("textDocument/didClose", td, request=False)
    except Gone as e:
        rec["exit"] = e.code
        rec["failures"].append(f"{at}: {e}")
    except Hang:
        rec["failures"].append(f"{at}: no reply in {hang:.0f}s (server killed)")
    except Exception as e:
        rec["failures"].append(f"{at}: the sweep itself failed: {e!r}")
    finally:
        rec["seconds"] = time.perf_counter() - started
    return rec


# --- the sweep ----------------------------------------------------------------------------

def run_sweep(exe, quick=False, jobs=None, limit=2.0, positions=None, filt=None, group=None,
              seed=1, edits=True, hang=60.0, analysis=120.0, errdir=None, goose_home=None,
              progress=None):
    """Sweeps the corpus; (records, failures) with each failure a line."""
    exe = str(Path(exe).resolve())
    files = corpus(quick)
    if filt:
        files = [f for f in files if filt in f[1].relative_to(REPO).as_posix()]
    if group:
        files = [f for f in files if f[0] == group]
    env = dict(os.environ)
    home = goose_home or env.get("GOOSE_HOME") or (REPO / "build" / "goose-home")
    if Path(home).is_dir():
        env["GOOSE_HOME"] = str(home)
    errdir = Path(errdir or (REPO / "build" / "lsp_sweep"))
    errdir.mkdir(parents=True, exist_ok=True)
    options = {"limit": limit, "hang": hang, "analysis": analysis, "seed": seed, "edits": edits,
               "positions": positions if positions is not None else (60 if quick else 0),
               "errdir": str(errdir)}
    jobs = jobs or min(8, os.cpu_count() or 1)
    records = []
    # The big files first, so no worker is left with one at the end.
    order = sorted(files, key=lambda f: -f[1].stat().st_size)
    with ProcessPoolExecutor(max_workers=min(jobs, max(1, len(order))), initializer=init_worker,
                             initargs=(exe, env, options)) as pool:
        for rec in pool.map(sweep_file, order, chunksize=1):
            records.append(rec)
            if progress:
                progress(rec)
    failures = [f"{r['file']}: {f}" for r in records for f in r["failures"]]
    return records, failures


def report(records, failures, seconds, top=20):
    """The summary: per group, the slowest files, the slowest request per
    method, and every failure."""
    out = []
    groups = {}
    for r in records:
        g = groups.setdefault(r["group"], {"files": 0, "requests": 0, "errors": 0, "failures": 0})
        g["files"] += 1
        g["requests"] += r["requests"]
        g["errors"] += r["errors"] > 0
        g["failures"] += len(r["failures"])
    out.append(f"{'group':15} {'files':>6} {'requests':>9} {'with errors':>12} {'failures':>9}")
    for name, g in sorted(groups.items()):
        out.append(f"{name:15} {g['files']:6} {g['requests']:9} {g['errors']:12} {g['failures']:9}")
    by_slowest = sorted(records, key=lambda r: -r["slowest_ms"])
    out.append("")
    out.append(f"{'slowest files':48} {'positions':>9} {'requests':>8} {'analysis':>9} {'edits':>8}  slowest request")
    for r in by_slowest[:top]:
        an = f"{r['analysis_ms']:.0f} ms" if r["analysis_ms"] is not None else "-"
        ed = f"{r['edit_analysis_ms']:.0f} ms" if r["edit_analysis_ms"] is not None else "-"
        out.append(f"{r['file'][:48]:48} {r['positions']:9} {r['requests']:8} {an:>9} {ed:>8}  "
                   f"{r['slowest_ms']:.1f} ms {r['slowest']}")
    per_method = {}
    for r in records:
        method = r["slowest"].split(" at ")[0]
        if method and r["slowest_ms"] > per_method.get(method, (0, ""))[0]:
            per_method[method] = (r["slowest_ms"], f"{r['file']} {r['slowest'].split(' at ')[-1]}")
    out.append("")
    for method, (ms, where) in sorted(per_method.items(), key=lambda kv: -kv[1][0]):
        out.append(f"slowest {method.split('/')[-1]:18} {ms:8.1f} ms  {where}")
    slowest_analysis = max(records, key=lambda r: r["analysis_ms"] or 0, default=None)
    requests = sum(r["requests"] for r in records)
    out.append("")
    summary = (f"{len(records)} files, {requests} requests, slowest request "
               f"{by_slowest[0]['slowest_ms']:.1f} ms ({by_slowest[0]['file']} {by_slowest[0]['slowest']}), "
               if records else "no files, ")
    if slowest_analysis and slowest_analysis["analysis_ms"] is not None:
        summary += f"slowest analysis {slowest_analysis['analysis_ms'] / 1000:.2f} s ({slowest_analysis['file']}), "
    summary += f"{seconds:.1f} s in all"
    out.append(summary)
    if failures:
        out.append("")
        out.append(f"{len(failures)} failure(s):")
        out.extend(f"  {f}" for f in failures)
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", default=str(REPO / "build" / "Release" / "goose.exe"))
    ap.add_argument("--quick", action="store_true", help="a handful of files, a few dozen positions each")
    ap.add_argument("--jobs", type=int, help="servers at once (default: up to 8, one per core)")
    ap.add_argument("--limit", type=float, default=2.0, help="seconds a request may take (default 2)")
    ap.add_argument("--positions", type=int, help="positions per file at most, spread over the file "
                                                 "(default: all; 60 with --quick)")
    ap.add_argument("--filter", help="only files whose path has this text")
    ap.add_argument("--group", help="only this group: samples, goose_in_goose, stdlib, fixtures, errors, errors_tc")
    ap.add_argument("--seed", type=int, default=1, help="of the random edits")
    ap.add_argument("--no-edits", action="store_true", help="skip the burst of edits")
    ap.add_argument("--hang", type=float, default=60.0, help="seconds without a reply before the server is killed")
    ap.add_argument("--analysis", type=float, default=120.0, help="seconds to wait for diagnostics")
    ap.add_argument("--goose-home", help="GOOSE_HOME for the servers (default: $GOOSE_HOME, else build/goose-home)")
    ap.add_argument("--json", help="write the per-file records here")
    ap.add_argument("--top", type=int, default=20, help="files in the slowest-first listing")
    ap.add_argument("--verbose", action="store_true", help="a line per file as it finishes")
    args = ap.parse_args()
    if not Path(args.exe).exists():
        sys.exit(f"no compiler at {args.exe}")
    started = time.perf_counter()

    done = 0

    def progress(rec):
        nonlocal done
        done += 1
        if args.verbose or rec["failures"]:
            print(f"{rec['file']}: {rec['requests']} requests, slowest {rec['slowest_ms']:.1f} ms"
                  + (f", {len(rec['failures'])} failure(s)" if rec["failures"] else ""), flush=True)
        elif done % 200 == 0:
            print(f"... {done} files", file=sys.stderr, flush=True)

    records, failures = run_sweep(args.exe, quick=args.quick, jobs=args.jobs, limit=args.limit,
                                  positions=args.positions, filt=args.filter, group=args.group,
                                  seed=args.seed, edits=not args.no_edits, hang=args.hang,
                                  analysis=args.analysis, goose_home=args.goose_home, progress=progress)
    seconds = time.perf_counter() - started
    if args.json:
        Path(args.json).write_text(json.dumps(records, indent=1), encoding="utf-8")
    print(report(records, failures, seconds, args.top))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
