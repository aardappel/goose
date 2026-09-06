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

Timing is whole-process wall clock. Goose has no clock builtin, so there is no
in-process timer available to all languages; the startup floor is measured
separately and printed alongside, so the reader can see how much of a short
run is process creation.

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

# Address space reserved per Goose data stack. The runtime default of 256 MB
# is below what the large sizes need (up to 536 MB on one stack); 2 GB leaves
# them 4x headroom and is the largest reservation under which a u32 relative
# reference stores without a range check (spec 3.9).
STACK_RESERVE = "GS_STACK_RESERVE=2147483648ull"

SIZE_NAMES = ["small", "medium", "large"]

# Rust is not built by the C/C++ toolchains, so its measurements are filed under
# a toolchain of their own and its rows carry one time, not one per backend.
RUST_TC = "rustc"

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
            "The control is a narrow Goose win under v145 and a tie under clang: "
            "293 ms against Rust 309 and the exactly-reserved vector 335, then "
            "314 against the same Rust 309 once clang builds it, on memory "
            "identical to within 0.1%. On flat scalar data the push loop is all "
            "there is to win. The only row that really loses is the unreserved "
            "vector at 1.6x, and that gap does not exist against Rust, because "
            "collect() over a TrustedLen iterator sizes the allocation exactly."),
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
            "Goose 154-160 ms against the exactly-reserved C++ vector 197 and "
            "Rust 176, on 504 MB for all three: a 10-15% Goose lead on the loop "
            "this benchmark is made of. It is the most interesting row in the "
            "suite for a reason unrelated to time. Goose keeps real Item& "
            "pointers that stay valid across every later push. Safe Rust cannot: "
            "a &Item borrows the vector, so the next push does not compile, and "
            "no std container is both pointer-stable and O(1)-append. Its marks "
            "are usize indices because that is the only shape the language "
            "admits, not because it is faster. C++ has the same problem, and its "
            "one pointer-stable answer, deque, is 10x slower and 2.5x larger."),
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
            "Against the two rows that own their bytes the way Goose does, Goose "
            "is 2.8-3.1x faster than vector<string> and 2.8x faster than Rust "
            "Vec<String>, on 3.1x and 2.2x less memory. Against the borrowing "
            "rows it is 14-16% ahead of Rust Vec<&str> and 13-26% ahead of "
            "hand-rolled C++ offsets -- but those are views into a text buffer "
            "they cannot outlive, which the Goose list is not. Borrowing is the "
            "Rust default rather than an optimisation someone has to be talked "
            "into, so Vec<&str> is what a Rust programmer writes first; ownership "
            "is the question worth asking of it. The split is the standard "
            "library's each_split, a shade faster here than the hand-written scan "
            "it replaces."),
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
            "The largest margin over Rust in the suite: 2.0-2.3x faster on 4.0x "
            "less memory, and 1.4x faster than the best C++ row on 3.7x less. "
            "Rust has the best of the fixed-tag shapes here -- niche optimisation "
            "hides the tag inside the String pointer, so its enum beats "
            "std::variant with a string on both time and memory -- but it is "
            "still a fixed enum, so every element pays for the largest variant "
            "and the Say text is a second allocation on top. The Goose fixed-enum "
            "row is the control for exactly that: same work, same language, 2.2x "
            "the memory."),
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
            "Goose is 4.4x faster than owning-pointer nodes in both languages "
            "(new/delete 2,855 ms, Rust Box 2,873) on 2.7x less memory, and 3-12% "
            "faster than the two arena rows while matching their memory to within "
            "0.1%. That the three arena rows land together is the finding: to get "
            "there both Rust and C++ give up pointers for u32 indices into a Vec, "
            "because neither will let a node hold a reference into the container "
            "that owns it. Goose links the same layout with typed, nullable, "
            "4-byte relative references, and never frees. Compare bintrees, the "
            "same node built and discarded many times, where the picture is "
            "different."),
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
            "Goose is 1.3x faster than the best Rust row on 2.0x less memory, and "
            "1.3-1.4x faster than a C++ tagged union on 2.3x less. Rust "
            "dispatches well -- a byte tag, a jump table, no vtable pointer -- "
            "and its arena row is the most compact of the non-Goose rows, so the "
            "remaining gap is representation: a variable-mode leaf costs 5 bytes "
            "and a binary node 9, where every fixed enum pays max-payload for "
            "both. The Rust Box row is the slowest in the benchmark at 449 ms, "
            "behind even C++ virtual dispatch, because an allocation per node "
            "plus a recursive drop costs more than a vtable does."),
    ),
    dict(
        name="graph",
        what=(
            "Build adjacency for V vertices and 8V edges in one pass, then BFS "
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
            "A wash where it counts: Goose CSR, C++ CSR and Rust CSR are level "
            "(794-943 ms on the same memory), which is the honest result, because "
            "every language wants CSR here. The one-pass linked build is 5x "
            "slower than CSR in all three, so that is a data-structure effect and "
            "not a language one. What Goose buys is that its one-pass version is "
            "written with real Edge& references into a growing pool; the Rust "
            "equivalent must chain u32 indices, and there is no safe Rust "
            "spelling of the pointer version at any level of effort. Rust "
            "Vec<Vec> is 1.6x faster than the C++ vector<vector> it mirrors. This "
            "is the noisiest benchmark in the suite: between full harness runs "
            "the linked rows move by up to 23%, in every language at once, so "
            "only the large gaps here mean anything."),
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
            "Level with the best safe Rust and 1.2x smaller: the standard "
            "library's dictionary is the fastest Goose row under v145 (2,180 ms) "
            "and the hand-rolled open-addressed table under clang (2,148), "
            "against Rust's hand-rolled table at 2,190 and its HashMap<&str> at "
            "2,972. The two Goose rows differ in table policy as much as in code: "
            "the hand-rolled one takes a power-of-two table four times the "
            "vocabulary and ends up 13% full, where the dictionary grows on a "
            "two-thirds threshold and ends up half full on 1.1M distinct keys. "
            "That is where its 1.2x memory advantage over the hand-rolled row "
            "comes from, and it costs nothing in time. Both other languages get "
            "borrowed keys from their idiomatic map, so the HashMap gap is the "
            "hash function: std SipHash-1-3 is keyed and DoS-resistant by policy, "
            "which costs about 36% here, and which the Rust community answers "
            "with a third-party hasher crate that this std-only suite does not "
            "use."),
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
            "The flat float kernel, where Goose is never ahead: Rust 864 ms "
            "against Goose 907 under v145 and 884 under clang, with C++ spanning "
            "852-1,055, on identical memory. Elementwise notation costs a few "
            "percent in both languages that can express it: Goose 939 against "
            "907, Rust 886 against 864. The struct-of-arrays C++ row is slower "
            "than array-of-structs under both toolchains. The vector type is the "
            "standard library's float3, which is the three floats a local struct "
            "would have been."),
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
            "The parse flagship, and the suite's widest memory margin: Rust's "
            "arena 1,606 ms against Goose 1,394-1,497 on 2.4x the memory, with "
            "the idiomatic owning shapes 3.1x slower in Rust and 3.9x in C++ on "
            "up to 5.0x the memory. Both fast rows are arenas whose symbols "
            "borrow the source text; Goose stores symbol bytes inline behind a "
            "varint length in nodes exactly as big as their variant needs, links "
            "them with 4-byte offsets -- including the null that ends every "
            "sibling chain, since a null relative reference needs no root of its "
            "own -- and never frees. The Rust borrow works here only because the "
            "text is complete before parsing starts: a parser that interned or "
            "rewrote text while building nodes would be back to owned Strings or "
            "offsets."),
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
            "Ahead of both arenas under clang and the clearest loss in the suite "
            "under v145: Goose 3,751/2,205 ms against the C++ arena 2,568/2,382 "
            "and Rust's open-addressed arena 2,563, on the smallest memory of the "
            "three (133 MB against 181 and 137). The textbook std::list + "
            "unordered_map is 2.6-4.4x slower than Goose and Rust's HashMap row "
            "1.4-2.4x, so the shape wins. The links are pool-relative (spec 3.9's "
            "`in pool` form): 4-byte offsets from the pool's base in the nodes "
            "and in the 8-byte map slots, so the map holds references and the "
            "lookup path has no index arithmetic. What separates the two backends "
            "is scheduling -- the store subtracts a register-resident base "
            "instead of the field's own address, and clang schedules the base add "
            "where v145 does not. lru_indices.goose is the same program with i32 "
            "index slots and self-relative list links, for the encoding "
            "comparison."),
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
            "Under clang Goose is 5% behind the Rust arena (395 against 377 ms) "
            "on 1.35x less memory; under v145 it is 30% behind, and so is the C++ "
            "arena (509), so that is the backend's handling of this "
            "float-and-pointer mix and not the layout -- padding the 117-byte "
            "node to an aligned 120 changes nothing. Against the idiomatic rows, "
            "a vector<unique_ptr> or Vec<Box> of children per node, Goose is "
            "5.4-6.7x faster on 1.3x less memory. The Rust arena has to compose "
            "each transform in a stack local and copy it in, because two borrows "
            "of one Vec cannot be split; Goose composes straight into the pool "
            "through references, and walks a node's children by iterating the "
            "inline array of relative references itself."),
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
            "Goose beats the idiomatic rows by a wide margin -- 4.3x against "
            "unique_ptr nodes with exceptions, 2.2x against Box with Result -- "
            "and trails the arena rows: the C++ arena with error codes by 2% "
            "under v145 and 11% under clang, the Rust arena with Result by "
            "14-18%. The per-input local pool is not the cost: a variant that "
            "builds into a global pool never reset times the same. What remains "
            "is the discriminant threaded through every return (spec 7.9), the "
            "global cursor, and the varint decode per evaluated number, on inputs "
            "of about 40 bytes, in a run that never leaves L1."),
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
            "The allocator benchmark: Goose is 13-15x faster than new/delete and "
            "Box on 2x less memory, 1.4-1.7x faster than the C++ vector arena, "
            "level with the Rust Vec arena under v145 (232 against 228 ms) and "
            "24% behind it under clang (282). It is not the by-reference push (a "
            "global-pool variant is slower, because it never reuses memory) and "
            "not the range-checked 4-byte links (8-byte links time the same); the "
            "C++ arena is behind both, so the clang gap is between what LLVM "
            "makes of rustc's recursion and what it makes of the same shape in C."),
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
            "The DTO rows are where the design shows: Goose builds and renders "
            "the response object 1.7-1.9x faster than the idiomatic C++ DTO and "
            "2.3x faster than the Rust one, on the same 8 MB, because the string, "
            "the item list and the skus are one inline value and the render reads "
            "it back with no allocation anywhere. The streaming rows are the same "
            "code in all three languages -- Goose 433/395 ms, C++ 491/399, Rust "
            "537 -- and the two Goose rows are 20-24% apart, which is what "
            "materialising the object costs even when it is free to allocate."),
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
            "Included to lose, and it does, under one backend. The flat form is "
            "1.9x slower than flat C++ and 7.0x slower than flat Rust under v145, "
            "where the nine bounds checks per pixel stop the loop vectorising; "
            "under clang the same source is level with flat Rust (271 against 291 "
            "ms), because the loop-view hoist reaches the arrays behind the fat "
            "references and the checks then cost clang nothing. Written over row "
            "slices with one assert per row, every check is proven away and both "
            "backends agree: Goose 307/266 ms against Rust 291-292 and C++ with "
            "__restrict 561/307. Rust's flat row is as fast as its slice row "
            "because LLVM vectorises around bounds checks it cannot remove; MSVC "
            "does not, which is what the single length assert in "
            "blur_assert.goose was for: the compiler has since carried the length "
            "from main into the kernel by itself (notes.md, bounds-check "
            "section), so the flat row proves its checks unaided and the next run "
            "should show it at the asserted row's speed."),
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
        self.generated = set()

    def build_goose(self, file, n, tag, tcname):
        base = re.sub(re.escape("_" + tcname) + "$", "", tag)
        cfile = GENDIR / f"{base}.c"
        # Generated once per (benchmark, size) and reused across toolchains,
        # but only within one run: a stale .c left over from a previous run
        # would silently benchmark the previous version of the source.
        if base not in self.generated:
            gsrc = GENDIR / f"{base}.goose"
            write_sized(GOOSEDIR / file, gsrc, n, GOOSE_N)
            code, out, err = tc.run_capture([self.args.exe, "-O2", "-o", cfile, gsrc])
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


def measure_exe(exe, reps):
    """Best time and worst peak over `reps` runs.

    Two discarded warm-up runs first: a freshly written executable is paged in
    and scanned by the OS on first execution, which on some machines costs up
    to 10x the run itself and takes about three executions to settle. It hits
    clang-linked binaries far harder than MSVC-linked ones, so without this the
    toolchain comparison would measure the virus scanner."""
    for _ in range(2):
        r = tc.run_measured(exe)
        if r.code != 0:
            return {"ok": False, "note": f"exit {r.code}: {r.err.splitlines()[0] if r.err else ''}"}
    best, peak, out = None, 0, None
    for _ in range(reps):
        r = tc.run_measured(exe)
        if r.code != 0:
            return {"ok": False, "note": f"exit {r.code}: {r.err.splitlines()[0] if r.err else ''}"}
        if best is None or r.ms < best:
            best = r.ms
        peak = max(peak, r.peak)
        out = r.out
    return {"ok": True, "ms": best, "peak": peak, "out": out}


def measure_baseline(harness):
    """What an empty program of each kind costs, so the reader can discount
    process startup from the small sizes."""
    tcname = harness.active[0]
    cc = harness.ccs[tcname]
    g = GENDIR / "baseline_goose.goose"
    tc.write_text(g, "fn main() { print(0); }\n")
    tc.run_capture([harness.args.exe, "-O2", "-o", GENDIR / "baseline_goose.c", g])
    cc.compile(GENDIR / "baseline_goose.c", GENDIR / ("baseline_goose" + tc.EXE_SUFFIX),
               opt=2, defines=[STACK_RESERVE])
    c = GENDIR / "baseline_cpp.cpp"
    tc.write_text(c, '#include <cstdio>\nint main(){printf("0\\n");}\n')
    cc.compile(c, GENDIR / ("baseline_cpp" + tc.EXE_SUFFIX), opt=2, cpp=True)
    return {k: measure_exe(GENDIR / ("baseline_" + k + tc.EXE_SUFFIX), 5)
            for k in ("goose", "cpp")}


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

    print("toolchains: " + "  |  ".join(descs[k]["desc"] for k in active))
    print(f"sizes: {', '.join(sizes)}   reps: {args.reps}\n")

    baseline = None if (args.skip_build or args.report_only) else measure_baseline(harness)

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

    active_sizes = [s for s in SIZE_NAMES if s in sizes]
    md = []

    def W(s=""):
        md.append(s)

    W("# Goose benchmark results")
    W()
    W(f"Generated by `bench/run_bench.py` on {datetime.now():%Y-%m-%d %H:%M}. "
      "Do not edit by hand.")
    W("The benchmarks themselves are `bench/goose/*.goose`, `bench/cpp/*.cpp` and")
    W("`bench/rust/*.rs`; what they are trying to find out is written down in")
    W("`bench/design.md`.")
    W()
    W("## How to read this")
    W()
    W("* Start with **Across all benchmarks**: it is the whole suite in one table. The")
    W("  per-benchmark sections below it are the detail, each ending in a sentence or two")
    W("  of what that row means.")
    W("* One table per benchmark. Rows are implementations, the three time columns are the")
    W("  data sizes -- the small one is meant to fit in cache, the large one is meant not")
    W("  to -- and the last column is peak memory at the large size.")
    if len(active) > 1:
        W(f"* Every Goose and C++ time cell is `{' / '.join(active)}`: the same source built by")
        W("  each of the toolchains in the table below, so a gap between those numbers is the")
        W("  backend and nothing else. Goose is compiled to C once per size and every")
        W("  toolchain builds that same C. Rust rows carry a single time, because rustc is")
        W("  the only Rust backend here; it is LLVM, so it is the clang column they are most")
        W("  directly comparable with.")
    else:
        W(f"* Every time cell is the `{active[0]}` build. Goose is compiled to C once per size")
        W("  and that toolchain builds that C.")
    W(f"* Times are the best of {args.reps} whole-process wall clocks, after two discarded warm-up")
    W("  runs. Memory is the peak working set, the larger of the toolchains (they")
    W("  rarely differ).")
    W("* Goose has no clock builtin, so there is no in-process timer common to all")
    W("  languages: everything a program does -- generating its input, building its")
    W("  structure, tearing it down -- is inside the measurement. That is deliberate for")
    W("  teardown, which is a real cost the other languages pay and Goose does not, and it")
    W("  dilutes the ratios wherever input generation is a large share of the work.")
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
    W("| Rust | " + (f"{rust_version}, `-O -C codegen-units=1`" if have_rust
                     else "not installed -- rows pending") + " |")
    W()
    if baseline:
        W(f"Startup floor (an empty program, best of 5): "
          f"Goose {tc.num(baseline['goose']['ms'])} ms / {tc.num(baseline['goose']['peak'] / (1 << 20))} MB, "
          f"C++ {tc.num(baseline['cpp']['ms'])} ms / {tc.num(baseline['cpp']['peak'] / (1 << 20))} MB.")
        W("Subtract that from the small sizes before believing any ratio there.")
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
                 for tcname in results[bench][l]
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
        W("Rust row for that benchmark, whichever shape won. Above 1.00 means Goose")
        W(f"wins. The suite figure is the geometric mean over the {len(per_bench)} benchmarks.")
        W()
        if len(active) > 1:
            W("The Rust number is the same in every toolchain column -- there is only one")
            W("rustc -- so those columns differ only in which Goose build they are against.")
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
