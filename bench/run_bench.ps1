# Goose benchmark harness.
#
# For every benchmark in the manifest below, at every requested size, this
# builds each implementation, runs it, and records wall time and peak working
# set. Every implementation of one benchmark must print the same checksum
# lines; a mismatch is reported and the row is marked, because a faster
# implementation that computes something else is not a result.
#
# Timing is whole-process wall clock. Goose has no clock builtin, so there is
# no in-process timer available to all three languages; the startup floor is
# measured separately (the `baseline` row) and printed alongside, so the reader
# can see how much of a short run is process creation.
#
# Toolchains are detected: MSVC (via vswhere) is required, clang and rustc are
# used when they are on PATH and the corresponding sources exist.
#
#   .\bench\run_bench.ps1                        # everything, 3 sizes
#   .\bench\run_bench.ps1 -Only tree,sexp        # re-measure a subset; the report
#                                                # still shows everything measured
#   .\bench\run_bench.ps1 -Sizes small -Reps 1   # a quick pass
#   .\bench\run_bench.ps1 -SkipBuild             # re-run what is already built
#   .\bench\run_bench.ps1 -ReportOnly            # regenerate results.md from saved
#                                                # measurements, e.g. after editing notes.md

param(
    [string[]] $Only = @(),
    [string[]] $Sizes = @("small", "medium", "large"),
    [int]      $Reps = 3,
    [string]   $Exe = "$PSScriptRoot\..\build\Debug\goose.exe",
    [string]   $Out = "$PSScriptRoot\results.md",
    [switch]   $SkipBuild,
    [switch]   $ReportOnly
)

$ErrorActionPreference = "Stop"
$root    = Resolve-Path "$PSScriptRoot\.."
$gendir  = "$PSScriptRoot\gen"
$goosedir = "$PSScriptRoot\goose"
$cppdir  = "$PSScriptRoot\cpp"
$rustdir = "$PSScriptRoot\rust"

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
       what = "Control: build a flat i32 array, then scan it eight times."
       sizes = @(1000000, 16000000, 128000000)
       goose = @(@{ label = "goose"; file = "sum.goose" })
       cpp = @(@{ label = "cpp vector"; file = "sum.cpp"; variant = 0 },
               @{ label = "cpp vector+reserve"; file = "sum.cpp"; variant = 1 }) },

    @{ name = "push"
       what = "Push N records while keeping pointers to every 64th one."
       sizes = @(1000000, 16000000, 64000000)
       goose = @(@{ label = "goose"; file = "push.goose" })
       cpp = @(@{ label = "cpp vector, indices"; file = "push.cpp"; variant = 0 },
               @{ label = "cpp vector+reserve, indices"; file = "push.cpp"; variant = 1 },
               @{ label = "cpp deque, pointers"; file = "push.cpp"; variant = 2 }) },

    @{ name = "strlist"
       what = "Split a text into N words, keep them as a list, scan it four times."
       sizes = @(200000, 2000000, 8000000)
       goose = @(@{ label = "goose"; file = "strlist.goose" })
       cpp = @(@{ label = "cpp vector<string>"; file = "strlist.cpp"; variant = 0 },
               @{ label = "cpp vector<string_view>"; file = "strlist.cpp"; variant = 1 },
               @{ label = "cpp flat offsets"; file = "strlist.cpp"; variant = 2 }) },

    @{ name = "records"
       what = "Build a log of N variant records, aggregate it four times."
       sizes = @(500000, 4000000, 16000000)
       goose = @(@{ label = "goose variable enum"; file = "records_var.goose" },
                 @{ label = "goose fixed enum"; file = "records_fixed.goose" })
       cpp = @(@{ label = "cpp virtual + unique_ptr"; file = "records.cpp"; variant = 0 },
               @{ label = "cpp variant + string"; file = "records.cpp"; variant = 1 },
               @{ label = "cpp variant + buffer"; file = "records.cpp"; variant = 2 }) },

    @{ name = "tree"
       what = "Build a complete binary tree of the given depth, sum it eight times."
       param = "depth"
       sizes = @(16, 20, 24)
       goose = @(@{ label = "goose pool + relative refs"; file = "tree.goose" })
       cpp = @(@{ label = "cpp unique_ptr"; file = "tree.cpp"; variant = 0 },
               @{ label = "cpp new/delete"; file = "tree.cpp"; variant = 1 },
               @{ label = "cpp arena + indices"; file = "tree.cpp"; variant = 2 }) },

    @{ name = "interp"
       what = "Build an expression tree of the given depth, evaluate it eight times."
       param = "depth"
       sizes = @(16, 20, 24)
       goose = @(@{ label = "goose case functions"; file = "interp.goose" })
       cpp = @(@{ label = "cpp virtual + unique_ptr"; file = "interp.cpp"; variant = 0 },
               @{ label = "cpp variant + arena"; file = "interp.cpp"; variant = 1 },
               @{ label = "cpp tagged union + arena"; file = "interp.cpp"; variant = 2 }) },

    @{ name = "graph"
       what = "Build adjacency for V vertices and 8V edges in one pass, then BFS from four sources."
       param = "V"
       sizes = @(100000, 500000, 2000000)
       goose = @(@{ label = "goose pool + relative refs"; file = "graph.goose" },
                 @{ label = "goose CSR two-pass"; file = "graph_csr.goose" })
       cpp = @(@{ label = "cpp vector<vector>"; file = "graph.cpp"; variant = 0 },
               @{ label = "cpp CSR two-pass"; file = "graph.cpp"; variant = 1 },
               @{ label = "cpp arena + indices"; file = "graph.cpp"; variant = 2 }) },

    @{ name = "words"
       what = "Count word frequencies over a text of N words."
       sizes = @(500000, 4000000, 16000000)
       goose = @(@{ label = "goose slices + open addressing"; file = "words.goose" })
       cpp = @(@{ label = "cpp unordered_map<string>"; file = "words.cpp"; variant = 0 },
               @{ label = "cpp unordered_map<string_view>"; file = "words.cpp"; variant = 1 },
               @{ label = "cpp open addressing"; file = "words.cpp"; variant = 2 }) },

    @{ name = "particles"
       what = "Integrate N particles for 200 steps of f32 vector math."
       sizes = @(100000, 1000000, 4000000)
       goose = @(@{ label = "goose AoS, elementwise"; file = "particles.goose" },
                 @{ label = "goose AoS, per component"; file = "particles_scalar.goose" })
       cpp = @(@{ label = "cpp AoS, per component"; file = "particles.cpp"; variant = 0 },
               @{ label = "cpp AoS, elementwise"; file = "particles.cpp"; variant = 2 },
               @{ label = "cpp SoA"; file = "particles.cpp"; variant = 1 }) },

    @{ name = "sexp"
       what = "Generate N s-expression forms, parse them into a tree, walk it four times."
       sizes = @(20000, 100000, 300000)
       goose = @(@{ label = "goose pool + varint text"; file = "sexp.goose" })
       cpp = @(@{ label = "cpp unique_ptr + string"; file = "sexp.cpp"; variant = 0 },
               @{ label = "cpp arena + string_view"; file = "sexp.cpp"; variant = 1 }) }
)

# --- toolchains --------------------------------------------------------------

function Import-VcVars {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere not found: MSVC is required" }
    $vsroot = & $vswhere -latest -property installationPath 2>$null
    if (-not $vsroot) { throw "no Visual Studio installation found" }
    foreach ($l in (cmd /c "`"$vsroot\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set")) {
        if ($l -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
    }
    return (& $vswhere -latest -property catalog_productDisplayVersion 2>$null)
}

$msvcVersion = Import-VcVars
$clang = (Get-Command clang.exe -ErrorAction SilentlyContinue)
$clangxx = (Get-Command clang++.exe -ErrorAction SilentlyContinue)
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

function Build-Goose([string]$name, [string]$file, [long]$n, [string]$tag) {
    $gsrc = "$gendir\$tag.goose"
    Write-Sized-Goose "$goosedir\$file" $gsrc $n
    $cfile = "$gendir\$tag.c"
    & $Exe -O2 -o $cfile $gsrc > "$gendir\$tag.goose.log" 2>&1
    if ($LASTEXITCODE -ne 0) { return @{ ok = $false; note = "goose compile failed, see $tag.goose.log" } }
    $exe = "$gendir\$tag.exe"
    cl /nologo /O2 "/DGS_STACK_RESERVE=$stackReserve" $cfile "/Fe:$exe" "/Fo:$gendir\$tag.obj" > "$gendir\$tag.cl.log" 2>&1
    if ($LASTEXITCODE -ne 0) { return @{ ok = $false; note = "cl failed, see $tag.cl.log" } }
    return @{ ok = $true; exe = $exe }
}

function Build-Cpp([string]$file, [int]$variant, [long]$n, [string]$tag) {
    $exe = "$gendir\$tag.exe"
    cl /nologo /O2 /EHsc /std:c++20 "/DBENCH_N=$n" "/DVARIANT=$variant" "$cppdir\$file" `
       "/Fe:$exe" "/Fo:$gendir\$tag.obj" > "$gendir\$tag.cl.log" 2>&1
    if ($LASTEXITCODE -ne 0) { return @{ ok = $false; note = "cl failed, see $tag.cl.log" } }
    return @{ ok = $true; exe = $exe }
}

# --- baseline ----------------------------------------------------------------
# What an empty program of each kind costs, so the reader can discount process
# startup from the small sizes.

function Measure-Baseline {
    $res = @{}
    $g = "$gendir\baseline_goose.goose"
    [IO.File]::WriteAllText($g, "fn main() { print(0); }`n", (New-Object Text.UTF8Encoding($false)))
    & $Exe -O2 -o "$gendir\baseline_goose.c" $g > $null 2>&1
    cl /nologo /O2 "/DGS_STACK_RESERVE=$stackReserve" "$gendir\baseline_goose.c" `
       "/Fe:$gendir\baseline_goose.exe" "/Fo:$gendir\baseline_goose.obj" > $null 2>&1
    $c = "$gendir\baseline_cpp.cpp"
    [IO.File]::WriteAllText($c, "#include <cstdio>`nint main(){printf(`"0\n`");}`n", (New-Object Text.UTF8Encoding($false)))
    cl /nologo /O2 /EHsc $c "/Fe:$gendir\baseline_cpp.exe" "/Fo:$gendir\baseline_cpp.obj" > $null 2>&1
    foreach ($k in "goose", "cpp") {
        $m = Measure-Exe "$gendir\baseline_$k.exe" 5
        $res[$k] = $m
    }
    return $res
}

# --- run ---------------------------------------------------------------------

$sizeNames = @("small", "medium", "large")
$selected = if ($Only.Count) { $benchmarks | Where-Object { $Only -contains $_.name } } else { $benchmarks }
if (-not $selected) { throw "no benchmarks matched -Only $($Only -join ',')" }

Write-Host "toolchain: MSVC $msvcVersion$(if ($clang) { ', clang ' + (& clang --version | Select-Object -First 1) })"
Write-Host "sizes: $($Sizes -join ', ')   reps: $Reps`n"

$baseline = if ($SkipBuild -or $ReportOnly) { $null } else { Measure-Baseline }

$results = @{}      # results[bench][impl][size] = measurement
$mismatch = @{}
$statefile = "$gendir\results.clixml"

# -ReportOnly regenerates results.md from the last run's measurements, so the
# hand-written commentary in notes.md can be edited without a rebuild.
if ($ReportOnly) {
    if (-not (Test-Path $statefile)) { throw "no saved measurements at $statefile; run without -ReportOnly first" }
    $state = Import-Clixml $statefile
    $results = $state.results
    $baseline = $state.baseline
    $mismatch = $state.mismatch
    $Reps = $state.reps
    $Sizes = $state.sizes
    $msvcVersion = $state.msvc
    $selected = $benchmarks | Where-Object { $results.ContainsKey($_.name) -and
                                             ($Only.Count -eq 0 -or $Only -contains $_.name) }
}

foreach ($b in $(if ($ReportOnly) { @() } else { $selected })) {
    $results[$b.name] = [ordered]@{}
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
            $tag = "$($b.name)_${sname}_$slug"
            if (-not $results[$b.name].Contains($im.label)) { $results[$b.name][$im.label] = @{} }
            if ($SkipBuild) {
                $build = @{ ok = (Test-Path "$gendir\$tag.exe"); exe = "$gendir\$tag.exe"; note = "not built" }
            } elseif ($im.kind -eq "goose") {
                $build = Build-Goose $b.name $im.file $n $tag
            } else {
                $build = Build-Cpp $im.file $im.variant $n $tag
            }
            if (-not $build.ok) {
                Write-Host ("  {0,-36} {1,-7} BUILD FAIL: {2}" -f $im.label, $sname, $build.note)
                $results[$b.name][$im.label][$sname] = @{ ok = $false; note = "build failed" }
                continue
            }
            $m = Measure-Exe $build.exe $Reps
            $results[$b.name][$im.label][$sname] = $m
            if ($m.ok) {
                $checks[$im.label] = $m.out
                Write-Host ("  {0,-36} {1,-7} {2,9:N1} ms  {3,8:N1} MB" -f $im.label, $sname, $m.ms, ($m.peak / 1MB))
            } else {
                Write-Host ("  {0,-36} {1,-7} RUN FAIL: {2}" -f $im.label, $sname, $m.note)
            }
        }
        # Every implementation must agree on the checksum lines.
        $distinct = $checks.Values | Sort-Object -Unique
        if (@($distinct).Count -gt 1) {
            $mismatch["$($b.name)/$sname"] = $checks
            Write-Host "  !! checksum mismatch in $($b.name)/$sname" -ForegroundColor Red
        }
    }
    Write-Host ""
}

# --- report ------------------------------------------------------------------

# A partial run (-Only, -Sizes) refreshes part of the picture; it merges into
# what previous runs measured rather than replacing it, so results.md always
# shows everything measured so far.
if (-not $ReportOnly) {
    if (Test-Path $statefile) {
        $prev = Import-Clixml $statefile
        $merged = $prev.results
        foreach ($bn in $results.Keys) {
            if (-not $merged.ContainsKey($bn)) { $merged[$bn] = @{} }
            foreach ($lbl in $results[$bn].Keys) {
                if (-not $merged[$bn].Contains($lbl)) { $merged[$bn][$lbl] = @{} }
                foreach ($sz in $results[$bn][$lbl].Keys) {
                    $merged[$bn][$lbl][$sz] = $results[$bn][$lbl][$sz]
                }
            }
        }
        $results = $merged
        foreach ($k in $prev.mismatch.Keys) { if (-not $mismatch.Contains($k)) { $mismatch[$k] = $prev.mismatch[$k] } }
        if (-not $baseline) { $baseline = $prev.baseline }
        $Sizes = @($sizeNames | Where-Object { $prev.sizes -contains $_ -or $Sizes -contains $_ })
    }
    Export-Clixml -Path $statefile -InputObject @{
        results = $results; baseline = $baseline; mismatch = $mismatch
        reps = $Reps; sizes = $Sizes; msvc = $msvcVersion
    }
    $selected = $benchmarks | Where-Object { $results.ContainsKey($_.name) }
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
W "* Each table is one benchmark. Rows are implementations, columns are the three data"
W "  sizes: the small one is meant to fit in cache, the large one is meant not to."
W "* ``ms`` is the best of $Reps whole-process wall times; ``MB`` is the peak working set over"
W "  those runs. Taking the best discards the first, cold run of a freshly built binary,"
W "  so these are warm numbers."
W "* Goose has no clock builtin, so there is no in-process timer common to all languages:"
W "  everything a program does -- generating its input, building its structure, tearing it"
W "  down -- is inside the measurement. That is deliberate for teardown, which is a real"
W "  cost the other languages pay and Goose does not, and it dilutes the ratios wherever"
W "  input generation is a large share of the work."
W "* Every implementation of a benchmark prints the same checksum lines and the harness"
W "  verifies they agree, so the rows are computing the same thing."
W ""
W "## Environment"
W ""
W "| | |"
W "|---|---|"
W "| CPU | $cpu |"
W "| RAM | $ram |"
W "| OS | $([Environment]::OSVersion.VersionString) |"
W "| C/C++ compiler | MSVC $msvcVersion (``/O2``, C++20) |"
W "| Goose | ``goose -O2`` to C, then the same MSVC ``/O2`` |"
W "| Rust | $(if ($haveRust) { (& rustc --version) } else { "not installed yet -- rows pending" }) |"
W "| clang | $(if ($clang) { (& clang --version | Select-Object -First 1) } else { "not installed yet -- rows pending" }) |"
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

foreach ($b in $selected) {
    if (-not $results[$b.name].Count) { continue }
    W "## $($b.name)"
    W ""
    W $b.what
    W ""
    $hdr = "| implementation |"
    $sep = "|---|"
    foreach ($s in $activeSizes) {
        $si = $sizeNames.IndexOf($s)
        $pname = if ($b.Contains("param")) { $b.param } else { "N" }
        $hdr += " $s ($pname=$($b.sizes[$si])) ms | MB |"
        $sep += "---:|---:|"
    }
    W $hdr
    W $sep
    # Manifest order, not hashtable order, so Goose rows come first.
    $labels = @($b.goose | ForEach-Object { $_.label }) + @($b.cpp | ForEach-Object { $_.label })
    foreach ($label in ($labels | Where-Object { $results[$b.name].Contains($_) })) {
        $row = "| $label |"
        foreach ($s in $activeSizes) {
            $m = $results[$b.name][$label][$s]
            $row += " $(Fmt-Ms $m) | $(Fmt-Mb $m) |"
        }
        W $row
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
W "* clang rows likewise: the harness builds both the generated Goose C and the C++ with"
W "  clang when it is on PATH, which is the comparison that removes the backend as a"
W "  variable."
W ""

[IO.File]::WriteAllText($Out, $md.ToString(), (New-Object Text.UTF8Encoding($false)))
Write-Host "wrote $Out"
if ($mismatch.Count) { Write-Host "$($mismatch.Count) checksum mismatch(es)" -ForegroundColor Red; exit 1 }
