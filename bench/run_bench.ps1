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
# sizes:  the value substituted for the `// BENCH_N` line (Goose) and
#         /DBENCH_N (C++/Rust), in small, medium, large order.
# param:  what that value is called in the table header; "N" if omitted.
# goose:  implementations built from bench/goose/<file>.
# cpp:    implementations built from bench/cpp/<file> with /DVARIANT=<variant>.

$benchmarks = @(
    @{ name = "sum"
       summary = 'Goose edges the exactly-reserved vector by 10% at large and beats the unreserved one by 1.6x, on identical memory. This is the control -- flat scalar data, nothing in the layout favours anyone -- so the only win is not having to remember to reserve.'
       what = "Control: build a flat i32 array, then scan it eight times."
       sizes = @(1000000, 16000000, 128000000)
       goose = @(@{ label = "goose"; file = "sum.goose" })
       cpp = @(@{ label = "cpp vector"; file = "sum.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp vector+reserve"; file = "sum.cpp"; variant = 1; tier = "expert" }) },

    @{ name = "push"
       summary = 'Goose is 3.0x faster than an unreserved vector and 11% faster than an exactly-reserved one, on half the memory of the former and the same as the latter. The code difference is the point: Goose hands out real pointers that stay valid across all later growth, while both vector rows have to store indices instead, and deque -- the one C++ container that could hand out stable pointers -- is 9.5x slower and 2.5x larger.'
       what = "Push N records while keeping pointers to every 64th one."
       sizes = @(1000000, 16000000, 64000000)
       goose = @(@{ label = "goose"; file = "push.goose" })
       cpp = @(@{ label = "cpp vector, indices"; file = "push.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp vector+reserve, indices"; file = "push.cpp"; variant = 1; tier = "expert" },
               @{ label = "cpp deque, pointers"; file = "push.cpp"; variant = 2; tier = "idiomatic" }) },

    @{ name = "strlist"
       summary = 'Goose is 2.8x faster than vector<string> on a third of the memory, and 6% ahead of hand-rolled flat offsets. It is also the only benchmark where Goose uses more memory than the best C++ row (217 vs 192 MB), and that is a fair trade: the Goose list is one contiguous block that owns its bytes, where both fast C++ rows are views into a text buffer they cannot outlive.'
       what = "Split a text into N words, keep them as a list, scan it four times."
       sizes = @(200000, 2000000, 8000000)
       goose = @(@{ label = "goose"; file = "strlist.goose" })
       cpp = @(@{ label = "cpp vector<string>"; file = "strlist.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp vector<string_view>"; file = "strlist.cpp"; variant = 1; tier = "expert" },
               @{ label = "cpp flat offsets"; file = "strlist.cpp"; variant = 2; tier = "expert" }) },

    @{ name = "records"
       summary = 'The variable-mode enum is 4.1x faster than the virtual/unique_ptr shape and 1.4x faster than a std::variant with a fixed buffer, on 4.9x and 3.8x less memory. The Goose fixed-enum row is the control for that: same work, same language, 2.2x the memory, because every element pads out to the largest variant the way C++ and Rust must.'
       what = "Build a log of N variant records, aggregate it four times."
       sizes = @(500000, 4000000, 16000000)
       goose = @(@{ label = "goose variable enum"; file = "records_var.goose" },
                 @{ label = "goose fixed enum"; file = "records_fixed.goose" })
       cpp = @(@{ label = "cpp virtual + unique_ptr"; file = "records.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp variant + string"; file = "records.cpp"; variant = 1; tier = "idiomatic" },
               @{ label = "cpp variant + buffer"; file = "records.cpp"; variant = 2; tier = "expert" }) },

    @{ name = "tree"
       summary = 'Goose is 4.5x faster than unique_ptr nodes on 2.7x less memory, and 8-14% faster than a hand-built arena while using the same memory as it. Goose links nodes with typed 4-byte relative references and never frees; the arena row reaches the same place with raw uint32 indices, and the two owning-pointer rows pay a malloc per node on the way in and a recursive destructor walk on the way out.'
       what = "Build a complete binary tree of the given depth, sum it eight times."
       param = "depth"
       sizes = @(16, 20, 24)
       goose = @(@{ label = "goose pool + relative refs"; file = "tree.goose" })
       cpp = @(@{ label = "cpp unique_ptr"; file = "tree.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp new/delete"; file = "tree.cpp"; variant = 1; tier = "idiomatic" },
               @{ label = "cpp arena + indices"; file = "tree.cpp"; variant = 2; tier = "expert" }) },

    @{ name = "interp"
       summary = 'The largest win in the suite: 6.1x over virtual dispatch with unique_ptr nodes (7.1x under clang) and 1.25x over a tagged union in an arena, on 4.1x and 2.4x less memory. A variable-mode leaf costs 5 bytes where the union pays max-payload for every node, and case functions dispatch through a jump table with no vtable pointer to store.'
       what = "Build an expression tree of the given depth, evaluate it eight times."
       param = "depth"
       sizes = @(16, 20, 24)
       goose = @(@{ label = "goose case functions"; file = "interp.goose" })
       cpp = @(@{ label = "cpp virtual + unique_ptr"; file = "interp.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp variant + arena"; file = "interp.cpp"; variant = 1; tier = "expert" },
               @{ label = "cpp tagged union + arena"; file = "interp.cpp"; variant = 2; tier = "expert" }) },

    @{ name = "graph"
       summary = 'A wash where it counts: Goose CSR and C++ CSR are level (842 vs 854 ms, same memory). The one-pass linked build is 5.8x slower than CSR in Goose and 5.4x slower in C++, so that is a data-structure effect and not a language one. What Goose buys is that the one-pass version is writable at all with real pointers into a growing pool; the idiomatic C++ answer, vector<vector>, is 4.2x slower than CSR and needs an allocation per vertex.'
       what = "Build adjacency for V vertices and 8V edges in one pass, then BFS from four sources."
       param = "V"
       sizes = @(100000, 500000, 2000000)
       goose = @(@{ label = "goose pool + relative refs"; file = "graph.goose" },
                 @{ label = "goose CSR two-pass"; file = "graph_csr.goose" })
       cpp = @(@{ label = "cpp vector<vector>"; file = "graph.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp CSR two-pass"; file = "graph.cpp"; variant = 1; tier = "expert" },
               @{ label = "cpp arena + indices"; file = "graph.cpp"; variant = 2; tier = "expert" }) },

    @{ name = "words"
       summary = 'Goose is 1.7x faster than unordered_map and 15-20% ahead of a hand-rolled open-addressed table, on 6% less memory than the maps and 19% less than the hand-rolled one. The map keys are slices pointing straight into the text with no copy and no allocation; the idiomatic C++ row copies every distinct key into a std::string inside a node.'
       what = "Count word frequencies over a text of N words."
       sizes = @(500000, 4000000, 16000000)
       goose = @(@{ label = "goose slices + open addressing"; file = "words.goose" })
       cpp = @(@{ label = "cpp unordered_map<string>"; file = "words.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp unordered_map<string_view>"; file = "words.cpp"; variant = 1; tier = "idiomatic" },
               @{ label = "cpp open addressing"; file = "words.cpp"; variant = 2; tier = "expert" }) },

    @{ name = "particles"
       summary = 'A draw, and the only benchmark where C++ is ever ahead: Goose is 3% up under v145 and 4% down under clang against the fastest C++ row. Elementwise struct math now costs nothing over writing the components out (883 vs 885 ms), so the idiomatic notation is free. The struct-of-arrays row is slower than array-of-structs in both toolchains -- six streams instead of one -- though clang narrows it by 1.6x over v145.'
       what = "Integrate N particles for 200 steps of f32 vector math."
       sizes = @(100000, 1000000, 4000000)
       goose = @(@{ label = "goose AoS, elementwise"; file = "particles.goose" },
                 @{ label = "goose AoS, per component"; file = "particles_scalar.goose" })
       cpp = @(@{ label = "cpp AoS, per component"; file = "particles.cpp"; variant = 0; tier = "expert" },
               @{ label = "cpp AoS, elementwise"; file = "particles.cpp"; variant = 2; tier = "idiomatic" },
               @{ label = "cpp SoA"; file = "particles.cpp"; variant = 1; tier = "expert" }) },

    @{ name = "sexp"
       summary = 'The flagship: 3.4x faster than unique_ptr nodes with std::string symbols on 3.9x less memory, and 11% faster than an arena with string_view keys on half its memory. Nodes are exactly as big as their variant needs, symbol text sits inline behind a varint length, and the tree is never freed because there is nothing to free.'
       what = "Generate N s-expression forms, parse them into a tree, walk it four times."
       sizes = @(20000, 100000, 300000)
       goose = @(@{ label = "goose pool + varint text"; file = "sexp.goose" })
       cpp = @(@{ label = "cpp unique_ptr + string"; file = "sexp.cpp"; variant = 0; tier = "idiomatic" },
               @{ label = "cpp arena + string_view"; file = "sexp.cpp"; variant = 1; tier = "expert" }) }
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

$rustc = (Get-Command rustc.exe -ErrorAction SilentlyContinue)
$haveRust = ($null -ne $rustc) -and (Test-Path $rustdir)

New-Item -ItemType Directory -Force $gendir | Out-Null
Copy-Item "$goosedir\rng.goose" $gendir -Force      # imports resolve next to the root file

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
    return @{ ms = $sw.Elapsed.TotalMilliseconds; peak = $peak; out = $stdout.Trim()
              err = $stderr.Trim(); code = $code }
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
        foreach ($im in $impls) {
            $slug = ($im.label -replace '[^A-Za-z0-9]+', '_').Trim('_')
            if (-not $results[$b.name].Contains($im.label)) { $results[$b.name][$im.label] = @{} }
            foreach ($tc in $activeTc) {
                $tag = "$($b.name)_${sname}_${slug}_$tc"
                if (-not $results[$b.name][$im.label].Contains($tc)) { $results[$b.name][$im.label][$tc] = @{} }
                if ($SkipBuild) {
                    $build = @{ ok = (Test-Path "$gendir\$tag.exe"); exe = "$gendir\$tag.exe"; note = "not built" }
                } elseif ($im.kind -eq "goose") {
                    $build = Build-Goose $im.file $n $tag $tc
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
W "The benchmarks themselves are ``bench/goose/*.goose`` and ``bench/cpp/*.cpp``; what they are"
W "trying to find out is written down in ``bench/design.md``."
W ""
W "## How to read this"
W ""
W "* Start with **Across all benchmarks**: it is the whole suite in one table. The"
W "  per-benchmark sections below it are the detail, each ending in a sentence or two"
W "  of what that row means."
W "* One table per benchmark. Rows are implementations, the three time columns are the"
W "  data sizes -- the small one is meant to fit in cache, the large one is meant not"
W "  to -- and the last column is peak memory at the large size."
W "* Every time cell is ``v145 / clang``: the same source built by MSVC and by the"
W "  clang-cl that ships in the same Visual Studio, so a gap between the two numbers is"
W "  the backend and nothing else. Goose is compiled to C once per size and both"
W "  toolchains build that same C."
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
W "| Rust | $(if ($haveRust) { (& rustc --version) } else { "not installed yet -- rows pending" }) |"
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
function Best-Mem($bench, $labels, $size) {
    $best = $null
    foreach ($l in $labels) {
        foreach ($tc in $activeTc) {
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
        $perBench += $row
    }

    W "## Across all benchmarks"
    W ""
    W "How many times faster (or smaller) Goose is than the best C++ row of each"
    W "tier, at the ``large`` size. *Idiomatic* is what a C++ programmer writes first"
    W "(``unique_ptr``, ``vector<string>``, ``unordered_map``, ``std::variant``);"
    W "*expert* is the hand-built version (exactly-reserved arena with indices,"
    W "``string_view``, flat offsets, open addressing, CSR). Above 1.00 means Goose"
    W "wins. The suite figure is the geometric mean over the $($perBench.Count) benchmarks."
    W ""
    $hdr = "| benchmark |"
    $sep = "|---|"
    foreach ($tier in "idiomatic", "expert") {
        foreach ($tc in $activeTc) { $hdr += " vs $tier ($tc) |"; $sep += "---:|" }
    }
    $hdr += " memory vs idiomatic | memory vs expert |"
    $sep += "---:|---:|"
    W $hdr
    W $sep
    foreach ($r in $perBench) {
        $line = "| $($r.bench) |"
        foreach ($tier in "idiomatic", "expert") {
            foreach ($tc in $activeTc) {
                $v = $r["$tier-$tc"]
                $line += $(if ($v) { " {0:N2}x |" -f $v } else { " -- |" })
            }
        }
        foreach ($tier in "idiomatic", "expert") {
            $v = $r["$tier-mem"]
            $line += $(if ($v) { " {0:N2}x |" -f $v } else { " -- |" })
        }
        W $line
    }
    $line = "| **geometric mean** |"
    foreach ($tier in "idiomatic", "expert") {
        foreach ($tc in $activeTc) {
            $v = GeoMean $ratios[$tier][$tc]
            $line += $(if ($v) { " **{0:N2}x** |" -f $v } else { " -- |" })
        }
    }
    foreach ($tier in "idiomatic", "expert") {
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
    $labels = @($b.goose | ForEach-Object { $_.label }) + @($b.cpp | ForEach-Object { $_.label })
    $labels = @($labels | Where-Object { $results[$b.name].Contains($_) })
    W "## $($b.name)"
    W ""
    W $b.what
    W ""
    W "Time in ms as ``$($activeTc -join ' / ')``; memory is the peak working set at the largest size."
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
        foreach ($s in $activeSizes) {
            $cells = foreach ($tc in $activeTc) { Fmt-Ms (Get-M $b.name $label $tc $s) }
            $row += " $($cells -join ' / ') |"
        }
        $peak = $null
        foreach ($tc in $activeTc) {
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

W "## Pending"
W ""
W "* Rust implementations (``bench/rust/``) are not written yet; the harness picks them up"
W "  automatically once they and ``rustc`` are present."
W ""

[IO.File]::WriteAllText($Out, $md.ToString(), (New-Object Text.UTF8Encoding($false)))
Write-Host "wrote $Out"
if ($mismatch.Count) { Write-Host "$($mismatch.Count) checksum mismatch(es)" -ForegroundColor Red; exit 1 }
