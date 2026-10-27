#!/usr/bin/env python3
"""Goose benchmark harness.

For every benchmark in the manifest below, at every requested size, under
every requested toolchain, this builds each implementation, runs it, and
records wall time and peak working set. Every implementation of one benchmark
must print the same checksum lines; a mismatch is reported and the row is
marked, because a faster implementation that computes something else is not a
result.

Both C toolchains are found on the machine this runs on: on Windows they are
the two that live in the Visual Studio install, `cl` and the bundled clang-cl,
so that both build against the same headers, libraries and CRT; elsewhere they
are gcc and clang. Both languages are built by both, which is what makes "does
the backend explain this?" answerable.

Timing is whole-process wall clock, deliberately including input generation,
construction and teardown. The startup floor is measured separately and
printed alongside, so the reader can see how much of a short run is process
creation. Goose also provides os.clock_ns for separate kernel experiments.

Where the compiler has the TinyCC backend, every Goose row is measured a second
way as well: compiled and run inside the compiler, with no C file and no
external toolchain. Those go in a table of their own at the end. They are not
comparable with the C++ and Rust rows -- the time includes compiling the
program, and TinyCC is a one-pass compiler that does not optimize -- so they
are kept out of every comparison against another language. That time also
includes the compiler's own front end, so pass `--exe` a release build of it
or those rows measure an unoptimized compiler.

  python bench/run_bench.py                          # everything
  python bench/run_bench.py --only tree,sexp         # re-measure a subset; the report
                                                     # still shows everything measured
  python bench/run_bench.py --toolchains clang       # one toolchain
  python bench/run_bench.py --sizes small --reps 1   # a quick pass
  python bench/run_bench.py --skip-build             # re-run what is already built
  python bench/run_bench.py --report-only            # regenerate results.md from saved
                                                     # measurements, e.g. after editing
                                                     # notes.md
"""

import argparse
import json
import math
import re
import shutil
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import toolchain as tc

HERE = Path(__file__).resolve().parent
GENDIR = HERE / "gen"
GOOSEDIR = HERE / "goose"
CPPDIR = HERE / "cpp"
RUSTDIR = HERE / "rust"

# Address space reserved per Goose data stack. Explicitly use the runtime's
# 2 GB default for reproducibility (up to 536 MB is used on one stack); it leaves
# 4x headroom and is the largest reservation under which a u32 relative
# reference stores without a range check (spec 3.9).
STACK_RESERVE = "GS_STACK_RESERVE=2147483648ull"

SIZE_NAMES = ["small", "medium", "large"]

# Rust is not built by the C/C++ toolchains, so its measurements are filed under
# a toolchain of their own and its rows carry one time, not one per backend.
RUST_TC = "rustc"

# The in-process TinyCC backend, filed the same way: one time per Goose row,
# under a key that is deliberately not one of the C toolchains, so it stays out
# of every cross-language comparison.
JIT_TC = "goose-jit"

# --- manifest ----------------------------------------------------------------
# sizes:  the value substituted for the `// BENCH_N` line, in small, medium,
#         large order. Goose and Rust take it by rewriting that line in a copy
#         of the source; C++ takes it as -DBENCH_N.
# param:  what that value is called in the table header; "N" if omitted.
# goose:  implementations built from bench/goose/<file>.
# cpp:    implementations built from bench/cpp/<file> with -DVARIANT=<variant>.
# rust:   implementations built from bench/rust/<file>, one file per row.
#         Rust is built by rustc only, so its rows carry a single time rather
#         than one per C/C++ toolchain.

BENCHMARKS = [
    dict(
        name="sum",
        what="Control: build a flat i32 array, then scan it eight times.",
        sizes=[1000000, 16000000, 128000000],
        goose=[dict(label="goose grow-only array", file="sum.goose")],
        cpp=[dict(label="cpp vector", file="sum.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp vector+reserve", file="sum.cpp", variant=1, tier="expert")],
        rust=[dict(label="rust Vec, collect", file="sum.rs")],
        summary=(
            "This control uses the same flat scalar layout in all three languages. The "
            "unreserved C++ vector pays for growth and spare capacity; the reserved "
            "vector and Rust collect over a TrustedLen iterator allocate the exact size. "
            "Whole-process time includes both building the array and scanning it."),
    ),
    dict(
        name="push",
        what="Push N records while keeping pointers to every 64th one.",
        sizes=[1000000, 16000000, 64000000],
        goose=[dict(label="goose array, typed references", file="push.goose")],
        cpp=[dict(label="cpp vector, indices", file="push.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp vector+reserve, indices", file="push.cpp", variant=1, tier="expert"),
             dict(label="cpp deque, pointers", file="push.cpp", variant=2, tier="idiomatic")],
        rust=[dict(label="rust Vec+reserve, indices", file="push.rs")],
        summary=(
            "Goose retains typed Item& references across later pushes. The C++ vector and "
            "safe Rust Vec rows retain indices instead; the pointer-stable C++ deque has "
            "a different allocation and locality cost. Compare the reserved vector with "
            "Goose to separate growth strategy from the cost of building records."),
    ),
    dict(
        name="strlist",
        what="Split a text into N words, keep them as a list, scan it four times.",
        sizes=[200000, 2000000, 8000000],
        goose=[dict(label="goose inline strings, owned", file="strlist.goose")],
        cpp=[dict(label="cpp vector<string>", file="strlist.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp vector<string_view>", file="strlist.cpp", variant=1, tier="expert"),
             dict(label="cpp flat offsets", file="strlist.cpp", variant=2, tier="expert")],
        rust=[dict(label="rust Vec<String>", file="strlist_owned.rs"),
              dict(label="rust Vec<&str>", file="strlist_borrowed.rs")],
        summary=(
            "Goose, vector<string>, and Vec<String> own the split words. The string_view, "
            "flat-offset, and Vec<&str> rows borrow the input instead, so their lifetime "
            "and ownership requirements differ. Goose uses the standard library "
            "each_split and packs owned strings into one contiguous list."),
    ),
    dict(
        name="records",
        what="Build a log of N variant records, aggregate it four times.",
        sizes=[500000, 4000000, 16000000],
        goose=[dict(label="goose variable enum", file="records_var.goose"),
               dict(label="goose fixed enum", file="records_fixed.goose")],
        cpp=[dict(label="cpp virtual + unique_ptr", file="records.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp variant + string", file="records.cpp", variant=1, tier="idiomatic"),
             dict(label="cpp variant + buffer", file="records.cpp", variant=2, tier="expert")],
        rust=[dict(label="rust enum + String", file="records.rs")],
        summary=(
            "The variable enum stores each record at the size of its own variant, "
            "including inline text. The fixed-enum Goose row controls for that "
            "representation choice. C++ variant and Rust enum reserve space for the "
            "largest payload in every element; their owned strings may also allocate "
            "separately."),
    ),
    dict(
        name="tree",
        what="Build a complete binary tree of the given depth, sum it eight times.",
        param="depth",
        sizes=[16, 20, 24],
        goose=[dict(label="goose pool + typed relative refs", file="tree.goose")],
        cpp=[dict(label="cpp unique_ptr", file="tree.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp new/delete", file="tree.cpp", variant=1, tier="idiomatic"),
             dict(label="cpp arena + indices", file="tree.cpp", variant=2, tier="expert")],
        rust=[dict(label="rust Box nodes", file="tree_box.rs"),
              dict(label="rust arena + indices", file="tree_arena.rs")],
        summary=(
            "The arena rows avoid per-node allocation and recursive destruction. Goose "
            "links its 12-byte nodes with typed nullable relative references; C++ and "
            "safe Rust use u32 indices. Compare bintrees for the cost of repeatedly "
            "building and discarding this shape."),
    ),
    dict(
        name="interp",
        what=(
            "Build an expression tree of the given depth, evaluate it eight "
            "times."),
        param="depth",
        sizes=[16, 20, 24],
        goose=[dict(label="goose case functions + relative refs", file="interp.goose")],
        cpp=[dict(label="cpp virtual + unique_ptr", file="interp.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp variant + arena", file="interp.cpp", variant=1, tier="expert"),
             dict(label="cpp tagged union + arena", file="interp.cpp", variant=2, tier="expert")],
        rust=[dict(label="rust enum + Box", file="interp_box.rs"),
              dict(label="rust enum + arena indices", file="interp_arena.rs")],
        summary=(
            "Variable-size leaves and binary nodes use different amounts of storage in "
            "Goose. The C++ tagged union and Rust enum arenas reserve the largest payload "
            "in every node. The owning-pointer rows additionally pay for allocation and "
            "recursive destruction."),
    ),
    dict(
        name="graph",
        what=(
            "Build adjacency for V vertices and 8V edges, then BFS "
            "from four sources."),
        param="V",
        sizes=[100000, 500000, 2000000],
        goose=[dict(label="goose one-pass, typed relative refs", file="graph.goose"),
               dict(label="goose CSR two-pass", file="graph_csr.goose")],
        cpp=[dict(label="cpp vector<vector>", file="graph.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp CSR two-pass", file="graph.cpp", variant=1, tier="expert"),
             dict(label="cpp arena + indices", file="graph.cpp", variant=2, tier="expert")],
        rust=[dict(label="rust Vec<Vec>", file="graph_nested.rs"),
              dict(label="rust CSR two-pass", file="graph_csr.rs"),
              dict(label="rust arena + indices", file="graph_arena.rs")],
        summary=(
            "CSR and linked adjacency have very different traversal locality in every "
            "language; compare matching representations before attributing a gap to the "
            "compiler. Both Goose BFS functions are checkfree: u32 vertex index types "
            "validate destinations on insertion, and CSR bound types validate offsets "
            "during construction. These retain the original four-byte entries. "
            "Construction and distance-array resets remain inside the measured process. "
            "Random-access timings vary substantially between runs."),
    ),
    dict(
        name="words",
        what="Count word frequencies over a text of N words.",
        sizes=[500000, 4000000, 16000000],
        goose=[dict(label="goose slices + open addressing", file="words.goose"),
               dict(label="goose std dictionary", file="words_dictionary.goose")],
        cpp=[dict(label="cpp unordered_map<string>", file="words.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp unordered_map<string_view>", file="words.cpp", variant=1, tier="idiomatic"),
             dict(label="cpp open addressing", file="words.cpp", variant=2, tier="expert")],
        rust=[dict(label="rust HashMap<&str>", file="words_hashmap.rs"),
              dict(label="rust open addressing", file="words_open.rs")],
        summary=(
            "The hand-written Goose table and std dictionary differ in capacity and "
            "load-factor policy as well as implementation. Goose uses the standard "
            "library hash; the C++ and Rust open-addressed tables use FNV-1a, while Rust "
            "HashMap uses its default keyed hasher. The timing comparison therefore "
            "includes different hash functions. The dictionary row measures the "
            "convenience and memory tradeoff of the standard container."),
    ),
    dict(
        name="particles",
        what="Integrate N particles for 200 steps of f32 vector math.",
        sizes=[100000, 1000000, 4000000],
        goose=[dict(label="goose AoS, elementwise", file="particles.goose"),
               dict(label="goose AoS, per component", file="particles_scalar.goose")],
        cpp=[dict(label="cpp AoS, per component", file="particles.cpp", variant=0, tier="expert"),
             dict(label="cpp AoS, elementwise", file="particles.cpp", variant=2, tier="idiomatic"),
             dict(label="cpp SoA", file="particles.cpp", variant=1, tier="expert")],
        rust=[dict(label="rust AoS, elementwise", file="particles.rs"),
              dict(label="rust AoS, per component", file="particles_scalar.rs")],
        summary=(
            "This flat floating-point workload offers little representation advantage. "
            "Compare elementwise and per-component rows to see the cost of expression "
            "style under each backend. The C++ struct-of-arrays row changes layout as "
            "well. All arithmetic is exact in binary32, so checksum equality is required "
            "across implementations."),
    ),
    dict(
        name="sexp",
        what=(
            "Generate N s-expression forms, parse them into a tree, walk it four "
            "times."),
        sizes=[20000, 100000, 300000],
        goose=[dict(label="goose pool + refs, inline text", file="sexp.goose")],
        cpp=[dict(label="cpp unique_ptr + string", file="sexp.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp arena + string_view", file="sexp.cpp", variant=1, tier="expert")],
        rust=[dict(label="rust enum + Box + String", file="sexp_box.rs"),
              dict(label="rust arena + &str", file="sexp_arena.rs")],
        summary=(
            "Goose stores symbol text inline in variable-size nodes and links them with "
            "four-byte relative references. The C++ and Rust arenas borrow symbol text "
            "from the completed input; their owning-pointer rows allocate nodes and "
            "strings separately. Input generation, parsing, four walks, and teardown are "
            "all timed."),
    ),
    dict(
        name="lru",
        what=(
            "An LRU cache of capacity N/8 under N skewed lookups, with an "
            "invalidation every 16th operation."),
        sizes=[2000000, 8000000, 32000000],
        goose=[dict(label="goose reusable pool + pool-relative refs", file="lru.goose")],
        cpp=[dict(label="cpp list + unordered_map", file="lru.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp arena + open addressing", file="lru.cpp", variant=1, tier="expert")],
        rust=[dict(label="rust HashMap + arena", file="lru_hashmap.rs"),
              dict(label="rust open addressing + arena", file="lru_open.rs")],
        summary=(
            "Goose uses a reusable pool with four-byte pool-relative references in both "
            "nodes and map slots. The C++ and Rust expert rows use arena indices; the "
            "idiomatic C++ row uses list plus unordered_map. Backend differences and "
            "cache-sensitive random access matter here. The separate lru_indices.goose "
            "experiment is not a headline row."),
    ),
    dict(
        name="scene",
        what=(
            "Build a scene graph of the given depth, then animate it and "
            "recompute world transforms for 16 frames."),
        param="depth",
        sizes=[12, 14, 16],
        goose=[dict(label="goose pool + inline child refs", file="scene.goose")],
        cpp=[dict(label="cpp unique_ptr children", file="scene.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp arena + child indices", file="scene.cpp", variant=1, tier="expert")],
        rust=[dict(label="rust Box children", file="scene_box.rs"),
              dict(label="rust arena + child indices", file="scene_arena.rs")],
        summary=(
            "Goose stores child references inline in each 117-byte node and composes "
            "transforms directly through references. The Rust arena uses child indices "
            "and a temporary transform to work within its borrowing rules. The Goose "
            "compose and recursive update functions are checkfree, which protects the "
            "matrix kernel and its traversal against bounds-check regressions."),
    ),
    dict(
        name="calc",
        what=(
            "Generate, parse and evaluate N small arithmetic expressions, a third "
            "of them malformed."),
        sizes=[100000, 400000, 1600000],
        goose=[dict(label="goose local pool + return from", file="calc.goose")],
        cpp=[dict(label="cpp unique_ptr + exceptions", file="calc.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp arena + error codes", file="calc.cpp", variant=1, tier="expert")],
        rust=[dict(label="rust Box + Result", file="calc_box.rs"),
              dict(label="rust arena + Result", file="calc_arena.rs")],
        summary=(
            "The workload includes malformed input and division by zero, so error "
            "propagation is part of the comparison. Goose uses a local pool, varint "
            "number payloads, a global parse cursor, and return from; C++ compares "
            "exceptions with explicit error codes, and Rust uses Result. This refresh "
            "does not isolate the individual costs of those choices."),
    ),
    dict(
        name="bintrees",
        what=(
            "The Benchmarks Game binary-trees: build, check and discard many "
            "trees up to the given depth."),
        param="depth",
        sizes=[15, 17, 19],
        goose=[dict(label="goose local pool per tree", file="bintrees.goose")],
        cpp=[dict(label="cpp new/delete", file="bintrees.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp vector arena", file="bintrees.cpp", variant=1, tier="expert")],
        rust=[dict(label="rust Box", file="bintrees_box.rs"),
              dict(label="rust Vec arena", file="bintrees_arena.rs")],
        summary=(
            "This repeatedly builds and discards trees rather than retaining one large "
            "tree. Goose releases a local pool by resetting its data-stack pointer; "
            "vector arenas reuse their capacity, while new/delete and Box pay per-node "
            "allocation and teardown. The arena comparisons are the relevant controls for "
            "the allocator advantage."),
    ),
    dict(
        name="respond",
        what=(
            "Build and render N JSON responses, each a DTO holding a name and a "
            "list of line items."),
        sizes=[100000, 400000, 1600000],
        goose=[dict(label="goose inline DTO", file="respond.goose"),
               dict(label="goose streaming", file="respond_stream.goose")],
        cpp=[dict(label="cpp DTO + string", file="respond.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp streaming", file="respond.cpp", variant=1, tier="expert")],
        rust=[dict(label="rust DTO + String", file="respond_dto.rs"),
              dict(label="rust streaming", file="respond_stream.rs")],
        summary=(
            "The DTO rows construct an object before rendering; streaming rows write the "
            "same response without constructing it. Goose keeps the DTO name, item list, "
            "and per-item text inline and performs no heap allocation. All rows checksum "
            "the rendered bytes with FNV-1a, including the Goose implementations."),
    ),
    dict(
        name="blur",
        what="16 passes of a 3x3 blur over a WxW 8-bit image.",
        param="W",
        sizes=[1024, 2048, 8192],
        goose=[dict(label="goose flat indexing", file="blur.goose"),
               dict(label="goose row slices", file="blur_rows.goose")],
        cpp=[dict(label="cpp flat indexing", file="blur.cpp", variant=0, tier="idiomatic"),
             dict(label="cpp row pointers", file="blur.cpp", variant=1, tier="expert")],
        rust=[dict(label="rust flat indexing", file="blur_index.rs"),
              dict(label="rust row slices", file="blur_windows.rs")],
        summary=(
            "Both Goose kernels are checkfree, including the row-slice constructions, so "
            "a backend gap is not evidence of surviving bounds checks. Row slices change "
            "the information available to the C compiler for alias analysis and "
            "vectorisation. Compare flat indexing and row-based implementations "
            "separately; all perform the same stencil and include image generation."),
    ),
]

# --- build helpers -----------------------------------------------------------

def write_sized(src, dst, n, pattern):
    """A copy of `src` with the number on its `// BENCH_N` line replaced. Goose
    and Rust have no preprocessor to take a -D through, so the size is baked in
    by rewriting the one line that declares it."""
    lines = tc.decode(Path(src).read_bytes()).split("\n")
    hit = False
    out = []
    for line in lines:
        m = re.match(pattern, line) if not hit else None
        if m:
            hit = True
            out.append(f"{m.group(1)}{n}{m.group(3)}")
        else:
            out.append(line)
    if not hit:
        raise SystemExit(f"no '// BENCH_N' line in {src}")
    tc.write_text(dst, "\n".join(out))


GOOSE_N = r"^(\s*let\s+\w+\s*=\s*)(\d+)(\s*;.*//\s*BENCH_N.*)$"
RUST_N = r"^(\s*const\s+\w+\s*:\s*\w+\s*=\s*)(\d+)(\s*;.*//\s*BENCH_N.*)$"


class Harness:
    def __init__(self, args, ccs, active, rustc):
        self.args = args
        self.ccs = ccs
        self.active = active
        self.rustc = rustc
        # The Goose source is sized once per (benchmark, size) and compiled to
        # C once; each toolchain then builds that same C, so the two rows
        # differ only in the backend.
        self.sized = set()
        self.generated = set()

    def goose_source(self, file, n, base):
        """The benchmark's Goose source with its size baked in, written once
        per (benchmark, size). Both backends start from this same file."""
        gsrc = GENDIR / f"{base}.goose"
        if base not in self.sized:
            write_sized(GOOSEDIR / file, gsrc, n, GOOSE_N)
            self.sized.add(base)
        return gsrc

    def build_goose(self, file, n, tag, tcname):
        base = re.sub(re.escape("_" + tcname) + "$", "", tag)
        cfile = GENDIR / f"{base}.c"
        # Generated once per (benchmark, size) and reused across toolchains,
        # but only within one run: a stale .c left over from a previous run
        # would silently benchmark the previous version of the source.
        if base not in self.generated:
            gsrc = self.goose_source(file, n, base)
            # One translation unit, as the measurements always were: every
            # runtime function the program calls is the C compiler's to
            # inline.
            code, out, err = tc.run_capture([self.args.exe, "-O2", "--standalone", "-o", cfile,
                                             gsrc])
            tc.write_text(GENDIR / f"{base}.goose.log", out + err)
            if code != 0:
                return None, f"goose compile failed, see {base}.goose.log"
            self.generated.add(base)
        exe = GENDIR / (tag + tc.EXE_SUFFIX)
        ok, _ = self.ccs[tcname].compile(cfile, exe, opt=2, defines=[STACK_RESERVE],
                                         log=GENDIR / f"{tag}.cc.log")
        return (exe, None) if ok else (None, f"{tcname} failed, see {tag}.cc.log")

    def build_cpp(self, file, variant, n, tag, tcname):
        exe = GENDIR / (tag + tc.EXE_SUFFIX)
        ok, _ = self.ccs[tcname].compile(CPPDIR / file, exe, opt=2, cpp=True,
                                         defines=[f"BENCH_N={n}", f"VARIANT={variant}"],
                                         log=GENDIR / f"{tag}.cc.log")
        return (exe, None) if ok else (None, f"{tcname} failed, see {tag}.cc.log")

    # `-C codegen-units=1` is what makes this comparable to the C++ rows: each
    # of those is one translation unit compiled whole, and the rustc default of
    # 16 codegen units would deny Rust the same cross-function view. Everything
    # else is left at the defaults a Rust program actually ships with -- bounds
    # checks on, unwinding panics, no target-cpu beyond the x86-64 baseline the
    # C++ rows also build for.
    def build_rust(self, file, n, tag):
        src = GENDIR / f"{tag}.rs"
        write_sized(RUSTDIR / file, src, n, RUST_N)
        exe = GENDIR / (tag + tc.EXE_SUFFIX)
        code, out, err = tc.run_capture([self.rustc, "-O", "-C", "codegen-units=1",
                                         "-o", exe, src])
        tc.write_text(GENDIR / f"{tag}.rs.log", out + err)
        return (exe, None) if code == 0 else (None, f"rustc failed, see {tag}.rs.log")


def measure_exe(exe, reps, args=()):
    """Best time and worst peak over `reps` runs.

    Two discarded warm-up runs first: a freshly written executable is paged in
    and scanned by the OS on first execution, which on some machines costs up
    to 10x the run itself and takes about three executions to settle. It hits
    clang-linked binaries far harder than MSVC-linked ones, so without this the
    toolchain comparison would measure the virus scanner."""
    for _ in range(2):
        r = tc.run_measured(exe, args)
        if r.code != 0:
            return {"ok": False, "note": f"exit {r.code}: {r.err.splitlines()[0] if r.err else ''}"}
    best, peak, out = None, 0, None
    for _ in range(reps):
        r = tc.run_measured(exe, args)
        if r.code != 0:
            return {"ok": False, "note": f"exit {r.code}: {r.err.splitlines()[0] if r.err else ''}"}
        if best is None or r.ms < best:
            best = r.ms
        peak = max(peak, r.peak)
        out = r.out
    return {"ok": True, "ms": best, "peak": peak, "out": out}


def measure_baseline(harness, jit):
    """What an empty program of each kind costs, so the reader can discount
    process startup from the small sizes. The JIT floor is the same empty
    program compiled and run in one go, which is compilation as much as
    startup: it is what every JIT row below pays before doing any work."""
    tcname = harness.active[0]
    cc = harness.ccs[tcname]
    g = GENDIR / "baseline_goose.goose"
    tc.write_text(g, "fn main() { print(0); }\n")
    tc.run_capture([harness.args.exe, "-O2", "--standalone", "-o", GENDIR / "baseline_goose.c", g])
    cc.compile(GENDIR / "baseline_goose.c", GENDIR / ("baseline_goose" + tc.EXE_SUFFIX),
               opt=2, defines=[STACK_RESERVE])
    c = GENDIR / "baseline_cpp.cpp"
    tc.write_text(c, '#include <cstdio>\nint main(){printf("0\\n");}\n')
    cc.compile(c, GENDIR / ("baseline_cpp" + tc.EXE_SUFFIX), opt=2, cpp=True)
    out = {k: measure_exe(GENDIR / ("baseline_" + k + tc.EXE_SUFFIX), 5)
           for k in ("goose", "cpp")}
    if jit:
        out["jit"] = measure_exe(harness.args.exe, 5,
                                 ["-O2", "--jit", f"-D{STACK_RESERVE}", g])
    return out


# --- report helpers ----------------------------------------------------------

def geomean(xs):
    xs = [x for x in xs if x and x > 0]
    if not xs:
        return None
    return math.exp(sum(math.log(x) for x in xs) / len(xs))


def fmt_ms(m):
    return tc.num(m["ms"]) if m and m.get("ok") else "--"


def fmt_mb(m):
    return tc.num(m["peak"] / (1 << 20)) if m and m.get("ok") else "--"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", default="", help="comma-separated benchmarks to re-measure")
    ap.add_argument("--sizes", default="small,medium,large")
    ap.add_argument("--toolchains", default="", help="comma-separated; default is all found")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--exe", help="the goose compiler to use")
    ap.add_argument("--out", default=str(HERE / "results.md"))
    ap.add_argument("--skip-build", action="store_true", help="re-run what is already built")
    ap.add_argument("--report-only", action="store_true",
                    help="regenerate the report from saved measurements")
    ap.add_argument("--no-jit", action="store_true",
                    help="skip the in-process TinyCC measurements")
    args = ap.parse_args()

    tc.setup_console()
    only = [s for s in args.only.split(",") if s]
    sizes = [s for s in args.sizes.split(",") if s]

    # --report-only touches neither compiler, so it works on a machine that has
    # the saved measurements and nothing else installed.
    ccs = tc.find_ccs()
    wanted = [s for s in args.toolchains.split(",") if s] or list(ccs)
    active = [k for k in ccs if k in wanted]
    if not args.report_only:
        args.exe = tc.find_goose(args.exe)
        if not ccs:
            sys.exit("no C toolchain found")
        if not active:
            sys.exit(f"no usable toolchain selected out of {', '.join(ccs)}")
    descs = {k: {"desc": ccs[k].desc, "opt": "/O2" if ccs[k].style == "msvc" else "-O2"}
             for k in ccs}

    jit = not (args.no_jit or args.report_only)
    rustc = tc.find_rustc()
    have_rust = bool(rustc) and RUSTDIR.is_dir()
    rust_version = tc.decode(tc.run_capture([rustc, "--version"])[1]).strip() if have_rust else None

    GENDIR.mkdir(parents=True, exist_ok=True)
    shutil.copy(GOOSEDIR / "rng.goose", GENDIR)     # imports resolve next to the root file
    if have_rust:
        shutil.copy(RUSTDIR / "bench.rs", GENDIR)   # `mod bench;` resolves next to the root

    harness = Harness(args, ccs, active, rustc)
    selected = [b for b in BENCHMARKS if not only or b["name"] in only]
    if not selected:
        sys.exit(f"no benchmarks matched --only {args.only}")

    print("toolchains: " + "  |  ".join(descs[k]["desc"] for k in active)
          + ("  |  goose --jit (TinyCC)" if jit else ""))
    print(f"sizes: {', '.join(sizes)}   reps: {args.reps}\n")

    baseline = None if (args.skip_build or args.report_only) else measure_baseline(harness, jit)

    results = {}      # results[bench][impl][toolchain][size] = measurement
    mismatch = {}
    measured = set()  # "bench/size" keys refreshed by this run
    statefile = GENDIR / "results.json"

    # --report-only regenerates results.md from saved measurements, so the
    # hand-written commentary in notes.md can be edited without a rebuild.
    if args.report_only:
        if not statefile.exists():
            sys.exit(f"no saved measurements at {statefile}; run without --report-only first")
        state = json.loads(statefile.read_text(encoding="utf-8"))
        results = state["results"]
        baseline = state["baseline"]
        mismatch = state["mismatch"]
        args.reps = state["reps"]
        sizes = state["sizes"]
        active = state["toolchains"]
        descs = state["tcdefs"]
        selected = [b for b in BENCHMARKS
                    if b["name"] in results and (not only or b["name"] in only)]

    for b in ([] if args.report_only else selected):
        results.setdefault(b["name"], {})
        for si, sname in enumerate(SIZE_NAMES):
            if sname not in sizes:
                continue
            n = b["sizes"][si]
            checks = {}
            impls = [dict(kind="goose", **g) for g in b["goose"]]
            impls += [dict(kind="cpp", **c) for c in b["cpp"]]
            if have_rust and "rust" in b:
                impls += [dict(kind="rust", **u) for u in b["rust"]]
            for im in impls:
                slug = re.sub(r"[^A-Za-z0-9]+", "_", im["label"]).strip("_")
                results[b["name"]].setdefault(im["label"], {})
                # Rust has one backend, so its rows are built and timed once
                # rather than once per C/C++ toolchain.
                tcs = [RUST_TC] if im["kind"] == "rust" else active
                for tcname in tcs:
                    tag = f"{b['name']}_{sname}_{slug}_{tcname}"
                    results[b["name"]][im["label"]].setdefault(tcname, {})
                    exe = GENDIR / (tag + tc.EXE_SUFFIX)
                    if args.skip_build:
                        exe, note = (exe, None) if exe.exists() else (None, "not built")
                    elif im["kind"] == "goose":
                        exe, note = harness.build_goose(im["file"], n, tag, tcname)
                    elif im["kind"] == "rust":
                        exe, note = harness.build_rust(im["file"], n, tag)
                    else:
                        exe, note = harness.build_cpp(im["file"], im["variant"], n, tag, tcname)
                    if not exe:
                        print(f"  {im['label']:<32} {sname:<7} {tcname:<5} BUILD FAIL: {note}")
                        results[b["name"]][im["label"]][tcname][sname] = {
                            "ok": False, "note": "build failed"}
                        continue
                    m = measure_exe(exe, args.reps)
                    results[b["name"]][im["label"]][tcname][sname] = m
                    if m["ok"]:
                        checks[f"{im['label']} [{tcname}]"] = m["out"]
                        print(f"  {im['label']:<32} {sname:<7} {tcname:<5} "
                              f"{tc.num(m['ms']):>9} ms  {tc.num(m['peak'] / (1 << 20)):>8} MB")
                    else:
                        print(f"  {im['label']:<32} {sname:<7} {tcname:<5} "
                              f"RUN FAIL: {m['note']}")
                # The same source through the in-process backend: one run that
                # compiles and executes, so the time carries the compilation
                # and the peak carries the compiler. The checksums still have
                # to agree with every built row.
                if jit and im["kind"] == "goose":
                    base = f"{b['name']}_{sname}_{slug}"
                    gsrc = harness.goose_source(im["file"], n, base)
                    results[b["name"]][im["label"]].setdefault(JIT_TC, {})
                    m = measure_exe(args.exe, args.reps,
                                    ["-O2", "--jit", f"-D{STACK_RESERVE}", gsrc])
                    results[b["name"]][im["label"]][JIT_TC][sname] = m
                    if m["ok"]:
                        checks[f"{im['label']} [{JIT_TC}]"] = m["out"]
                        print(f"  {im['label']:<32} {sname:<7} {'jit':<5} "
                              f"{tc.num(m['ms']):>9} ms  {tc.num(m['peak'] / (1 << 20)):>8} MB")
                    else:
                        print(f"  {im['label']:<32} {sname:<7} {'jit':<5} "
                              f"RUN FAIL: {m['note']}")
            # Every implementation, under every toolchain, must agree.
            measured.add(f"{b['name']}/{sname}")
            if len(set(checks.values())) > 1:
                mismatch[f"{b['name']}/{sname}"] = checks
                print(f"  !! checksum mismatch in {b['name']}/{sname}")
        print()

    # --- report --------------------------------------------------------------

    # A partial run (--only, --sizes, --toolchains) refreshes part of the
    # picture; it merges into what previous runs measured rather than replacing
    # it, so results.md always shows everything measured so far.
    if not args.report_only:
        if statefile.exists():
            prev = json.loads(statefile.read_text(encoding="utf-8"))
            merged = prev["results"]
            for bn, impls in results.items():
                for lbl, tcs in impls.items():
                    for tcname, szs in tcs.items():
                        merged.setdefault(bn, {}).setdefault(lbl, {}).setdefault(tcname, {}).update(szs)
            results = merged
            # A mismatch recorded earlier is cleared by a run that re-measures
            # that benchmark and size and finds them agreeing.
            for k, v in prev["mismatch"].items():
                if k not in mismatch and k not in measured:
                    mismatch[k] = v
            if not baseline:
                baseline = prev["baseline"]
            sizes = [s for s in SIZE_NAMES if s in prev["sizes"] or s in sizes]
            active = [k for k in ccs if k in prev["toolchains"] or k in active]
            descs = {**prev["tcdefs"], **descs}
        tc.write_text(statefile, json.dumps(
            {"results": results, "baseline": baseline, "mismatch": mismatch,
             "reps": args.reps, "sizes": sizes, "toolchains": active,
             "tcdefs": {k: descs[k] for k in active}}, indent=1))
        selected = [b for b in BENCHMARKS if b["name"] in results]

    def get_m(bench, label, tcname, size):
        return results.get(bench, {}).get(label, {}).get(tcname, {}).get(size)

    # Saved measurements outlive the run that made them, so the report asks
    # what is in them rather than what this run had available.
    jit_measured = any(JIT_TC in impl for b in results.values() for impl in b.values())

    active_sizes = [s for s in SIZE_NAMES if s in sizes]
    md = []

    def W(s=""):
        md.append(s)

    W("# Goose benchmark results")
    W()
    W(f"Generated by `bench/run_bench.py` on {datetime.now():%Y-%m-%d %H:%M}. "
      "Do not edit by hand.")
    W("The benchmarks themselves are `bench/goose/*.goose`, `bench/cpp/*.cpp` and")
    W("`bench/rust/*.rs`; their purpose is explained in")
    W("`bench/design.md`.")
    W()
    W("## How to read this")
    W()
    W("* Start with **Across all benchmarks**: it shows the whole suite in one table. The")
    W("  per-benchmark sections below it give the details, and each ends with a sentence or")
    W("  two on what that row means.")
    W("* There is one table per benchmark. Rows are implementations. The three time columns")
    W("  are the data sizes (the small one is meant to fit in cache, the large one is meant")
    W("  not to), and the last column is peak memory at the large size.")
    if len(active) > 1:
        W(f"* Every Goose and C++ time cell is `{' / '.join(active)}`: the same source built by")
        W("  each of the toolchains in the table below, so a gap between those numbers comes")
        W("  from the backend alone. Goose is compiled to C once per size and every")
        W("  toolchain builds that same C. Rust rows carry a single time, because rustc is")
        W("  the only Rust backend here; it is LLVM, so it is the clang column they are most")
        W("  directly comparable with.")
    else:
        W(f"* Every time cell is the `{active[0]}` build. Goose is compiled to C once per size")
        W("  and that toolchain builds that C.")
    W(f"* Times are the best of {args.reps} whole-process wall clocks, after two discarded warm-up")
    W("  runs. Memory is the peak working set, the larger of the toolchains (they")
    W("  rarely differ).")
    W("* Everything a program does, including generating its input, building its")
    W("  structure and tearing it down, is inside the measurement. Whole-process timing")
    W("  measures these lifecycle costs together; input generation can dilute differences in")
    W("  the phase under study. Goose now has `os.clock_ns` for separate kernel")
    W("  experiments, but this suite retains its whole-process measurement policy.")
    W("* Every implementation of a benchmark, under every toolchain, prints the same")
    W("  checksum lines and the harness verifies they agree.")
    W()
    W("## Environment")
    W()
    W("| | |")
    W("|---|---|")
    W(f"| CPU | {tc.cpu_name()} |")
    W(f"| RAM | {tc.num(tc.ram_bytes() / (1 << 30), 0)} GB |")
    W(f"| OS | {tc.os_name()} |")
    for tcname in active:
        W(f"| {tcname} | {descs[tcname]['desc']}, `{descs[tcname]['opt']}` "
          "(C++20 for the C++ rows) |")
    W("| Goose | `goose -O2` to C, then each toolchain above on that C |")
    if jit_measured:
        W("| Goose JIT | `goose -O2 --jit`: TinyCC in the compiler's own process, no C file |")
    W("| Rust | " + (f"{rust_version}, `-O -C codegen-units=1`" if have_rust
                     else "not installed (rows pending)") + " |")
    W()
    if baseline:
        W(f"Startup floor (an empty program, best of 5): "
          f"Goose {tc.num(baseline['goose']['ms'])} ms / {tc.num(baseline['goose']['peak'] / (1 << 20))} MB, "
          f"C++ {tc.num(baseline['cpp']['ms'])} ms / {tc.num(baseline['cpp']['peak'] / (1 << 20))} MB.")
        W("Account for this startup cost when interpreting ratios at small sizes.")
        W()
    if mismatch:
        W(f"> **Checksum mismatches:** {', '.join(mismatch)}. Those rows are not comparable.")
        W()

    # --- aggregate -----------------------------------------------------------
    # How Goose compares to each C++ tier over the whole suite, per toolchain.
    # Per benchmark the ratio is (fastest C++ row of that tier) / (fastest Goose
    # row), both under the same toolchain; the suite figure is the geometric
    # mean, which is the right average for ratios.

    def best_time(bench, labels, tcname, size):
        times = [m["ms"] for l in labels
                 for m in [get_m(bench, l, tcname, size)] if m and m.get("ok")]
        return min(times) if times else None

    # Over whichever toolchains actually measured this label, so the Rust rows
    # -- filed under `rustc` alone -- are picked up the same way as the others.
    def best_mem(bench, labels, size):
        peaks = [m["peak"] for l in labels if l in results[bench]
                 for tcname in results[bench][l] if tcname != JIT_TC
                 for m in [get_m(bench, l, tcname, size)] if m and m.get("ok")]
        return min(peaks) if peaks else None

    if "large" in active_sizes:
        ratios = {}        # ratios[tier][tc] = list of per-benchmark ratios
        mem_ratios = {}
        per_bench = []
        for b in selected:
            if not results[b["name"]]:
                continue
            gl = [g["label"] for g in b["goose"] if g["label"] in results[b["name"]]]
            if not gl:
                continue
            row = {"bench": b["name"]}
            for tier in ("idiomatic", "expert"):
                cl = [c["label"] for c in b["cpp"]
                      if c["tier"] == tier and c["label"] in results[b["name"]]]
                if not cl:
                    continue
                for tcname in active:
                    g = best_time(b["name"], gl, tcname, "large")
                    c = best_time(b["name"], cl, tcname, "large")
                    if g and c:
                        ratios.setdefault(tier, {}).setdefault(tcname, []).append(c / g)
                        row[f"{tier}-{tcname}"] = c / g
                gm = best_mem(b["name"], gl, "large")
                cm = best_mem(b["name"], cl, "large")
                if gm and cm:
                    mem_ratios.setdefault(tier, []).append(cm / gm)
                    row[f"{tier}-mem"] = cm / gm
            # Rust is one column pair rather than two tiers: the comparison
            # point is the fastest (and separately the smallest) safe Rust row,
            # whichever shape that turned out to be, because "the way Rust is
            # meant to be used" is a single target and picking the loser of two
            # Rust rows would be flattering Goose for nothing.
            ul = [u["label"] for u in b.get("rust", []) if u["label"] in results[b["name"]]]
            if ul:
                u = best_time(b["name"], ul, RUST_TC, "large")
                for tcname in active:
                    g = best_time(b["name"], gl, tcname, "large")
                    if g and u:
                        ratios.setdefault("rust", {}).setdefault(tcname, []).append(u / g)
                        row[f"rust-{tcname}"] = u / g
                gm = best_mem(b["name"], gl, "large")
                um = best_mem(b["name"], ul, "large")
                if gm and um:
                    mem_ratios.setdefault("rust", []).append(um / gm)
                    row["rust-mem"] = um / gm
            per_bench.append(row)

        tiers = ["idiomatic", "expert"] + (["rust"] if "rust" in ratios else [])
        W("## Across all benchmarks")
        W()
        W("How many times faster (or smaller) Goose is than the best row of each comparison")
        W("at the `large` size. *Idiomatic* is what a C++ programmer writes first")
        W("(`unique_ptr`, `vector<string>`, `unordered_map`, `std::variant`);")
        W("*expert* is the hand-built version (exactly-reserved arena with indices,")
        W("`string_view`, flat offsets, open addressing, CSR); *rust* is the best safe")
        W("Rust row for that benchmark, whichever implementation that is. Above 1.00 means")
        W(f"Goose is ahead. The suite figure is the geometric mean over the {len(per_bench)} benchmarks.")
        W()
        if len(active) > 1:
            W("There is only one rustc build, so the Rust number is the same in every toolchain")
            W("column; those columns differ only in which Goose build they compare against.")
            W()
        hdr, sep = "| benchmark |", "|---|"
        for tier in tiers:
            for tcname in active:
                hdr += f" vs {tier} ({tcname}) |"
                sep += "---:|"
        for tier in tiers:
            hdr += f" memory vs {tier} |"
            sep += "---:|"
        W(hdr)
        W(sep)
        for r in per_bench:
            line = f"| {r['bench']} |"
            for tier in tiers:
                for tcname in active:
                    v = r.get(f"{tier}-{tcname}")
                    line += f" {tc.num(v, 2)}x |" if v else " -- |"
            for tier in tiers:
                v = r.get(f"{tier}-mem")
                line += f" {tc.num(v, 2)}x |" if v else " -- |"
            W(line)
        line = "| **geometric mean** |"
        for tier in tiers:
            for tcname in active:
                v = geomean(ratios.get(tier, {}).get(tcname, []))
                line += f" **{tc.num(v, 2)}x** |" if v else " -- |"
        for tier in tiers:
            v = geomean(mem_ratios.get(tier, []))
            line += f" **{tc.num(v, 2)}x** |" if v else " -- |"
        W(line)
        W()
        if (HERE / "summary.md").exists():
            W(tc.decode((HERE / "summary.md").read_bytes()).rstrip())
            W()

    # --- per benchmark -------------------------------------------------------

    for b in selected:
        if not results[b["name"]]:
            continue
        pname = b.get("param", "N")
        rust_of = {u["label"] for u in b.get("rust", [])}
        labels = ([g["label"] for g in b["goose"]] + [c["label"] for c in b["cpp"]] +
                  [u["label"] for u in b.get("rust", [])])
        labels = [l for l in labels if l in results[b["name"]]]
        W(f"## {b['name']}")
        W()
        W(b["what"])
        W()
        W(f"Time in ms as `{' / '.join(active)}` for the Goose and C++ rows, and a single")
        W("rustc time for the Rust ones; memory is the peak working set at the largest size.")
        W()
        hdr, sep = "| implementation |", "|---|"
        for s in active_sizes:
            hdr += f" {s} ({pname}={b['sizes'][SIZE_NAMES.index(s)]}) |"
            sep += "---:|"
        hdr += " large MB |"
        sep += "---:|"
        W(hdr)
        W(sep)
        for label in labels:
            row = f"| {label} |"
            tcs = [RUST_TC] if label in rust_of else active
            for s in active_sizes:
                row += " " + " / ".join(fmt_ms(get_m(b["name"], label, t, s)) for t in tcs) + " |"
            peak = None
            for tcname in tcs:
                m = get_m(b["name"], label, tcname, "large")
                if m and m.get("ok") and (peak is None or m["peak"] > peak["peak"]):
                    peak = m
            row += f" {fmt_mb(peak)} |"
            W(row)
        W()
        if "summary" in b:
            W(b["summary"])
            W()

    # --- toolchain comparison ------------------------------------------------
    # Where the same C or C++ runs meaningfully faster under one backend,
    # listed so the codegen work can be aimed at the biggest gaps.

    if len(active) > 1:
        first, second = active[0], active[1]
        W(f"## Toolchain: {first} vs {second}")
        W()
        W(f"Ratio of {first} time to {second} time at the `large` size "
          f"(>1 means {second} is faster).")
        W("Only rows differing by more than 5% are listed, biggest gap first.")
        W()
        rows = []
        for b in selected:
            # Only rows the manifest still lists: the saved measurements outlive
            # a renamed or removed implementation, and a stale label here would
            # be reported as if it were part of the suite.
            current = ([g["label"] for g in b["goose"]] + [c["label"] for c in b["cpp"]] +
                       [u["label"] for u in b.get("rust", [])])
            for label in current:
                a = get_m(b["name"], label, first, "large")
                c = get_m(b["name"], label, second, "large")
                if a and c and a.get("ok") and c.get("ok"):
                    rows.append({"bench": b["name"], "label": label,
                                 "ratio": a["ms"] / c["ms"], "v": a["ms"], "c": c["ms"]})
        interesting = sorted((r for r in rows if abs(r["ratio"] - 1) > 0.05),
                             key=lambda r: -abs(r["ratio"] - 1))
        if interesting:
            W(f"| benchmark | implementation | {first} ms | {second} ms | {first}/{second} |")
            W("|---|---|---:|---:|---:|")
            for r in interesting:
                W(f"| {r['bench']} | {r['label']} | {tc.num(r['v'])} | {tc.num(r['c'])} | "
                  f"{tc.num(r['ratio'], 2)} |")
        else:
            W("No row differs by more than 5%.")
        W()

    # --- Goose AOT vs Goose JIT ----------------------------------------------
    # Deliberately its own table. A JIT cell is one process that compiles the
    # program and then runs it, against a cell that is a run of something built
    # earlier by an optimizing compiler, so the two are only comparable with
    # each other and never with the C++ or Rust rows above.

    aot_tc = active[0] if active else None
    jit_rows = [] if not jit_measured else [
                (b, g["label"]) for b in selected for g in b["goose"]
                if results.get(b["name"], {}).get(g["label"], {}).get(JIT_TC)]
    if jit_rows and aot_tc:
        W("## Goose: compiled vs JIT")
        W()
        W(f"The same Goose source is run two ways: built by `{aot_tc}` from the generated C")
        W("and then run, and compiled and run by the compiler in one process through")
        W("TinyCC. Each cell is `compiled / JIT` in ms.")
        W()
        W("The JIT number includes the compiler's own front end and TinyCC compiling the")
        W("program, as well as running it, and TinyCC is a one-pass compiler with no")
        W("optimizer. So this does not measure Goose against another language; it measures")
        W("the cost of not needing a C toolchain. The memory columns are the peak working")
        W("set; the JIT process holds the compiler, the generated C and the program at the")
        W("same time.")
        W()
        hdr, sep = "| benchmark | implementation |", "|---|---|"
        for sz in active_sizes:
            hdr += f" {sz} |"
            sep += "---:|"
        hdr += " JIT/compiled (large) | large MB compiled / JIT |"
        sep += "---:|---:|"
        W(hdr)
        W(sep)
        ratios = []
        for b, label in jit_rows:
            row = f"| {b['name']} | {label} |"
            for sz in active_sizes:
                a, j = get_m(b["name"], label, aot_tc, sz), get_m(b["name"], label, JIT_TC, sz)
                row += f" {fmt_ms(a)} / {fmt_ms(j)} |"
            a = get_m(b["name"], label, aot_tc, "large")
            j = get_m(b["name"], label, JIT_TC, "large")
            if a and j and a.get("ok") and j.get("ok"):
                ratios.append(j["ms"] / a["ms"])
                row += f" {tc.num(j['ms'] / a['ms'], 2)}x |"
            else:
                row += " -- |"
            row += f" {fmt_mb(a)} / {fmt_mb(j)} |"
            W(row)
        gm = geomean(ratios)
        W("| **geometric mean** | |" + " |" * len(active_sizes) +
          (f" **{tc.num(gm, 2)}x** |" if gm else " -- |") + " |")
        W()
        if baseline and baseline.get("jit"):
            W(f"Floor for a JIT row (an empty program compiled and run, best of 5): "
              f"{tc.num(baseline['jit']['ms'])} ms / "
              f"{tc.num(baseline['jit']['peak'] / (1 << 20))} MB. That time is almost all")
            W("compilation, and it is included in every JIT cell above.")
            W()

    # The numbers above are generated; what they mean is written by hand in
    # notes.md and carried through verbatim.
    if (HERE / "notes.md").exists():
        W(tc.decode((HERE / "notes.md").read_bytes()).rstrip())
        W()

    if not have_rust:
        W("## Pending")
        W()
        W("* The Rust rows are missing from this run: `rustc` was not found. The harness")
        W("  picks up `bench/rust/` automatically once it is installed.")
        W()

    tc.write_text(args.out, "\n".join(md) + "\n")
    print(f"wrote {args.out}")
    if mismatch:
        print(f"{len(mismatch)} checksum mismatch(es)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
