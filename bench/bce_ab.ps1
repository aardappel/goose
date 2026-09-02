# What bounds-check elimination is worth, measured directly: the same Goose
# sources built with and without `--no-bce`, under both toolchains, at the size
# baked into each source file. The compiler's own elision counts are printed
# alongside, so a benchmark where nothing was elided is visible as such.
#
#   .\bench\bce_ab.ps1                       # the benchmarks that index at all
#   .\bench\bce_ab.ps1 -Names graph,words    # a subset
#
# Benchmarks with no index or slice expressions (sum, push, tree, interp,
# particles) are omitted: there is nothing for the pass to do in them.

param(
    [string[]] $Names = @("graph", "graph_csr", "words", "strlist", "sexp", "records_var",
                          "lru", "scene", "calc", "respond", "blur", "blur_rows"),
    [int]      $Reps = 4,
    [string]   $Exe = "$PSScriptRoot\..\build\Debug\goose.exe",
    [string]   $Dir = "$PSScriptRoot\gen\bce"
)

$ErrorActionPreference = "Stop"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsroot = & $vswhere -latest -prerelease -property installationPath 2>$null
if (-not $vsroot) { throw "no Visual Studio installation found" }
foreach ($l in (cmd /c "`"$vsroot\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set")) {
    if ($l -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
}
$ccs = [ordered]@{ v145 = "cl"; clang = "$vsroot\VC\Tools\Llvm\x64\bin\clang-cl.exe" }

New-Item -ItemType Directory -Force $Dir | Out-Null
Copy-Item "$PSScriptRoot\goose\rng.goose" $Dir -Force

# Best of $reps, after two discarded warm-ups (see notes.md on why).
function Time-It([string]$path, [int]$reps) {
    for ($i = 0; $i -lt 2; $i++) { & $path | Out-Null }
    $best = [double]::MaxValue
    $out = $null
    for ($i = 0; $i -lt $reps; $i++) {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $out = & $path
        $sw.Stop()
        if ($sw.Elapsed.TotalMilliseconds -lt $best) { $best = $sw.Elapsed.TotalMilliseconds }
    }
    return @{ ms = $best; out = ($out -join ",") }
}

$rows = @()
foreach ($n in $Names) {
    Copy-Item "$PSScriptRoot\goose\$n.goose" $Dir -Force
    $row = @{ name = $n; stat = "" }
    foreach ($mode in "bce", "nobce") {
        $c = Join-Path $Dir "$n-$mode.c"
        if (Test-Path $c) { Remove-Item $c }
        $gargs = @("-O2")
        if ($mode -eq "nobce") { $gargs += "--no-bce" }
        $gargs += @("-o", $c, (Join-Path $Dir "$n.goose"))
        $log = (& $Exe @gargs) -join "`n"
        if (-not (Test-Path $c)) { Write-Host "goose failed on $n $mode"; Write-Host $log; continue }
        if ($mode -eq "bce" -and $log -match "bce: (.*)") { $row.stat = $matches[1] }
        foreach ($tc in $ccs.Keys) {
            # Not $exe: PowerShell names are case-insensitive, so that would
            # overwrite the $Exe parameter holding the compiler path.
            $bin = Join-Path $Dir "$n-$mode-$tc.exe"
            if (Test-Path $bin) { Remove-Item $bin }
            & $ccs[$tc] /nologo /O2 "/DGS_STACK_RESERVE=2147483648ull" $c `
                "/Fe:$bin" "/Fo:$(Join-Path $Dir "$n-$mode-$tc.obj")" `
                > (Join-Path $Dir "$n-$mode-$tc.cc.log") 2>&1
            if (-not (Test-Path $bin)) { Write-Host "$tc failed on $n $mode"; continue }
            $r = Time-It $bin $Reps
            $row["$mode-$tc"] = $r.ms
            $row["out-$mode"] = $r.out
        }
    }
    if ($row["out-bce"] -and $row["out-nobce"] -and $row["out-bce"] -ne $row["out-nobce"]) {
        Write-Host "!! $n : output differs with and without BCE -- the pass is unsound" -ForegroundColor Red
    }
    $rows += $row
}

function Gain($on, $off) { if ($on -and $off) { ($off / $on - 1) * 100 } else { 0 } }

Write-Host ""
Write-Host ("{0,-12} {1,-34} {2,9} {3,9} {4,7}   {5,9} {6,9} {7,7}" -f `
            "benchmark", "elided", "v145 on", "v145 off", "gain", "clang on", "clang off", "gain")
foreach ($r in $rows) {
    Write-Host ("{0,-12} {1,-34} {2,9:N1} {3,9:N1} {4,6:N1}%   {5,9:N1} {6,9:N1} {7,6:N1}%" -f `
                $r.name, $r.stat, $r["bce-v145"], $r["nobce-v145"], (Gain $r["bce-v145"] $r["nobce-v145"]),
                $r["bce-clang"], $r["nobce-clang"], (Gain $r["bce-clang"] $r["nobce-clang"]))
}
