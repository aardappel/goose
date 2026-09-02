# Goose benchmark harness.
#
# For every benchmark in the manifest below, at every requested size, under
# every requested toolchain, this builds each implementation, runs it, and
# records wall time and peak working set. Every implementation of one
# benchmark must print the same checksum lines; a mismatch is reported and the
# row is marked, because a faster implementation that computes something else
# is not a result.
#
# Both toolchains live in the Visual Studio 2026 install: `cl` (v145) and the
# bundled `clang-cl`. Both languages are built by both, which is what makes
# "does the backend explain this?" answerable.
#
# Timing is whole-process wall clock. Goose has no clock builtin, so there is
# no in-process timer available to all languages; the startup floor is
# measured separately and printed alongside, so the reader can see how much of
# a short run is process creation.
#
#   .\bench\run_bench.ps1                        # everything
#   .\bench\run_bench.ps1 -Only tree,sexp        # re-measure a subset; the report
#                                                # still shows everything measured
#   .\bench\run_bench.ps1 -Toolchains clang      # one toolchain
#   .\bench\run_bench.ps1 -Sizes small -Reps 1   # a quick pass
#   .\bench\run_bench.ps1 -SkipBuild             # re-run what is already built
#   .\bench\run_bench.ps1 -ReportOnly            # regenerate results.md from saved
#                                                # measurements, e.g. after editing notes.md

param(
    [string[]] $Only = @(),
    [string[]] $Sizes = @("small", "medium", "large"),
    [string[]] $Toolchains = @("v145", "clang"),
    [int]      $Reps = 3,
    [string]   $Exe = "$PSScriptRoot\..\build\Debug\goose.exe",
    [string]   $Out = "$PSScriptRoot\results.md",
    [switch]   $SkipBuild,
    [switch]   $ReportOnly
)

$ErrorActionPreference = "Stop"
$gendir   = "$PSScriptRoot\gen"
$goosedir = "$PSScriptRoot\goose"
$cppdir   = "$PSScriptRoot\cpp"
$rustdir  = "$PSScriptRoot\rust"

# Address space reserved per Goose data stack. The default in the runtime is
# 256MB, which the large sizes exceed.
$stackReserve = "8589934592ull"

# --- manifest ----------------------------------------------------------------
# sizes:  the value substituted for the `// BENCH_N` line, in small, medium,
#         large order. Goose and Rust take it by rewriting that line in a copy
#         of the source; C++ takes it as /DBENCH_N.
# param:  what that value is called in the table header; "N" if omitted.
# goose:  implementations built from bench/goose/<file>.
# cpp:    implementations built from bench/cpp/<file> with /DVARIANT=<variant>.
# rust:   implementations built from bench/rust/<file>, one file per row.
#         Rust is built by rustc only, so its rows carry a single time rather
#         than one per C/C++ toolchain.

$benchmarks = @(
    @{ name = "sum"
       summary = 'The control is a narrow Goose win: 296 ms against Rust 312 and the exactly-reserved vector 334, on memory identical to within 0.1%, and under clang Goose and Rust are level at 311. On flat scalar data the push loop is all there is to win, and caching each data stack top in a local (last round) took the last 6% off it. The only row that really loses is the unreserved vector at 1.6x, and that gap does not exist against Rust, because collect() over a TrustedLen iterator sizes the allocation exactly.'
       what = "Control: build a flat i32 array, then scan it eight times."
       sizes = @(1000000, 16000000, 128000000)
       goose = @(@{ label = "goose grow-only array"; file = "sum.goose" })
       cpp = @(@{ label = "cpp vector"; file = "sum.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp vector+reserve"; file = "sum.cpp"; variant = 1; tier = "expert" })
       rust = @(@{ label = "rust Vec, collect"; file = "sum.rs" }) },

    @{ name = "push"
       summary = 'Goose 151-158 ms against the exactly-reserved C++ vector 196 and Rust 176, on 504 MB for all three: a 11-16% Goose lead on the loop this benchmark is made of. It stays the most interesting row in the suite for a reason unrelated to time. Goose keeps real Item& pointers that stay valid across every later push. Safe Rust cannot: a &Item borrows the vector, so the next push does not compile, and no std container is both pointer-stable and O(1)-append. Its marks are usize indices because that is the only shape the language admits, not because it is faster. C++ has the same problem, and its one pointer-stable answer, deque, is 10x slower and 2.5x larger.'
       what = "Push N records while keeping pointers to every 64th one."
       sizes = @(1000000, 16000000, 64000000)
       goose = @(@{ label = "goose array, typed references"; file = "push.goose" })
       cpp = @(@{ label = "cpp vector, indices"; file = "push.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp vector+reserve, indices"; file = "push.cpp"; variant = 1; tier = "expert" },
               @{ label = "cpp deque, pointers"; file = "push.cpp"; variant = 2; tier = "idiomatic" })
       rust = @(@{ label = "rust Vec+reserve, indices"; file = "push.rs" }) },

    @{ name = "strlist"
       summary = 'Against the two rows that own their bytes the way Goose does, Goose is 2.7-2.9x faster than vector<string> and 2.8x faster than Rust Vec<String>, on 3.1x and 2.2x less memory. Against the borrowing rows it is 9-12% ahead of Rust Vec<&str> and 7-21% ahead of hand-rolled C++ offsets -- but those are views into a text buffer they cannot outlive, which the Goose list is not. Borrowing is the Rust default rather than an optimisation someone has to be talked into, so Vec<&str> is what a Rust programmer writes first; ownership is the question worth asking of it.'
       what = "Split a text into N words, keep them as a list, scan it four times."
       sizes = @(200000, 2000000, 8000000)
       goose = @(@{ label = "goose inline strings, owned"; file = "strlist.goose" })
       cpp = @(@{ label = "cpp vector<string>"; file = "strlist.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp vector<string_view>"; file = "strlist.cpp"; variant = 1; tier = "expert" },
               @{ label = "cpp flat offsets"; file = "strlist.cpp"; variant = 2; tier = "expert" })
       rust = @(@{ label = "rust Vec<String>"; file = "strlist_owned.rs" },
                @{ label = "rust Vec<&str>"; file = "strlist_borrowed.rs" }) },

    @{ name = "records"
       summary = 'The largest margin over Rust in the suite: 2.0-2.2x faster on 4.0x less memory, and 1.3-1.4x faster than the best C++ row on 3.7x less. Rust has the best of the fixed-tag shapes here -- niche optimisation hides the tag inside the String pointer, so its enum beats std::variant with a string on both time and memory -- but it is still a fixed enum, so every element pays for the largest variant and the Say text is a second allocation on top. The Goose fixed-enum row is the control for exactly that: same work, same language, 2.2x the memory.'
       what = "Build a log of N variant records, aggregate it four times."
       sizes = @(500000, 4000000, 16000000)
       goose = @(@{ label = "goose variable enum"; file = "records_var.goose" },
                 @{ label = "goose fixed enum"; file = "records_fixed.goose" })
       cpp = @(@{ label = "cpp virtual + unique_ptr"; file = "records.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp variant + string"; file = "records.cpp"; variant = 1; tier = "idiomatic" },
               @{ label = "cpp variant + buffer"; file = "records.cpp"; variant = 2; tier = "expert" })
       rust = @(@{ label = "rust enum + String"; file = "records.rs" }) },

    @{ name = "tree"
       summary = 'Goose is 4.1-4.6x faster than owning-pointer nodes in both languages (unique_ptr 2,833-2,956 ms, Rust Box 2,777) on 2.7x less memory, and 4-16% faster than the two arena rows while matching their memory to within 0.1%. That the three arena rows land together is the finding: to get there both Rust and C++ give up pointers for u32 indices into a Vec, because neither will let a node hold a reference into the container that owns it. Goose links the same layout with typed, nullable, 4-byte relative references, and never frees. Compare bintrees, the same node built and discarded many times, where the picture is different.'
       what = "Build a complete binary tree of the given depth, sum it eight times."
       param = "depth"
       sizes = @(16, 20, 24)
       goose = @(@{ label = "goose pool + typed relative refs"; file = "tree.goose" })
       cpp = @(@{ label = "cpp unique_ptr"; file = "tree.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp new/delete"; file = "tree.cpp"; variant = 1; tier = "idiomatic" },
               @{ label = "cpp arena + indices"; file = "tree.cpp"; variant = 2; tier = "expert" })
       rust = @(@{ label = "rust Box nodes"; file = "tree_box.rs" },
                @{ label = "rust arena + indices"; file = "tree_arena.rs" }) },

    @{ name = "interp"
       summary = 'Goose is 1.3x faster than the best Rust row on 2.0x less memory, and 1.3x faster than a C++ tagged union on 2.3x less. Rust dispatches well -- a byte tag, a jump table, no vtable pointer -- and its arena row is the most compact of the non-Goose rows, so the remaining gap is representation: a variable-mode leaf costs 5 bytes and a binary node 9, where every fixed enum pays max-payload for both. The Rust Box row is the slowest in the benchmark at 453 ms, behind even C++ virtual dispatch, because an allocation per node plus a recursive drop costs more than a vtable does.'
       what = "Build an expression tree of the given depth, evaluate it eight times."
       param = "depth"
       sizes = @(16, 20, 24)
       goose = @(@{ label = "goose case functions + relative refs"; file = "interp.goose" })
       cpp = @(@{ label = "cpp virtual + unique_ptr"; file = "interp.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp variant + arena"; file = "interp.cpp"; variant = 1; tier = "expert" },
               @{ label = "cpp tagged union + arena"; file = "interp.cpp"; variant = 2; tier = "expert" })
       rust = @(@{ label = "rust enum + Box"; file = "interp_box.rs" },
                @{ label = "rust enum + arena indices"; file = "interp_arena.rs" }) },

    @{ name = "graph"
       summary = 'A wash where it counts: Goose CSR, C++ CSR and Rust CSR are level (763-815 ms on the same memory), which is the honest result, because every language wants CSR here. The one-pass linked build is 5-6x slower than CSR in all three, so that is a data-structure effect and not a language one. What Goose buys is that its one-pass version is written with real Edge& references into a growing pool; the Rust equivalent must chain u32 indices, and there is no safe Rust spelling of the pointer version at any level of effort. Rust Vec<Vec> is 1.7x faster than the C++ vector<vector> it mirrors. This is the noisiest benchmark in the suite: between full harness runs the linked rows move by up to 23%, in every language at once, so only the large gaps here mean anything.'
       what = "Build adjacency for V vertices and 8V edges in one pass, then BFS from four sources."
       param = "V"
       sizes = @(100000, 500000, 2000000)
       goose = @(@{ label = "goose one-pass, typed relative refs"; file = "graph.goose" },
                 @{ label = "goose CSR two-pass"; file = "graph_csr.goose" })
       cpp = @(@{ label = "cpp vector<vector>"; file = "graph.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp CSR two-pass"; file = "graph.cpp"; variant = 1; tier = "expert" },
               @{ label = "cpp arena + indices"; file = "graph.cpp"; variant = 2; tier = "expert" })
       rust = @(@{ label = "rust Vec<Vec>"; file = "graph_nested.rs" },
                @{ label = "rust CSR two-pass"; file = "graph_csr.rs" },
                @{ label = "rust arena + indices"; file = "graph_arena.rs" }) },

    @{ name = "words"
       summary = 'Goose is level with a hand-rolled open-addressed table in Rust (1,951-1,809 ms against 2,028) on 21% less memory than it, and 1.4-1.5x faster than HashMap<&str> on 4% less. Both languages get borrowed keys from their idiomatic map -- that is the Rust default, and the thing a C++ programmer has to be talked into -- so the HashMap gap is the hash function: std SipHash-1-3 is keyed and DoS-resistant by policy, which costs about 35% here, and which the Rust community answers with a third-party hasher crate that this std-only suite does not use.'
       what = "Count word frequencies over a text of N words."
       sizes = @(500000, 4000000, 16000000)
       goose = @(@{ label = "goose slices + open addressing"; file = "words.goose" })
       cpp = @(@{ label = "cpp unordered_map<string>"; file = "words.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp unordered_map<string_view>"; file = "words.cpp"; variant = 1; tier = "idiomatic" },
               @{ label = "cpp open addressing"; file = "words.cpp"; variant = 2; tier = "expert" })
       rust = @(@{ label = "rust HashMap<&str>"; file = "words_hashmap.rs" },
                @{ label = "rust open addressing"; file = "words_open.rs" }) },

    @{ name = "particles"
       summary = 'The flat float kernel, where Goose is never ahead: Rust 861 ms against Goose 882 under v145 and 909 under clang, with C++ spanning 842-930, on identical memory. The clang side still carries the 6% that caching data stack tops in locals cost both float kernels last round (measured then in a per-commit A/B with -falign-loops=32 controlling for code alignment), the first item on the compiler list. Elementwise notation is free in both languages that can express it: Goose 912 against 882, Rust 892 against 861. The struct-of-arrays C++ row is slower than array-of-structs under both toolchains.'
       what = "Integrate N particles for 200 steps of f32 vector math."
       sizes = @(100000, 1000000, 4000000)
       goose = @(@{ label = "goose AoS, elementwise"; file = "particles.goose" },
                 @{ label = "goose AoS, per component"; file = "particles_scalar.goose" })
       cpp = @(@{ label = "cpp AoS, per component"; file = "particles.cpp"; variant = 0; tier = "expert" },
               @{ label = "cpp AoS, elementwise"; file = "particles.cpp"; variant = 2; tier = "idiomatic" },
               @{ label = "cpp SoA"; file = "particles.cpp"; variant = 1; tier = "expert" })
       rust = @(@{ label = "rust AoS, elementwise"; file = "particles.rs" },
                @{ label = "rust AoS, per component"; file = "particles_scalar.rs" }) },

    @{ name = "sexp"
       summary = 'The parse flagship: Rust 1,573 ms against Goose 1,411-1,484, on 1.9x the memory. Both fast rows are arenas whose symbols borrow the source text; Goose stores symbol bytes inline behind a varint length in nodes exactly as big as their variant needs, and never frees. The idiomatic owning shapes are 2.8x slower in Rust and 3.2-3.5x in C++. The Rust borrow works here only because the text is complete before parsing starts -- a parser that interned or rewrote text while building nodes would be back to owned Strings or offsets.'
       what = "Generate N s-expression forms, parse them into a tree, walk it four times."
       sizes = @(20000, 100000, 300000)
       goose = @(@{ label = "goose pool + refs, inline text"; file = "sexp.goose" })
       cpp = @(@{ label = "cpp unique_ptr + string"; file = "sexp.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp arena + string_view"; file = "sexp.cpp"; variant = 1; tier = "expert" })
       rust = @(@{ label = "rust enum + Box + String"; file = "sexp_box.rs" },
                @{ label = "rust arena + &str"; file = "sexp_arena.rs" }) },

    @{ name = "lru"
       summary = 'The clearest loss in the suite, and an informative one: Goose 3,568/3,035 ms against the C++ arena 2,309/2,154 and Rust''s open-addressed arena 2,170, on the smallest memory of the three (133 MB against 181 and 137). The textbook std::list + unordered_map is 2.6-2.9x slower than Goose and Rust''s HashMap row 1.3-1.5x, so the shape wins; it is the links that lose. The same Goose program with i32 index links runs in 2,397 ms and with plain 8-byte references in 2,557: the self-relative encoding -- an offset computed and range-checked on every relink, six per hit -- costs 1.5x on a workload that does nothing but relink, in exchange for 16-byte nodes. The reusable pool is not the cost: the index variant frees and reuses through the same freelist.'
       what = "An LRU cache of capacity N/8 under N skewed lookups, with an invalidation every 16th operation."
       sizes = @(2000000, 8000000, 32000000)
       goose = @(@{ label = "goose reusable pool + relative refs"; file = "lru.goose" })
       cpp = @(@{ label = "cpp list + unordered_map"; file = "lru.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp arena + open addressing"; file = "lru.cpp"; variant = 1; tier = "expert" })
       rust = @(@{ label = "rust HashMap + arena"; file = "lru_hashmap.rs" },
                @{ label = "rust open addressing + arena"; file = "lru_open.rs" }) },

    @{ name = "scene"
       summary = 'Under clang Goose and the Rust arena are level (382 against 375 ms) on 1.35x less memory; under v145 Goose is 24% behind, and so is the C++ arena (493), so that is the backend''s handling of this float-and-pointer mix and not the layout -- padding the 117-byte node to an aligned 120 changes nothing. Against the idiomatic rows, a vector<unique_ptr> or Vec<Box> of children per node, Goose is 5.5-6.8x faster on 1.3x less memory. The Rust arena has to compose each transform in a stack local and copy it in, because two borrows of one Vec cannot be split; Goose composes straight into the pool through references.'
       what = "Build a scene graph of the given depth, then animate it and recompute world transforms for 16 frames."
       param = "depth"
       sizes = @(12, 14, 16)
       goose = @(@{ label = "goose pool + inline child refs"; file = "scene.goose" })
       cpp = @(@{ label = "cpp unique_ptr children"; file = "scene.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp arena + child indices"; file = "scene.cpp"; variant = 1; tier = "expert" })
       rust = @(@{ label = "rust Box children"; file = "scene_box.rs" },
                @{ label = "rust arena + child indices"; file = "scene_arena.rs" }) },

    @{ name = "calc"
       summary = 'Goose beats the idiomatic rows by a wide margin -- 3.8-3.9x against unique_ptr nodes with exceptions, 2.1x against Box with Result -- and trails the arena rows: the C++ arena with error codes by 7% under v145 and 19% under clang, the Rust arena with Result by 21-24%. The per-input local pool is not the cost: a variant that builds into a global pool never reset times the same. What remains is the discriminant threaded through every return (spec 7.9), the global cursor, and the varint decode per evaluated number, on inputs of about 40 bytes, in a run that never leaves L1.'
       what = "Generate, parse and evaluate N small arithmetic expressions, a third of them malformed."
       sizes = @(100000, 400000, 1600000)
       goose = @(@{ label = "goose local pool + return from"; file = "calc.goose" })
       cpp = @(@{ label = "cpp unique_ptr + exceptions"; file = "calc.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp arena + error codes"; file = "calc.cpp"; variant = 1; tier = "expert" })
       rust = @(@{ label = "rust Box + Result"; file = "calc_box.rs" },
                @{ label = "rust arena + Result"; file = "calc_arena.rs" }) },

    @{ name = "bintrees"
       summary = 'The allocator benchmark: Goose is 8x faster than new/delete and Box on 2x less memory, level with the C++ vector arena (353-376 against 384-397 ms), and 1.6x behind the Rust Vec arena (227). The split is 243 ms building and 115 checking for Goose against 153 and 74 for Rust, so both phases carry the same factor. It is not the by-reference push (a global-pool variant is slower, 430 ms, because it never reuses memory) and not the range-checked 4-byte links (8-byte links time the same); the C++ arena lands with Goose, so the gap is between what LLVM makes of rustc''s recursion and what either C backend makes of the same shape.'
       what = "The Benchmarks Game binary-trees: build, check and discard many trees up to the given depth."
       param = "depth"
       sizes = @(15, 17, 19)
       goose = @(@{ label = "goose local pool per tree"; file = "bintrees.goose" })
       cpp = @(@{ label = "cpp new/delete"; file = "bintrees.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp vector arena"; file = "bintrees.cpp"; variant = 1; tier = "expert" })
       rust = @(@{ label = "rust Box"; file = "bintrees_box.rs" },
                @{ label = "rust Vec arena"; file = "bintrees_arena.rs" }) },

    @{ name = "respond"
       summary = 'The DTO rows are where the design shows: Goose builds and renders the response object 1.5-1.8x faster than the idiomatic C++ DTO and 2.1x faster than the Rust one, on the same 8 MB, because the string, the item list and the skus are one inline value and the render reads it back with no allocation anywhere. The streaming rows are the same code in all three languages -- Goose 427/419 ms, C++ 490/401, Rust 532 -- and the two Goose rows are 30% apart, which is what materialising the object costs even when it is free to allocate.'
       what = "Build and render N JSON responses, each a DTO holding a name and a list of line items."
       sizes = @(100000, 400000, 1600000)
       goose = @(@{ label = "goose inline DTO"; file = "respond.goose" },
                 @{ label = "goose streaming"; file = "respond_stream.goose" })
       cpp = @(@{ label = "cpp DTO + string"; file = "respond.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp streaming"; file = "respond.cpp"; variant = 1; tier = "expert" })
       rust = @(@{ label = "rust DTO + String"; file = "respond_dto.rs" },
                @{ label = "rust streaming"; file = "respond_stream.rs" }) },

    @{ name = "blur"
       summary = 'Included to lose, and it does, in two different ways. The flat form is 1.8x slower than flat C++ and 6.4x slower than flat Rust: under v145 the nine bounds checks per pixel stop the loop vectorising, and under clang the checks are free but the fat reference''s base and length are reloaded after every store because a byte store may alias them -- the design''s no-aliasing-information cost, measured. Written over row slices with one assert per row, every check is proven away and both problems disappear: Goose 304/263 ms against Rust 287-292 and C++ with __restrict 552/305. Rust''s flat row is as fast as its slice row because LLVM vectorises around bounds checks it cannot remove.'
       what = "16 passes of a 3x3 blur over a WxW 8-bit image."
       param = "W"
       sizes = @(1024, 2048, 8192)
       goose = @(@{ label = "goose flat indexing"; file = "blur.goose" },
                 @{ label = "goose row slices"; file = "blur_rows.goose" })
       cpp = @(@{ label = "cpp flat indexing"; file = "blur.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp row pointers"; file = "blur.cpp"; variant = 1; tier = "expert" })
       rust = @(@{ label = "rust flat indexing"; file = "blur_index.rs" },
                @{ label = "rust row slices"; file = "blur_windows.rs" }) }
)

# --- toolchains --------------------------------------------------------------

# The newest installed Visual Studio, preferring 2026 over 2022, and the clang
# that ships inside it. Importing vcvars once gives both compilers the same
# headers, libraries and CRT.
function Import-VcVars {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere not found: Visual Studio is required" }
    $root = & $vswhere -latest -prerelease -property installationPath 2>$null
    if (-not $root) { throw "no Visual Studio installation found" }
    foreach ($l in (cmd /c "`"$root\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set")) {
        if ($l -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
    }
    return $root
}

$vsroot = Import-VcVars
$clangcl = "$vsroot\VC\Tools\Llvm\x64\bin\clang-cl.exe"
# Via cmd: redirecting a native tool's stderr inside PowerShell 5.1 turns each
# line into an ErrorRecord, which $ErrorActionPreference = Stop then throws on.
$clVersion = ((cmd /c "cl 2>&1" | Select-Object -First 1) -replace '^.*Version\s+', '' -replace '\s+for.*$', '')
$toolsetVersion = $env:VCToolsVersion
$clangVersion = if (Test-Path $clangcl) {
    ((& $clangcl --version | Select-Object -First 1) -replace '^.*version\s+', '' -replace '\s.*$', '')
} else { $null }

$tcdefs = [ordered]@{
    v145  = @{ name = "v145"; cc = "cl"; desc = "MSVC $clVersion (toolset $toolsetVersion)" }
    clang = @{ name = "clang"; cc = $clangcl; desc = "clang-cl $clangVersion (bundled with VS)" }
}
if (-not $clangVersion) { $Toolchains = @($Toolchains | Where-Object { $_ -ne "clang" }) }
$activeTc = @($tcdefs.Keys | Where-Object { $Toolchains -contains $_ })
if (-not $activeTc) { throw "no usable toolchain selected" }

# rustup installs into %USERPROFILE%\.cargo\bin and puts it on the PATH of
# shells started afterwards, which is not necessarily this one.
$rustc = (Get-Command rustc.exe -ErrorAction SilentlyContinue)
$rustcPath = if ($rustc) { $rustc.Source } else { "$env:USERPROFILE\.cargo\bin\rustc.exe" }
$haveRust = (Test-Path $rustcPath) -and (Test-Path $rustdir)
$rustVersion = if ($haveRust) { (& $rustcPath --version) } else { $null }
# Rust is not built by the C/C++ toolchains, so its measurements are filed
# under a toolchain of their own and its rows carry one time, not one per
# backend.
$rustTc = "rustc"

New-Item -ItemType Directory -Force $gendir | Out-Null
Copy-Item "$goosedir\rng.goose" $gendir -Force      # imports resolve next to the root file
if ($haveRust) { Copy-Item "$rustdir\bench.rs" $gendir -Force }   # `mod bench;` resolves next to the root file

# --- build and run helpers ---------------------------------------------------

function Write-Sized-Goose([string]$src, [string]$dst, [long]$n) {
    $lines = Get-Content $src
    $hit = $false
    $outl = foreach ($l in $lines) {
        if (-not $hit -and $l -match '^(\s*let\s+\w+\s*=\s*)(\d+)(\s*;.*//\s*BENCH_N.*)$') {
            $hit = $true
            "$($matches[1])$n$($matches[3])"
        } else { $l }
    }
    if (-not $hit) { throw "no '// BENCH_N' line in $src" }
    [IO.File]::WriteAllLines($dst, $outl, (New-Object Text.UTF8Encoding($false)))
}

# The same trick for Rust, which has no preprocessor to take a -D through.
function Write-Sized-Rust([string]$src, [string]$dst, [long]$n) {
    $lines = Get-Content $src
    $hit = $false
    $outl = foreach ($l in $lines) {
        if (-not $hit -and $l -match '^(\s*const\s+\w+\s*:\s*\w+\s*=\s*)(\d+)(\s*;.*//\s*BENCH_N.*)$') {
            $hit = $true
            "$($matches[1])$n$($matches[3])"
        } else { $l }
    }
    if (-not $hit) { throw "no '// BENCH_N' line in $src" }
    [IO.File]::WriteAllLines($dst, $outl, (New-Object Text.UTF8Encoding($false)))
}

# Peak working set has to come from the process handle: .NET's
# PeakWorkingSet64 reads a live snapshot and reports 0 once the process is
# gone, which is exactly when we want to ask.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class MemProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct PMC {
        public uint cb, PageFaultCount;
        public UIntPtr PeakWorkingSetSize, WorkingSetSize;
        public UIntPtr QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
        public UIntPtr QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage;
        public UIntPtr PagefileUsage, PeakPagefileUsage;
    }
    [DllImport("psapi.dll", SetLastError = true)]
    static extern bool GetProcessMemoryInfo(IntPtr h, out PMC c, uint size);
    public static long Peak(IntPtr h) {
        PMC c = new PMC();
        c.cb = (uint)Marshal.SizeOf(typeof(PMC));
        if (!GetProcessMemoryInfo(h, out c, c.cb)) return 0;
        return (long)c.PeakWorkingSetSize.ToUInt64();
    }
}
'@

# Runs an executable once, returning elapsed ms, peak working set and stdout.
function Invoke-Timed([string]$exe) {
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $p = New-Object Diagnostics.Process
    $p.StartInfo = $psi
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p.Start() | Out-Null
    $h = $p.Handle                      # keep the handle alive past exit
    $stdout = $p.StandardOutput.ReadToEnd()
    $stderr = $p.StandardError.ReadToEnd()
    $p.WaitForExit()
    $sw.Stop()
    $peak = [MemProbe]::Peak($h)
    $code = $p.ExitCode
    $p.Dispose()
    # Line endings are normalised before the checksums are compared: C stdio
    # writes CRLF on Windows and Rust's println! writes LF, which would make
    # identical numbers look like a mismatch.
    return @{ ms = $sw.Elapsed.TotalMilliseconds; peak = $peak
              out = ($stdout -replace "`r`n", "`n").Trim()
              err = ($stderr -replace "`r`n", "`n").Trim(); code = $code }
}

function Measure-Exe([string]$exe, [int]$reps) {
    $best = $null
    $peak = 0
    $out = $null
    # Two discarded warm-up runs: a freshly written executable is paged in and
    # scanned by the OS on first execution, which on this machine costs up to
    # 10x the run itself and takes about three executions to settle. It hits
    # clang-linked binaries far harder than MSVC-linked ones, so without this
    # the toolchain comparison would measure Defender.
    for ($w = 0; $w -lt 2; $w++) {
        $r0 = Invoke-Timed $exe
        if ($r0.code -ne 0) {
            return @{ ok = $false; note = "exit $($r0.code): $($r0.err -split "`n" | Select-Object -First 1)" }
        }
    }
    for ($i = 0; $i -lt $reps; $i++) {
        $r = Invoke-Timed $exe
        if ($r.code -ne 0) {
            return @{ ok = $false; note = "exit $($r.code): $($r.err -split "`n" | Select-Object -First 1)" }
        }
        if ($null -eq $best -or $r.ms -lt $best) { $best = $r.ms }
        if ($r.peak -gt $peak) { $peak = $r.peak }
        $out = $r.out
    }
    return @{ ok = $true; ms = $best; peak = $peak; out = $out }
}

# The Goose source is sized once per (benchmark, size) and compiled to C once;
# each toolchain then builds that same C, so the two rows differ only in the
# backend.
$gooseGenerated = @{}      # base names whose C this run has already produced

function Build-Goose([string]$file, [long]$n, [string]$tag, [string]$tc) {
    $base = ($tag -replace "_$tc$", "")
    $cfile = "$gendir\$base.c"
    # Generated once per (benchmark, size) and reused across toolchains, but
    # only within one run: a stale .c left over from a previous run would
    # silently benchmark the previous version of the source.
    if (-not $gooseGenerated.ContainsKey($base)) {
        $gsrc = "$gendir\$base.goose"
        Write-Sized-Goose "$goosedir\$file" $gsrc $n
        & $Exe -O2 -o $cfile $gsrc > "$gendir\$base.goose.log" 2>&1
        if ($LASTEXITCODE -ne 0) { return @{ ok = $false; note = "goose compile failed, see $base.goose.log" } }
        $gooseGenerated[$base] = $true
    }
    $exe = "$gendir\$tag.exe"
    & $tcdefs[$tc].cc /nologo /O2 "/DGS_STACK_RESERVE=$stackReserve" $cfile `
        "/Fe:$exe" "/Fo:$gendir\$tag.obj" > "$gendir\$tag.cc.log" 2>&1
    if ($LASTEXITCODE -ne 0) { return @{ ok = $false; note = "$tc failed, see $tag.cc.log" } }
    return @{ ok = $true; exe = $exe }
}

function Build-Cpp([string]$file, [int]$variant, [long]$n, [string]$tag, [string]$tc) {
    $exe = "$gendir\$tag.exe"
    & $tcdefs[$tc].cc /nologo /O2 /EHsc /std:c++20 "/DBENCH_N=$n" "/DVARIANT=$variant" "$cppdir\$file" `
        "/Fe:$exe" "/Fo:$gendir\$tag.obj" > "$gendir\$tag.cc.log" 2>&1
    if ($LASTEXITCODE -ne 0) { return @{ ok = $false; note = "$tc failed, see $tag.cc.log" } }
    return @{ ok = $true; exe = $exe }
}

# `-C codegen-units=1` is what makes this comparable to the C++ rows: each of
# those is one translation unit compiled whole, and the rustc default of 16
# codegen units would deny Rust the same cross-function view. Everything else
# is left at the defaults a Rust program actually ships with -- bounds checks
# on, unwinding panics, no target-cpu beyond the x86-64 baseline the C++ rows
# also build for.
function Build-Rust([string]$file, [long]$n, [string]$tag) {
    $src = "$gendir\$tag.rs"
    Write-Sized-Rust "$rustdir\$file" $src $n
    $exe = "$gendir\$tag.exe"
    & $rustcPath -O -C codegen-units=1 -o $exe $src > "$gendir\$tag.rs.log" 2>&1
    if ($LASTEXITCODE -ne 0) { return @{ ok = $false; note = "rustc failed, see $tag.rs.log" } }
    return @{ ok = $true; exe = $exe }
}

# --- baseline ----------------------------------------------------------------
# What an empty program of each kind costs, so the reader can discount process
# startup from the small sizes.

function Measure-Baseline {
    $res = @{}
    $tc = $activeTc[0]
    $g = "$gendir\baseline_goose.goose"
    [IO.File]::WriteAllText($g, "fn main() { print(0); }`n", (New-Object Text.UTF8Encoding($false)))
    & $Exe -O2 -o "$gendir\baseline_goose.c" $g > $null 2>&1
    & $tcdefs[$tc].cc /nologo /O2 "/DGS_STACK_RESERVE=$stackReserve" "$gendir\baseline_goose.c" `
        "/Fe:$gendir\baseline_goose.exe" "/Fo:$gendir\baseline_goose.obj" > $null 2>&1
    $c = "$gendir\baseline_cpp.cpp"
    [IO.File]::WriteAllText($c, "#include <cstdio>`nint main(){printf(`"0\n`");}`n", (New-Object Text.UTF8Encoding($false)))
    & $tcdefs[$tc].cc /nologo /O2 /EHsc $c "/Fe:$gendir\baseline_cpp.exe" "/Fo:$gendir\baseline_cpp.obj" > $null 2>&1
    foreach ($k in "goose", "cpp") { $res[$k] = Measure-Exe "$gendir\baseline_$k.exe" 5 }
    return $res
}

# --- run ---------------------------------------------------------------------

$sizeNames = @("small", "medium", "large")
$selected = if ($Only.Count) { $benchmarks | Where-Object { $Only -contains $_.name } } else { $benchmarks }
if (-not $selected) { throw "no benchmarks matched -Only $($Only -join ',')" }

Write-Host "toolchains: $(($activeTc | ForEach-Object { $tcdefs[$_].desc }) -join '  |  ')"
Write-Host "sizes: $($Sizes -join ', ')   reps: $Reps`n"

$baseline = if ($SkipBuild -or $ReportOnly) { $null } else { Measure-Baseline }

$results = @{}      # results[bench][impl][toolchain][size] = measurement
$mismatch = @{}
$measured = @{}     # "bench/size" keys refreshed by this run
$statefile = "$gendir\results.clixml"

# -ReportOnly regenerates results.md from saved measurements, so the
# hand-written commentary in notes.md can be edited without a rebuild.
if ($ReportOnly) {
    if (-not (Test-Path $statefile)) { throw "no saved measurements at $statefile; run without -ReportOnly first" }
    $state = Import-Clixml $statefile
    $results = $state.results
    $baseline = $state.baseline
    $mismatch = $state.mismatch
    $Reps = $state.reps
    $Sizes = $state.sizes
    $activeTc = $state.toolchains
    $tcdefs = $state.tcdefs
    $selected = $benchmarks | Where-Object { $results.ContainsKey($_.name) -and
                                             ($Only.Count -eq 0 -or $Only -contains $_.name) }
}

foreach ($b in $(if ($ReportOnly) { @() } else { $selected })) {
    if (-not $results.ContainsKey($b.name)) { $results[$b.name] = @{} }
    foreach ($si in 0..2) {
        $sname = $sizeNames[$si]
        if ($Sizes -notcontains $sname) { continue }
        $n = $b.sizes[$si]
        $checks = @{}
        $impls = @()
        foreach ($g in $b.goose) { $impls += @{ kind = "goose"; label = $g.label; file = $g.file } }
        foreach ($c in $b.cpp)   { $impls += @{ kind = "cpp"; label = $c.label; file = $c.file; variant = $c.variant } }
        if ($haveRust -and $b.Contains("rust")) {
            foreach ($u in $b.rust) { $impls += @{ kind = "rust"; label = $u.label; file = $u.file } }
        }
        foreach ($im in $impls) {
            $slug = ($im.label -replace '[^A-Za-z0-9]+', '_').Trim('_')
            if (-not $results[$b.name].Contains($im.label)) { $results[$b.name][$im.label] = @{} }
            # Rust has one backend, so its rows are built and timed once rather
            # than once per C/C++ toolchain.
            $tcs = if ($im.kind -eq "rust") { @($rustTc) } else { $activeTc }
            foreach ($tc in $tcs) {
                $tag = "$($b.name)_${sname}_${slug}_$tc"
                if (-not $results[$b.name][$im.label].Contains($tc)) { $results[$b.name][$im.label][$tc] = @{} }
                if ($SkipBuild) {
                    $build = @{ ok = (Test-Path "$gendir\$tag.exe"); exe = "$gendir\$tag.exe"; note = "not built" }
                } elseif ($im.kind -eq "goose") {
                    $build = Build-Goose $im.file $n $tag $tc
                } elseif ($im.kind -eq "rust") {
                    $build = Build-Rust $im.file $n $tag
                } else {
                    $build = Build-Cpp $im.file $im.variant $n $tag $tc
                }
                if (-not $build.ok) {
                    Write-Host ("  {0,-32} {1,-7} {2,-5} BUILD FAIL: {3}" -f $im.label, $sname, $tc, $build.note)
                    $results[$b.name][$im.label][$tc][$sname] = @{ ok = $false; note = "build failed" }
                    continue
                }
                $m = Measure-Exe $build.exe $Reps
                $results[$b.name][$im.label][$tc][$sname] = $m
                if ($m.ok) {
                    $checks["$($im.label) [$tc]"] = $m.out
                    Write-Host ("  {0,-32} {1,-7} {2,-5} {3,9:N1} ms  {4,8:N1} MB" -f `
                                $im.label, $sname, $tc, $m.ms, ($m.peak / 1MB))
                } else {
                    Write-Host ("  {0,-32} {1,-7} {2,-5} RUN FAIL: {3}" -f $im.label, $sname, $tc, $m.note)
                }
            }
        }
        # Every implementation, under every toolchain, must agree.
        $measured["$($b.name)/$sname"] = $true
        $distinct = $checks.Values | Sort-Object -Unique
        if (@($distinct).Count -gt 1) {
            $mismatch["$($b.name)/$sname"] = $checks
            Write-Host "  !! checksum mismatch in $($b.name)/$sname" -ForegroundColor Red
        }
    }
    Write-Host ""
}

# --- report ------------------------------------------------------------------

# A partial run (-Only, -Sizes, -Toolchains) refreshes part of the picture; it
# merges into what previous runs measured rather than replacing it, so
# results.md always shows everything measured so far.
if (-not $ReportOnly) {
    if (Test-Path $statefile) {
        $prev = Import-Clixml $statefile
        $merged = $prev.results
        foreach ($bn in $results.Keys) {
            if (-not $merged.ContainsKey($bn)) { $merged[$bn] = @{} }
            foreach ($lbl in $results[$bn].Keys) {
                if (-not $merged[$bn].Contains($lbl)) { $merged[$bn][$lbl] = @{} }
                foreach ($tc in $results[$bn][$lbl].Keys) {
                    if (-not $merged[$bn][$lbl].Contains($tc)) { $merged[$bn][$lbl][$tc] = @{} }
                    foreach ($sz in $results[$bn][$lbl][$tc].Keys) {
                        $merged[$bn][$lbl][$tc][$sz] = $results[$bn][$lbl][$tc][$sz]
                    }
                }
            }
        }
        $results = $merged
        # A mismatch recorded earlier is cleared by a run that re-measures
        # that benchmark and size and finds them agreeing.
        foreach ($k in $prev.mismatch.Keys) {
            if (-not $mismatch.Contains($k) -and -not $measured.ContainsKey($k)) { $mismatch[$k] = $prev.mismatch[$k] }
        }
        if (-not $baseline) { $baseline = $prev.baseline }
        $Sizes = @($sizeNames | Where-Object { $prev.sizes -contains $_ -or $Sizes -contains $_ })
        $activeTc = @($tcdefs.Keys | Where-Object { $prev.toolchains -contains $_ -or $activeTc -contains $_ })
    }
    Export-Clixml -Path $statefile -InputObject @{
        results = $results; baseline = $baseline; mismatch = $mismatch
        reps = $Reps; sizes = $Sizes; toolchains = $activeTc; tcdefs = $tcdefs
    }
    $selected = $benchmarks | Where-Object { $results.ContainsKey($_.name) }
}

function Get-M($bench, $label, $tc, $size) {
    if (-not $results.ContainsKey($bench)) { return $null }
    if (-not $results[$bench].Contains($label)) { return $null }
    if (-not $results[$bench][$label].Contains($tc)) { return $null }
    return $results[$bench][$label][$tc][$size]
}
function Fmt-Ms($m) { if ($m -and $m.ok) { "{0:N1}" -f $m.ms } else { "--" } }
function Fmt-Mb($m) { if ($m -and $m.ok) { "{0:N1}" -f ($m.peak / 1MB) } else { "--" } }

$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim()
$ram = "{0:N0} GB" -f ((Get-CimInstance Win32_OperatingSystem).TotalVisibleMemorySize / 1MB)
$activeSizes = $sizeNames | Where-Object { $Sizes -contains $_ }

$md = New-Object Text.StringBuilder
function W($s) { [void]$md.AppendLine($s) }

W "# Goose benchmark results"
W ""
W "Generated by ``bench/run_bench.ps1`` on $(Get-Date -Format 'yyyy-MM-dd HH:mm'). Do not edit by hand."
W "The benchmarks themselves are ``bench/goose/*.goose``, ``bench/cpp/*.cpp`` and"
W "``bench/rust/*.rs``; what they are trying to find out is written down in"
W "``bench/design.md``."
W ""
W "## How to read this"
W ""
W "* Start with **Across all benchmarks**: it is the whole suite in one table. The"
W "  per-benchmark sections below it are the detail, each ending in a sentence or two"
W "  of what that row means."
W "* One table per benchmark. Rows are implementations, the three time columns are the"
W "  data sizes -- the small one is meant to fit in cache, the large one is meant not"
W "  to -- and the last column is peak memory at the large size."
W "* Every Goose and C++ time cell is ``v145 / clang``: the same source built by MSVC"
W "  and by the clang-cl that ships in the same Visual Studio, so a gap between the two"
W "  numbers is the backend and nothing else. Goose is compiled to C once per size and"
W "  both toolchains build that same C. Rust rows carry a single time, because rustc is"
W "  the only Rust backend here; it is LLVM, so it is the clang column they are most"
W "  directly comparable with."
W "* Times are the best of $Reps whole-process wall clocks, after two discarded warm-up"
W "  runs. Memory is the peak working set, the larger of the two toolchains (they"
W "  rarely differ)."
W "* Goose has no clock builtin, so there is no in-process timer common to all"
W "  languages: everything a program does -- generating its input, building its"
W "  structure, tearing it down -- is inside the measurement. That is deliberate for"
W "  teardown, which is a real cost the other languages pay and Goose does not, and it"
W "  dilutes the ratios wherever input generation is a large share of the work."
W "* Every implementation of a benchmark, under every toolchain, prints the same"
W "  checksum lines and the harness verifies they agree."
W ""
W "## Environment"
W ""
W "| | |"
W "|---|---|"
W "| CPU | $cpu |"
W "| RAM | $ram |"
W "| OS | $([Environment]::OSVersion.VersionString) |"
foreach ($tc in $activeTc) { W "| $tc | $($tcdefs[$tc].desc), ``/O2`` (C++20 for the C++ rows) |" }
W "| Goose | ``goose -O2`` to C, then each toolchain above on that C |"
$rustDesc = if ($haveRust) { "$rustVersion, ``-O -C codegen-units=1``" } else { 'not installed -- rows pending' }
W "| Rust | $rustDesc |"
W ""
if ($baseline) {
    W "Startup floor (an empty program, best of 5): Goose $("{0:N1}" -f $baseline.goose.ms) ms / $("{0:N1}" -f ($baseline.goose.peak / 1MB)) MB, C++ $("{0:N1}" -f $baseline.cpp.ms) ms / $("{0:N1}" -f ($baseline.cpp.peak / 1MB)) MB."
    W "Subtract that from the small sizes before believing any ratio there."
    W ""
}
if ($mismatch.Count) {
    W "> **Checksum mismatches:** $($mismatch.Keys -join ', '). Those rows are not comparable."
    W ""
}

# --- aggregate ---------------------------------------------------------------
# How Goose compares to each C++ tier over the whole suite, per toolchain. Per
# benchmark the ratio is (fastest C++ row of that tier) / (fastest Goose row),
# both under the same toolchain; the suite figure is the geometric mean, which
# is the right average for ratios.

function Best-Time($bench, $labels, $tc, $size) {
    $best = $null
    foreach ($l in $labels) {
        $m = Get-M $bench $l $tc $size
        if ($m -and $m.ok -and ($null -eq $best -or $m.ms -lt $best)) { $best = $m.ms }
    }
    return $best
}
# Over whichever toolchains actually measured this label, so the Rust rows --
# filed under `rustc` alone -- are picked up the same way as the others.
function Best-Mem($bench, $labels, $size) {
    $best = $null
    foreach ($l in $labels) {
        if (-not $results[$bench].Contains($l)) { continue }
        foreach ($tc in $results[$bench][$l].Keys) {
            $m = Get-M $bench $l $tc $size
            if ($m -and $m.ok -and ($null -eq $best -or $m.peak -lt $best)) { $best = $m.peak }
        }
    }
    return $best
}
function GeoMean($xs) {
    $xs = @($xs | Where-Object { $_ -and $_ -gt 0 })
    if (-not $xs.Count) { return $null }
    $t = 0.0
    foreach ($x in $xs) { $t += [Math]::Log($x) }
    return [Math]::Exp($t / $xs.Count)
}

if ($activeSizes -contains "large") {
    $ratios = @{}          # ratios[tier][tc] = list of per-benchmark ratios
    $memRatios = @{}
    $perBench = @()
    foreach ($b in $selected) {
        if (-not $results[$b.name].Count) { continue }
        $gl = @($b.goose | ForEach-Object { $_.label } | Where-Object { $results[$b.name].Contains($_) })
        if (-not $gl.Count) { continue }
        $row = @{ bench = $b.name }
        foreach ($tier in "idiomatic", "expert") {
            $cl = @($b.cpp | Where-Object { $_.tier -eq $tier } | ForEach-Object { $_.label } |
                    Where-Object { $results[$b.name].Contains($_) })
            if (-not $cl.Count) { continue }
            foreach ($tc in $activeTc) {
                $g = Best-Time $b.name $gl $tc "large"
                $c = Best-Time $b.name $cl $tc "large"
                if ($g -and $c) {
                    if (-not $ratios.ContainsKey($tier)) { $ratios[$tier] = @{} }
                    if (-not $ratios[$tier].ContainsKey($tc)) { $ratios[$tier][$tc] = @() }
                    $ratios[$tier][$tc] += ($c / $g)
                    $row["$tier-$tc"] = $c / $g
                }
            }
            $gm = Best-Mem $b.name $gl "large"
            $cm = Best-Mem $b.name $cl "large"
            if ($gm -and $cm) {
                if (-not $memRatios.ContainsKey($tier)) { $memRatios[$tier] = @() }
                $memRatios[$tier] += ($cm / $gm)
                $row["$tier-mem"] = $cm / $gm
            }
        }
        # Rust is one column pair rather than two tiers: the comparison point is
        # the fastest (and separately the smallest) safe Rust row, whichever
        # shape that turned out to be, because "the way Rust is meant to be
        # used" is a single target and picking the loser of two Rust rows would
        # be flattering Goose for nothing.
        $ul = @($(if ($b.Contains("rust")) { $b.rust | ForEach-Object { $_.label } } else { @() }) |
                Where-Object { $results[$b.name].Contains($_) })
        if ($ul.Count) {
            $u = Best-Time $b.name $ul $rustTc "large"
            foreach ($tc in $activeTc) {
                $g = Best-Time $b.name $gl $tc "large"
                if ($g -and $u) {
                    if (-not $ratios.ContainsKey("rust")) { $ratios["rust"] = @{} }
                    if (-not $ratios["rust"].ContainsKey($tc)) { $ratios["rust"][$tc] = @() }
                    $ratios["rust"][$tc] += ($u / $g)
                    $row["rust-$tc"] = $u / $g
                }
            }
            $gm = Best-Mem $b.name $gl "large"
            $um = Best-Mem $b.name $ul "large"
            if ($gm -and $um) {
                if (-not $memRatios.ContainsKey("rust")) { $memRatios["rust"] = @() }
                $memRatios["rust"] += ($um / $gm)
                $row["rust-mem"] = $um / $gm
            }
        }
        $perBench += $row
    }

    $tiers = @("idiomatic", "expert") + $(if ($ratios.ContainsKey("rust")) { @("rust") } else { @() })
    W "## Across all benchmarks"
    W ""
    W "How many times faster (or smaller) Goose is than the best row of each comparison"
    W "at the ``large`` size. *Idiomatic* is what a C++ programmer writes first"
    W "(``unique_ptr``, ``vector<string>``, ``unordered_map``, ``std::variant``);"
    W "*expert* is the hand-built version (exactly-reserved arena with indices,"
    W "``string_view``, flat offsets, open addressing, CSR); *rust* is the best safe"
    W "Rust row for that benchmark, whichever shape won. Above 1.00 means Goose"
    W "wins. The suite figure is the geometric mean over the $($perBench.Count) benchmarks."
    W ""
    W "The Rust number is the same in the v145 and clang columns -- there is only one"
    W "rustc -- so those two columns differ only in which Goose build they are against."
    W ""
    $hdr = "| benchmark |"
    $sep = "|---|"
    foreach ($tier in $tiers) {
        foreach ($tc in $activeTc) { $hdr += " vs $tier ($tc) |"; $sep += "---:|" }
    }
    foreach ($tier in $tiers) { $hdr += " memory vs $tier |"; $sep += "---:|" }
    W $hdr
    W $sep
    foreach ($r in $perBench) {
        $line = "| $($r.bench) |"
        foreach ($tier in $tiers) {
            foreach ($tc in $activeTc) {
                $v = $r["$tier-$tc"]
                $line += $(if ($v) { " {0:N2}x |" -f $v } else { " -- |" })
            }
        }
        foreach ($tier in $tiers) {
            $v = $r["$tier-mem"]
            $line += $(if ($v) { " {0:N2}x |" -f $v } else { " -- |" })
        }
        W $line
    }
    $line = "| **geometric mean** |"
    foreach ($tier in $tiers) {
        foreach ($tc in $activeTc) {
            $v = GeoMean $ratios[$tier][$tc]
            $line += $(if ($v) { " **{0:N2}x** |" -f $v } else { " -- |" })
        }
    }
    foreach ($tier in $tiers) {
        $v = GeoMean $memRatios[$tier]
        $line += $(if ($v) { " **{0:N2}x** |" -f $v } else { " -- |" })
    }
    W $line
    W ""
    if (Test-Path "$PSScriptRoot\summary.md") {
        W ([IO.File]::ReadAllText("$PSScriptRoot\summary.md").TrimEnd())
        W ""
    }
}

# --- per benchmark -----------------------------------------------------------

foreach ($b in $selected) {
    if (-not $results[$b.name].Count) { continue }
    $pname = if ($b.Contains("param")) { $b.param } else { "N" }
    $rustOf = @{}
    if ($b.Contains("rust")) { foreach ($u in $b.rust) { $rustOf[$u.label] = $true } }
    $labels = @($b.goose | ForEach-Object { $_.label }) + @($b.cpp | ForEach-Object { $_.label }) +
              @($(if ($b.Contains("rust")) { $b.rust | ForEach-Object { $_.label } } else { @() }))
    $labels = @($labels | Where-Object { $results[$b.name].Contains($_) })
    W "## $($b.name)"
    W ""
    W $b.what
    W ""
    W "Time in ms as ``$($activeTc -join ' / ')`` for the Goose and C++ rows, and a single"
    W "rustc time for the Rust ones; memory is the peak working set at the largest size."
    W ""
    $hdr = "| implementation |"
    $sep = "|---|"
    foreach ($s in $activeSizes) {
        $hdr += " $s ($pname=$($b.sizes[$sizeNames.IndexOf($s)])) |"
        $sep += "---:|"
    }
    $hdr += " large MB |"
    $sep += "---:|"
    W $hdr
    W $sep
    foreach ($label in $labels) {
        $row = "| $label |"
        $tcs = if ($rustOf.ContainsKey($label)) { @($rustTc) } else { $activeTc }
        foreach ($s in $activeSizes) {
            $cells = foreach ($tc in $tcs) { Fmt-Ms (Get-M $b.name $label $tc $s) }
            $row += " $($cells -join ' / ') |"
        }
        $peak = $null
        foreach ($tc in $tcs) {
            $m = Get-M $b.name $label $tc "large"
            if ($m -and $m.ok -and ($null -eq $peak -or $m.peak -gt $peak.peak)) { $peak = $m }
        }
        $row += " $(Fmt-Mb $peak) |"
        W $row
    }
    W ""
    if ($b.Contains("summary")) { W $b.summary; W "" }
}

# --- toolchain comparison ----------------------------------------------------
# Where the same C or C++ runs meaningfully faster under one backend, listed so
# the codegen work can be aimed at the biggest gaps.

if ($activeTc.Count -gt 1) {
    W "## Toolchain: v145 vs clang"
    W ""
    W "Ratio of v145 time to clang time at the ``large`` size (>1 means clang is faster)."
    W "Only rows differing by more than 5% are listed, biggest gap first."
    W ""
    $rows = @()
    foreach ($b in $selected) {
        foreach ($label in $results[$b.name].Keys) {
            $a = Get-M $b.name $label "v145" "large"
            $c = Get-M $b.name $label "clang" "large"
            if ($a -and $c -and $a.ok -and $c.ok) {
                $rows += @{ bench = $b.name; label = $label; ratio = $a.ms / $c.ms; v = $a.ms; c = $c.ms }
            }
        }
    }
    $interesting = @($rows | Where-Object { [Math]::Abs($_.ratio - 1) -gt 0.05 } |
                     Sort-Object { -[Math]::Abs($_.ratio - 1) })
    if ($interesting.Count) {
        W "| benchmark | implementation | v145 ms | clang ms | v145/clang |"
        W "|---|---|---:|---:|---:|"
        foreach ($r in $interesting) {
            W ("| {0} | {1} | {2:N1} | {3:N1} | {4:N2} |" -f $r.bench, $r.label, $r.v, $r.c, $r.ratio)
        }
    } else {
        W "No row differs by more than 5%."
    }
    W ""
}

# The numbers above are generated; what they mean is written by hand in
# notes.md and carried through verbatim.
if (Test-Path "$PSScriptRoot\notes.md") {
    W ([IO.File]::ReadAllText("$PSScriptRoot\notes.md").TrimEnd())
    W ""
}

if (-not $haveRust) {
    W "## Pending"
    W ""
    W "* The Rust rows are missing from this run: ``rustc`` was not found. The harness"
    W "  picks up ``bench/rust/`` automatically once it is installed."
    W ""
}

[IO.File]::WriteAllText($Out, $md.ToString(), (New-Object Text.UTF8Encoding($false)))
Write-Host "wrote $Out"
if ($mismatch.Count) { Write-Host "$($mismatch.Count) checksum mismatch(es)" -ForegroundColor Red; exit 1 }
